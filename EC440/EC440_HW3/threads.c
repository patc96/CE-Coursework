#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <setjmp.h>
#include <semaphore.h>
#include <stdint.h>
#include <string.h>
#include "ec440threads.h"

#define STACK_SIZE 32767
#define READY 0
#define RUNNING 1
#define EXITED 2
#define BLOCKED 3
#define MAX_THREADS 128
#define MAX_SEMAPHORES 128

typedef struct thread_control_block {
    pthread_t id;
    jmp_buf context;
    void *stack;
    int state;
    void *(*start_routine)(void *);
    void *arg;
    void *exit_value;
} thread_control_block;

typedef struct custom_semaphore {
    int value;
    int initialized;
    pthread_t waiting_threads[MAX_THREADS];
    int wait_count;
} custom_semaphore;

// Forward declaration of pthread_exit_wrapper
void pthread_exit_wrapper();

static thread_control_block tcb[MAX_THREADS];
static int thread_count = 1;
static pthread_t current_thread = 0;
static pthread_t next_thread = 0;
static struct itimerval timer;
static custom_semaphore *semaphore_array[MAX_SEMAPHORES] = {NULL};
static int next_semaphore_id = 0;

static sigset_t alarm_mask;

#define JB_RBX 0
#define JB_RBP 1
#define JB_R12 2
#define JB_R13 3
#define JB_R14 4
#define JB_R15 5
#define JB_RSP 6
#define JB_PC 7

void lock() {
    sigemptyset(&alarm_mask);
    sigaddset(&alarm_mask, SIGALRM);
    sigprocmask(SIG_BLOCK, &alarm_mask, NULL);
}

void unlock() {
    sigprocmask(SIG_UNBLOCK, &alarm_mask, NULL);
}

void schedule(int signum) {
    if (setjmp(tcb[current_thread].context) == 0) {
        do {
            next_thread = (current_thread + 1) % thread_count;
            current_thread = next_thread;
        } while (tcb[current_thread].state != READY && current_thread != 0);

        longjmp(tcb[current_thread].context, 1);
    }
}

void pthread_exit(void *value_ptr) {
    lock();
    tcb[current_thread].exit_value = value_ptr;
    tcb[current_thread].state = EXITED;

    // Unblock any threads waiting on this thread
    for (int i = 0; i < thread_count; i++) {
        if (tcb[i].state == BLOCKED && tcb[i].id == current_thread) {
            tcb[i].state = READY;
        }
    }

    unlock();
    schedule(0);
    while (1);
}

pthread_t pthread_self(void) {
    return tcb[current_thread].id;
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg) {
    if (thread_count >= MAX_THREADS) {
        return -1;
    }

    *thread = thread_count;
    tcb[thread_count].id = thread_count;
    tcb[thread_count].stack = malloc(STACK_SIZE);
    if (tcb[thread_count].stack == NULL) {
        return -1;
    }
    tcb[thread_count].start_routine = start_routine;
    tcb[thread_count].arg = arg;
    tcb[thread_count].state = READY;

    if (setjmp(tcb[thread_count].context) == 0) {
        unsigned long *stack_top = (unsigned long *)(tcb[thread_count].stack + STACK_SIZE - sizeof(unsigned long));
        *stack_top = (unsigned long)pthread_exit_wrapper;  // Set the return address to pthread_exit_wrapper
        ((unsigned long *)tcb[thread_count].context)[JB_RSP] = ptr_mangle((unsigned long)stack_top);
        ((unsigned long *)tcb[thread_count].context)[JB_PC] = ptr_mangle((unsigned long)start_thunk);
        ((unsigned long *)tcb[thread_count].context)[JB_R12] = (unsigned long)start_routine;
        ((unsigned long *)tcb[thread_count].context)[JB_R13] = (unsigned long)arg;
        thread_count++;
    }

    schedule(0);
    return 0;
}

int pthread_join(pthread_t thread, void **value_ptr) {
    lock();

    int target_index = -1;
    for (int i = 0; i < thread_count; i++) {
        if (tcb[i].id == thread) {
            target_index = i;
            break;
        }
    }

    if (target_index == -1) {
        unlock();
        return -1;  // Thread not found
    }

    while (tcb[target_index].state != EXITED) {
        tcb[current_thread].state = BLOCKED;
        unlock();
        schedule(0);
        lock();
    }

    if (value_ptr) {
        *value_ptr = tcb[target_index].exit_value;
    }

    unlock();
    return 0;
}

int sem_init(sem_t *sem, int pshared, unsigned value) {
    if (next_semaphore_id >= MAX_SEMAPHORES) return -1;

    custom_semaphore *csem = malloc(sizeof(custom_semaphore));
    if (!csem) return -1;

    csem->value = value;
    csem->initialized = 1;
    csem->wait_count = 0;

    semaphore_array[next_semaphore_id] = csem;
    *(uintptr_t *)sem = (uintptr_t)next_semaphore_id;
    next_semaphore_id++;
    return 0;
}

int sem_wait(sem_t *sem) {
    int sem_index = *(uintptr_t *)sem;
    if (sem_index < 0 || sem_index >= MAX_SEMAPHORES) return -1;
    custom_semaphore *csem = semaphore_array[sem_index];
    if (!csem || !csem->initialized) return -1;

    lock();
    if (csem->value > 0) {
        csem->value--;
    } else {
        csem->waiting_threads[csem->wait_count++] = current_thread;
        tcb[current_thread].state = BLOCKED;
        schedule(0);
    }
    unlock();
    return 0;
}

int sem_post(sem_t *sem) {
    int sem_index = *(uintptr_t *)sem;
    if (sem_index < 0 || sem_index >= MAX_SEMAPHORES) return -1;
    custom_semaphore *csem = semaphore_array[sem_index];
    if (!csem || !csem->initialized) return -1;

    lock();
    if (csem->wait_count > 0) {
        pthread_t next_thread = csem->waiting_threads[--csem->wait_count];
        tcb[next_thread].state = READY;
    } else {
        csem->value++;
    }
    unlock();
    return 0;
}

int sem_destroy(sem_t *sem) {
    int sem_index = *(uintptr_t *)sem;
    if (sem_index < 0 || sem_index >= MAX_SEMAPHORES) return -1;
    custom_semaphore *csem = semaphore_array[sem_index];
    if (!csem || !csem->initialized) return -1;

    free(csem);
    semaphore_array[sem_index] = NULL;
    *(uintptr_t *)sem = (uintptr_t)-1;
    return 0;
}

void initialize_scheduler() {
    signal(SIGALRM, schedule);
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 50000;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 50000;
    setitimer(ITIMER_REAL, &timer, NULL);
}

__attribute__((constructor)) void init() {
    tcb[0].id = 0;
    tcb[0].state = RUNNING;
    initialize_scheduler();
}

// Define pthread_exit_wrapper
void pthread_exit_wrapper() {
    unsigned long res;
    asm("movq %%rax, %0" : "=r"(res));  // Capture return value from rax register
    pthread_exit((void *)res);
}
