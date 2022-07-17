# Semaphore / Readers-writerLock
## Semaphore
Wikipedia explains it this way:
> In computer science, a semaphore is a variable or abstract data type used to control access to a common resource by multiple threads and avoid critical section problems in a concurrent system such as a multitasking operating system. Semaphores are a type of synchronization primitives. A trivial semaphore is a plain variable that is changed (for example, incremented or decremented, or toggled) depending on programmer-defined conditions.

With it, some threads can enter into the critical section simultaneously. Of course, if its first value is 1, then just a thread can enter.

## Readers-writer Lock
Wikipedia explains it this way:
> In computer science, a readers–writer (single-writer lock, a multi-reader lock, a push lock, or an MRSW lock) is a synchronization primitive that solves one of the readers–writers problems. An RW lock allows concurrent access for read-only operations, write operations require exclusive access. This means that multiple threads can read the data in parallel but an exclusive lock is needed for writing or modifying data. When a writer is writing the data, all other writers or readers will be blocked until the writer is finished writing. A common use might be to control access to a data structure in memory that cannot be updated atomically and is invalid (and should not be read by another thread) until the update is complete.

With it, some threads can read in the critical section in parallel. But if a thread tries to write, then the thread should wait for the others or the others should wait for it.

# POSIX semaphore
## Data Structure
`sem_t`

This is the data structure of the semaphore. I wanted to find the linux-gnu version implementation of it in `semaphore.h`. But when I found it in `/usr/include/x86_64-linux-gnu/bits/semaphore.h`, the structure was like the below.
```c
typedef union
{
  char __size[__SIZEOF_SEM_T];
  long int __align;
} sem_t;
```
I couldn't understand about it. So I decided to understand it just as `sem_t` type.

## Initialization
`int sem_init(sem_t *sem, int pshared, unsigned int value);`

It initializes the unnamed semaphore at the address pointed to by `sem`. The `value` argument specifies the initial value for the semaphore. The `pshared` argument indicates whether this semaphore is to be shared between the threads of a process, or between processes.

## Wait
`int sem_wait(sem_t *sem);`

It decrements (locks) the semaphore pointed to by `sem`. If the semaphore's value is greater than zero, then the decrement proceeds, and the function returns, immediately.  If the semaphore currently has the value zero, then the call blocks until it becomes possible to perform the decrement.

## Post
`int sem_post(sem_t *sem);`

It increments (unlocks) the semaphore pointed to by `sem`. If the semaphore's value consequently becomes greater than zero, then another process or thread blocked in a sem_wait call will be woken up and proceed to lock the semaphore.

# POSIX readers-writer lock
## Data Structure
`pthread_rwlock_t`

This is the data structure of the readers-writer lock. I wanted to find the implementation of it in `pthread.h` like the semaphore. But when I found it in `/usr/include/x86_64-linux-gnu/bits/pthreadtypes.h`, the structure was like the below.
```c
typedef union
{
  struct __pthread_rwlock_arch_t __data;
  char __size[__SIZEOF_PTHREAD_RWLOCK_T];
  long int __align;
} pthread_rwlock_t;
```
I couldn't understand about it too. But I focused on the `__pthread_rwlock_arch_t`. And I looked up more about it. Then I could find it at `/usr/include/x86_64-linux-gnu/bits/pthreadtypes-arch.h`.
```c
struct __pthread_rwlock_arch_t
{
  unsigned int __readers;
  unsigned int __writers;
  unsigned int __wrphase_futex;
  unsigned int __writers_futex;
  unsigned int __pad3;
  unsigned int __pad4;
#ifdef __x86_64__
  int __cur_writer;
  int __shared;
  signed char __rwelision;
# ifdef  __ILP32__
  unsigned char __pad1[3];
#  define __PTHREAD_RWLOCK_ELISION_EXTRA 0, { 0, 0, 0 }
# else
  unsigned char __pad1[7];
#  define __PTHREAD_RWLOCK_ELISION_EXTRA 0, { 0, 0, 0, 0, 0, 0, 0 }
# endif
  unsigned long int __pad2;
  /* FLAGS must stay at this position in the structure to maintain
     binary compatibility.  */
  unsigned int __flags;
# define __PTHREAD_RWLOCK_INT_FLAGS_SHARED  1
#else
  /* FLAGS must stay at this position in the structure to maintain
     binary compatibility.  */
  unsigned char __flags;
  unsigned char __shared;
  signed char __rwelision;
# define __PTHREAD_RWLOCK_ELISION_EXTRA 0
  unsigned char __pad2;
  int __cur_writer;
#endif
};
```
It has `readers`, `writers`, and `futex`. They probably use futex systemcall, and check the number of readers and writers. I've only been looking into this so far and haven't looked into it any further.

## Initialization
`int pthread_rwlock_init(pthread_rwlock_t* lock, const pthread_rwlockattr_t attr);`

It is used to initialize a read/write lock, with attributes specified by `attr`.  If `attr` is `NULL`, the default read/write lock attributes are used.

## Read Lock
`int pthread_rwlock_rdlock(pthread_rwlock_t* lock);`

It acquires a read lock on lock provided that lock is not presently held for writing and no writer threads are presently blocked on the lock. If the read lock cannot be immediately acquired, the calling thread blocks until it can acquire the lock.

## Write Lock
`int pthread_rwlock_wrlock(pthread_rwlock_t* lock);`

It blocks until a write lock can be acquired against lock.

## Unlock
`int pthread_rwlock_unlock(pthread_rwlock_t* lock);`

It is used to release the read/write lock previously obtained by the lock functions.