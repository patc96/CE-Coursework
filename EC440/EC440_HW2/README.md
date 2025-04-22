Description:

This project implements a user-mode threading library in C that provides basic threading functionality such as thread creation, termination, and context switching using round-robin scheduling. The library allows multiple threads to run concurrently within the same process. Preemptive scheduling is achieved using a 50ms timer, ensuring that threads receive fair CPU time.



Process:

1) The pthread_create() function is responsible for creating new threads. It allocates memory for the thread's stack, initializes the thread's context using setjmp(), and stores the start routine and arguments.
The stack is set up with the return address pointing to pthread_exit(), ensuring that the thread exits cleanly once its start routine finishes. The thread's program counter is initialized to the start_thunk function, which prepares the thread for execution. Each newly created thread is marked as READY and added to the thread control block (TCB) array for scheduling.

2) The schedule() function uses a round-robin scheduling algorithm, where each thread gets a fair share of CPU time. The scheduler is triggered every 50ms via a SIGALRM signal that is set up to trigger every 50 milliseconds. When the signal handler is invoked, the context of the currently running thread is saved using setjmp(), and the next thread in the READY state is selected. The next thread's context is restored using longjmp(), allowing the thread to continue execution from where it left off.

3) The pthread_exit() function changes the current thread's state to EXITED and cleans up resources. If all threads have exited, the process terminates. Otherwise, the scheduler continues to run the remaining threads.

4) The pthread_self() function returns the thread ID of the currently running thread. Since only one thread can run at a time, this function allows the program to know which thread is currently active. The scheduler maintains a global variable current_thread, which gets updated every time a new thread is scheduled. This function simply returns the value of current_thread, enabling identification of the executing thread.



Problems: 

1) Initially, context switching between threads wasn't functioning correctly. The threads were not resuming from their saved state, and preemptive switching wasn't taking place as expected. This issue was resolved by using setjmp() and longjmp() to save and restore the CPU context. Also, preemption was implemented by using a 50ms SIGALRM timer with setitimer(), which triggers the scheduler to switch threads periodically without requiring threads to yield manually.

2) Initially, threads were overwriting each other’s stacks, which led to memory corruption and unpredictable behavior. The problem was resolved by allocating a separate stack for each thread and ensuring the stack pointer (RSP) was correctly initialized at the top of the allocated space. The return address of the stack was set to pthread_exit() to ensure proper thread termination. This allowed threads to have isolated memory spaces for their execution.

3) Initially, The program would crash or behave unexpectedly when creating many threads, particularly when the number of threads exceeded a certain limit. I resolved this by implementing a maximum limit (MAX_THREADS) to ensure that memory management is more efficient by preventing the overallocation of stacks.