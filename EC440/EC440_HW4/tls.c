#define _GNU_SOURCE

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <stdint.h>
#include <unistd.h>

#define HASH_SIZE 128

typedef struct page {
    void *address;   // Start address of the page
    int ref_count;   // Reference count for shared pages
} page_t;

typedef struct thread_local_storage {
    pthread_t tid;        // Thread ID
    unsigned int size;    // Size in bytes
    unsigned int page_num; // Number of pages
    page_t **pages;       // Array of pointers to pages
} TLS;

typedef struct hash_element {
    pthread_t tid;
    TLS *tls;
    struct hash_element *next;
} hash_element_t;

// Globals
hash_element_t *hash_table[HASH_SIZE] = {0};
int page_size;
int initialized = 0;

// Function Declarations
void tls_init();
void tls_protect(page_t *p);
void tls_unprotect(page_t *p);
void tls_handle_page_fault(int sig, siginfo_t *si, void *context);
int hash_func(pthread_t tid);
int tls_create(unsigned int size);
int tls_destroy();
int tls_read(unsigned int offset, unsigned int length, char *buffer);
int tls_write(unsigned int offset, unsigned int length, char *buffer);
int tls_clone(pthread_t tid);

// Initializes TLS
void tls_init() {
    struct sigaction sigact;
    page_size = getpagesize(); // Finds page size
    sigemptyset(&sigact.sa_mask); // Sets up signal sigaction
    sigact.sa_flags = SA_SIGINFO;
    sigact.sa_sigaction = tls_handle_page_fault; // Assign tls_handle_page_fault to be the handler
    if (sigaction(SIGSEGV, &sigact, NULL) != 0) {
        fprintf(stderr, "tls_init: sigaction failed\n");
        exit(1);
    }
    initialized = 1;
}

// Uses mprotect to set a page's permissions to prevent all access.
void tls_protect(page_t *p) {
    if (mprotect(p->address, page_size, 0)) {
        fprintf(stderr, "tls_protect: Could not protect page\n");
        exit(1);
    }
}
// Grants access to the page temporarily.
void tls_unprotect(page_t *p) {
    if (mprotect(p->address, page_size, PROT_READ | PROT_WRITE)) {
        fprintf(stderr, "tls_unprotect: Could not unprotect page\n");
        exit(1);
    }
}


void tls_handle_page_fault(int sig, siginfo_t *si, void *context) {
    void *fault_addr = si->si_addr; // Finds the address that caused the segmentation fault.
    uintptr_t page_fault_addr = (uintptr_t)fault_addr & ~(page_size - 1); // Aligns the faulting address to the start of the page.

    // Iterates through the hash table to locate TLS containing the faulting page.
    for (int i = 0; i < HASH_SIZE; i++) {
        hash_element_t *current = hash_table[i];
        while (current) {
            TLS *tls = current->tls;
            for (unsigned int j = 0; j < tls->page_num; j++) {
                if ((uintptr_t)tls->pages[j]->address == page_fault_addr) {
                    pthread_exit(NULL); // Offending thread is terminated if faulting address belongs to a TLS page.
                }
            }
            current = current->next;
        }
    }

    signal(SIGSEGV, SIG_DFL);
    raise(SIGSEGV);
}

// Maps thread ID to the global hash table.
int hash_func(pthread_t tid) {
    return tid % HASH_SIZE;
}

int tls_create(unsigned int size) {
    if (!initialized) tls_init();

    if (size <= 0) return -1;

    pthread_t tid = pthread_self();
    int index = hash_func(tid);

    hash_element_t *current = hash_table[index];
    while (current) {
        if (current->tid == tid) return -1;
        current = current->next;
    }

    TLS *tls = calloc(1, sizeof(TLS));
    tls->tid = tid;
    tls->size = size;
    tls->page_num = (size + page_size - 1) / page_size;
    tls->pages = calloc(tls->page_num, sizeof(page_t *));

    for (unsigned int i = 0; i < tls->page_num; i++) {
        page_t *p = calloc(1, sizeof(page_t));
        p->address = mmap(0, page_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p->address == MAP_FAILED) {
            fprintf(stderr, "tls_create: mmap failed\n");
            exit(1);
        }
        p->ref_count = 1;
        tls->pages[i] = p;
    }

    hash_element_t *new_elem = calloc(1, sizeof(hash_element_t)); // Create hash element to link thread ID to the new tls and add to hash table
    new_elem->tid = tid;
    new_elem->tls = tls;
    new_elem->next = hash_table[index];
    hash_table[index] = new_elem;

    return 0;
}

