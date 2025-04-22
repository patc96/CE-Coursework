Description:

This project implements a Thread Local Storage (TLS) library in C, providing protected memory regions for threads. The library allows threads to create, read, write, destroy, and clone TLS areas safely, leveraging features such as memory protection and Copy-on-Write (CoW) semantics to ensure secure and efficient management of thread-local storage. The implementation is built on top of user-space threading and uses system calls like mmap, mprotect, and sigaction to provide robust memory management.



Process:

1) The tls_create() function initializes a TLS area for the calling thread by allocating memory in page-sized chunks using mmap. The size is rounded up to the nearest multiple of the system's page size to ensure proper alignment. Each allocated page is initialized with PROT_NONE permissions to prevent unauthorized access. A thread-specific structure stores metadata such as the size, number of pages, and pointers to the allocated pages. The TLS structure is added to a global hash table for thread-specific lookup. If the thread already has a TLS or the size is invalid, the function returns an error.

2) The tls_read() function reads data from the calling thread's TLS into the provided buffer. It first verifies that the offset and length are within the bounds of the TLS size. All relevant pages are temporarily unprotected using mprotect, and the requested data is read in chunks, accounting for page boundaries. After reading, the pages are reprotected to maintain security. If the bounds check fails or the thread has no TLS, the function returns an error.

3) The tls_write() function writes data from the buffer into the calling thread's TLS, starting at the specified offset. Like tls_read, it first verifies bounds to ensure the write does not exceed the TLS size. It also implements Copy-on-Write (CoW) semantics: if the page being written to is shared with other threads, a private copy of the page is created before writing. The function temporarily unprotects the pages, writes the data, and then reprotects the pages. Proper handling of CoW ensures shared pages remain consistent and safe across threads.

4) The tls_destroy() function releases the TLS for the calling thread. It iterates through the pages of the TLS and decrements their reference counts. If a page is no longer shared (reference count reaches zero), it is unmapped using munmap. The TLS structure is removed from the global hash table, and its resources are freed. This ensures clean and efficient memory management, preventing leaks or dangling references. If the thread has no TLS, the function returns an error.

5) The tls_clone() function allows the calling thread to clone the TLS of another thread (target_tid). It creates a new TLS structure for the calling thread, sharing the pages of the target thread's TLS. The reference counts of the shared pages are incremented to reflect the new ownership. The cloned TLS remains efficient because data is not copied initially; CoW is used to create private copies only when the cloned TLS is modified. The new TLS is added to the global hash table for the calling thread, ensuring proper management.

6) Memory protection is achieved using mprotect. All TLS pages are initially protected against access (PROT_NONE). During tls_read and tls_write operations, the pages being accessed are temporarily unprotected (PROT_READ | PROT_WRITE), allowing safe access. Once the operation completes, the pages are reprotected to prevent accidental modification by other threads. If a thread attempts to access TLS memory directly, a segmentation fault (SIGSEGV) is triggered, and the signal handler terminates the offending thread.


Problems:

1) Initially, tls_read and tls_write had no checks to ensure that the requested offset and length were within the bounds of the TLS size. This led to potential buffer overflows and segmentation faults. To address this, bounds verification was added to both functions, ensuring that offset + length does not exceed the allocated size of the TLS. If the check fails, the function returns an error, preventing unsafe memory access.

2) Initially, shared pages were sometimes prematurely unmapped, leading to faults when other threads attempted to access them. This was resolved by properly incrementing reference counts for shared pages during tls_clone and ensuring a new private copy is created only when a write operation occurs on a shared page. The reference counts are also decremented and validated during tls_destroy to avoid accidental unmapping.

3) Initially, the signal handler for SIGSEGV could not reliably differentiate between genuine segmentation faults and TLS access violations. This caused unintended terminations of the entire process. The solution involved inspecting the faulting address (siginfo_t.si_addr) and comparing it with known TLS pages in the global hash table. If the fault was due to unauthorized TLS access, the offending thread was terminated using pthread_exit, leaving other threads unaffected. Non-TLS faults now trigger the default segmentation fault behavior.

4) During tls_destroy, shared pages were not always handled correctly, leading to memory leaks or dangling references. Pages shared between multiple threads need to remain accessible until all threads release them. This was resolved by decrementing the reference count for each page during tls_destroy and only unmapping pages when their reference count reaches zero. This ensures shared resources are cleaned up safely without impacting other threads.

5) Sometimes, tls_clone would incorrectly assume that pages were independent, leading to faults when shared pages were accessed by the cloned thread. This issue was resolved by carefully updating the reference count for each shared page and ensuring that the cloned TLS structure correctly referenced the original pages. This robust handling of shared memory prevents accidental faults and maintains consistency across threads.