Description:

This project implements a user-mode threading library in C that provides basic threading functionality, including thread creation, termination, joining, and semaphore-based synchronization. The library allows multiple threads to run concurrently within a single process using cooperative round-robin scheduling. A 50ms timer enables preemptive scheduling, ensuring fair CPU time distribution among threads.



Process:

1) The pthread_create() function creates new threads by allocating memory for the thread’s stack, setting up the thread’s context with setjmp(), and storing the start routine and arguments. The stack is initialized with the return address set to pthread_exit_wrapper, which ensures the thread exits cleanly after completing its start routine. The thread's program counter is initialized to start_thunk, which prepares the thread for execution. Each newly created thread is marked as READY and added to the Thread Control Block (TCB) array for scheduling.

2) The schedule() function implements round-robin scheduling, ensuring that each thread receives a fair share of CPU time. The scheduler is triggered every 50ms by a SIGALRM signal, which saves the context of the currently running thread using setjmp() and selects the next READY thread. The context of the next thread is restored using longjmp(), allowing it to resume execution from where it left off. Threads that are EXITED or BLOCKED are skipped in the rotation until they change states.

3) The pthread_exit() function terminates the current thread, marking its state as EXITED and storing its return value in the TCB. It unblocks any threads waiting on this thread (e.g., via pthread_join). If all threads have exited, the process terminates. Otherwise, the scheduler continues with the remaining threads.

4) The pthread_join() function allows a thread to wait for another thread to finish execution. If the target thread is still running, the calling thread is marked BLOCKED and control is transferred to another thread. When the target thread exits, its return value is retrieved and provided to the caller.

5) The pthread_self() function returns the thread ID of the currently running thread. The scheduler keeps track of the active thread through a current_thread variable, which is updated each time a new thread is scheduled.

6) The sem_init() function initializes a semaphore with the specified initial value. A new custom_semaphore structure is created, storing the semaphore value, a queue of threads waiting on the semaphore, and a flag indicating initialization. This structure is stored in an array, and an identifier is set in sem so the semaphore can be referenced by its index.

7) The sem_wait() function decrements the semaphore if its value is greater than zero, allowing the calling thread to proceed. If the semaphore value is zero, the calling thread is added to the semaphore’s waiting queue and marked as BLOCKED. The scheduler then yields control to another thread. When the semaphore is available again, the thread will be unblocked.

8) The sem_post() function increments the semaphore’s value. If there are threads waiting on the semaphore, the first thread in the queue is unblocked and removed from the waiting list, giving it access to the semaphore.

9) The sem_destroy() function cleans up the resources associated with a semaphore, freeing its memory and setting its initialization flag to indicate it is no longer valid. Any threads still waiting on the semaphore are not handled here, so sem_destroy should only be called when the semaphore is no longer in use.



Problems:

1) An issue arose where threads’ return values were not correctly captured, especially when different types (e.g., integers, strings) were returned. To solve this, pthread_exit was modified to store the exit value in the TCB’s exit_value field, which is accessed by pthread_join. The pthread_exit_wrapper was introduced to capture the return value in a register and pass it directly to pthread_exit, allowing consistent and accurate retrieval of the thread's return value.