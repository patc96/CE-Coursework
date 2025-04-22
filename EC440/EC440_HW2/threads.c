#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <setjmp.h>
#include "ec440threads.h"

#define MAX_THREADS 128
#define STACK_SIZE 32767
#define READY 0
#define RUNNING 1
#define EXITED 2

typedef struct thread_control_block {
    pthread_t id;
    jmp_buf context;
    void *stack;
    int state;
    void *(*start_routine)(void *);
    void *arg;
} thread_control_block;

static thread_control_block tcb[MAX_THREADS];
static int thread_count = 1;  // Start with main thread
static pthread_t current_thread = 0;  // Main thread
static pthread_t next_thread = 0;
static struct itimerval timer;

// Helper macros to access jmp_buf fields
#define JB_RBX 0
#define JB_RBP 1
#define JB_R12 2
#define JB_R13 3
#define JB_R14 4
#define JB_R15 5
#define JB_RSP 6
#define JB_PC 7

void schedule(int signum) {
    if (setjmp(tcb[current_thread].context) == 0) {
        // Round-robin scheduling: pick the next READY thread
        do {
            next_thread = (current_thread + 1) % thread_count;
            current_thread = next_thread;
        } while (tcb[current_thread].state != READY && current_thread != 0);

        // Load the new thread context
        longjmp(tcb[current_thread].context, 1);
    }
}

void pthread_exit(void *value_ptr) __attribute__((noreturn));

void pthread_exit(void *value_ptr) {
    tcb[current_thread].state = EXITED;

    // Check if all threads have exited, then exit the program
    int alive = 0;
    for (int i = 0; i < thread_count; i++) {
        if (tcb[i].state != EXITED) {
            alive = 1;
            break;
        }
    }
    if (!alive) {
        exit(0);
    }
    schedule(0);  // Schedule the next thread

    // noreturn attribute is meant to indicate this function doesn't return
    while (1);  // Keep the thread from returning
}

pthread_t pthread_self(void) {
    return tcb[current_thread].id;
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg) {
    if (thread_count >= MAX_THREADS) {
        return -1;  // Max thread limit reached
    }

    *thread = thread_count;
    tcb[thread_count].id = thread_count;
    tcb[thread_count].stack = malloc(STACK_SIZE);
    if (tcb[thread_count].stack == NULL) {
        return -1;  // Stack allocation failed
    }
    tcb[thread_count].start_routine = start_routine;
    tcb[thread_count].arg = arg;
    tcb[thread_count].state = READY;

    if (setjmp(tcb[thread_count].context) == 0) {
        // Set up the thread's stack and registers
        unsigned long *stack_top = (unsigned long *)(tcb[thread_count].stack + STACK_SIZE - sizeof(unsigned long));
        *stack_top = (unsigned long)pthread_exit;  // Return address is pthread_exit

        ((unsigned long *)tcb[thread_count].context)[JB_RSP] = ptr_mangle((unsigned long)stack_top);  // RSP
        ((unsigned long *)tcb[thread_count].context)[JB_PC] = ptr_mangle((unsigned long)start_thunk);  // RIP (start_thunk)

        // R12 holds start_routine, and R13 holds arg for start_thunk
        ((unsigned long *)tcb[thread_count].context)[JB_R12] = (unsigned long)start_routine;  // R12
        ((unsigned long *)tcb[thread_count].context)[JB_R13] = (unsigned long)arg;  // R13

        thread_count++;
    }

    // Schedule the newly created thread
    schedule(0);

    return 0;
}

void initialize_scheduler() {
    // Set up the 50ms timer for scheduling
    signal(SIGALRM, schedule);
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 50000;  // 50ms
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 50000;
    setitimer(ITIMER_REAL, &timer, NULL);
}

__attribute__((constructor)) void init() {
    tcb[0].id = 0;
    tcb[0].state = RUNNING;
    initialize_scheduler();
}