int tls_destroy() {
    pthread_t tid = pthread_self();
    int index = hash_func(tid);

    hash_element_t *current = hash_table[index];
    hash_element_t *prev = NULL;

    while (current) {
        if (current->tid == tid) {
            TLS *tls = current->tls;

            for (unsigned int i = 0; i < tls->page_num; i++) {
                page_t *p = tls->pages[i];
                if (--p->ref_count == 0) {
                    munmap(p->address, page_size); //unmaps page if ref_count reaches 0.
                    free(p);
                }
            }

            free(tls->pages);
            free(tls);

            if (prev) prev->next = current->next;
            else hash_table[index] = current->next; 

            free(current);
            return 0;
        }
        prev = current;
        current = current->next;
    }

    return -1;
}

int tls_read(unsigned int offset, unsigned int length, char *buffer) {
    pthread_t tid = pthread_self();
    int index = hash_func(tid);

    hash_element_t *current = hash_table[index];
    while (current && current->tid != tid) {
        current = current->next;
    }

    if (!current) return -1;

    TLS *tls = current->tls;
    if (offset + length > tls->size) return -1;

    for (unsigned int i = 0; i < tls->page_num; i++) {
        tls_unprotect(tls->pages[i]);
    }

    for (unsigned int i = 0; i < length; i++) {
        unsigned int page_index = (offset + i) / page_size;
        unsigned int page_offset = (offset + i) % page_size;
        char *src = (char *)tls->pages[page_index]->address + page_offset;
        buffer[i] = *src;
    }

    for (unsigned int i = 0; i < tls->page_num; i++) {
        tls_protect(tls->pages[i]);
    }

    return 0;
}

int tls_write(unsigned int offset, unsigned int length, char *buffer) {
    pthread_t tid = pthread_self();
    int index = hash_func(tid);

    hash_element_t *current = hash_table[index];
    while (current && current->tid != tid) {
        current = current->next;
    }

    if (!current) return -1;

    TLS *tls = current->tls;
    if (offset + length > tls->size) return -1;

    for (unsigned int i = 0; i < tls->page_num; i++) {
        tls_unprotect(tls->pages[i]);
    }

    for (unsigned int i = 0; i < length; i++) {
        unsigned int page_index = (offset + i) / page_size;
        unsigned int page_offset = (offset + i) % page_size;
        page_t *p = tls->pages[page_index];

        if (p->ref_count > 1) {
            page_t *copy = calloc(1, sizeof(page_t));
            copy->address = mmap(0, page_size, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            memcpy(copy->address, p->address, page_size);
            copy->ref_count = 1;
            tls->pages[page_index] = copy;
            p->ref_count--;
            tls_protect(p);
            p = copy;
        }

        char *dst = (char *)p->address + page_offset;
        *dst = buffer[i];
    }

    for (unsigned int i = 0; i < tls->page_num; i++) {
        tls_protect(tls->pages[i]);
    }

    return 0;
}

int tls_clone(pthread_t tid) {
    pthread_t self_tid = pthread_self();
    int index = hash_func(tid);
    int self_index = hash_func(self_tid);

    hash_element_t *target = hash_table[index];
    hash_element_t *self = hash_table[self_index];

    while (target && target->tid != tid) {
        target = target->next;
    }
    while (self && self->tid != self_tid) {
        self = self->next;
    }

    if (!target || self) return -1;

    TLS *src_tls = target->tls;
    TLS *new_tls = calloc(1, sizeof(TLS));
    new_tls->tid = self_tid;
    new_tls->size = src_tls->size;
    new_tls->page_num = src_tls->page_num;
    new_tls->pages = calloc(new_tls->page_num, sizeof(page_t *));

    for (unsigned int i = 0; i < src_tls->page_num; i++) {
        page_t *p = src_tls->pages[i];
        new_tls->pages[i] = p;
        p->ref_count++;
    }

    hash_element_t *new_elem = calloc(1, sizeof(hash_element_t));
    new_elem->tid = self_tid;
    new_elem->tls = new_tls;
    new_elem->next = hash_table[self_index];
    hash_table[self_index] = new_elem;

    return 0;
}
