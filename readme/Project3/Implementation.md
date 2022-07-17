
# Introduction
I have referenced the class resource (*Concurrency-1-revised.pdf*) for semaphore implementation. In this resource, *chapter 28* helped me a lot. This semaphore implementation is very similar to what I learned in this class. And it has several key points.

1. Mutual exclusion
2. Fairness
3. Performance
4. Futex systemcall
5. Two-phase lock

These are all things I learned in class. These will be described in more detail below.
 
# Basic Semaphore
## Data Structure
### Prototype
`xem_t`
### Definition
```c
typedef struct {
  int guard;
  int count;
  queue_t q;
} xem_t;
```
### Explanation
`xem` has 3 field.

1. guard

`guard` is a minimal spinlock. If it is 1, any thread can't enter the critical section before it is set to 0. This semaphore uses an atomic instruction with it.

2. count

This is the value of the semaphore. Threads as much as `count` can enter the critical section. If it is set to 1 initially, only 1 thread can enter the area.

3. queue

This is used for the fairness. Threads wait in the order in this queue. The first thread which waits for the semaphore can wake up firstly with it. And the element of this queue is the thread id. With this thread id, a thread sleeps or wakes up.

## Initialization
### Prototype
`int xem_init(xem_t *semaphore);`
### Definition
```
xem_init(xem_t *sema):
  sema->guard = 0
  sema->count = 1
  queue_init(&sema->q)
```
### Explanation
The guard is set to 0. And the initial value of the count is 1. And the queue is initialized.

## Wait
### Prototype
`int xem_wait(xem_t *semaphore);`
### Definition
```
xem_wait(xem_t *sema):
  int timer = 0
  thread_t *addr
  thread_t val

  while(test_and_set(&sema->guard, 1) == 1):
    if(timer++ < SLEEPTIME):
      timer = 0
      sleep(1)
  sema->count -= 1
  if(sema->count >= 0):
    sema->guard = 0
  else:
    val = gettid()
    addr = queue_add(&sema->q, val)
    sema->guard = 0
    futex_wait(addr, val)
```
### Explanation
This function is protected with the guard. To protect, it uses the atomic instruction: `test_and_set`. But the naive loop with the atomic instruction can just burn the cpu time. So it has 2 phase. First the guard lock spins for a while. If the lock is not acquired during the first spin phase, a second phase is entered. Second phase is the yield phase. Because threads are scheduled by the round-robin policy, the thread which enters the second phase can pass the cpu occupancy to another thread by the yield. But it calls sleep systemcall instead of yield systemcall. Because the yield uses up a time slice to prevent gaming MLFQ scheduler, it calls sleep instead of yield not to use up the time slice.

Next, the count decreases by 1. Then if the count is 0 or more, the thread which tries to acquire the semaphore can enter the area. Otherwise if it is less than 0, the thread has to wait in the queue. The queue guarantees the order, so the thread which came first can wake up first and enter the area.

The important point is the last line: `futex_wait`. If the guard is set to 0, other threads can enter the area which is protected by the guard. So a problem can occur after this point. A thread can wake B thread up in the queue before B thread really sleeps. In this case, B thread can sleep forever. So this function uses `futex_wait` systemcall. This systemcall puts the thread to sleep if the value to be pointed by `addr` equals to `val`. Otherwise it doesn't do anything. So with it, the threads try to acquire the semaphore can wait atomically.

## Post
### Prototype
`int xem_unlock(xem_t *semaphore);`
### Definition
```
xem_unlock(xem_t *sema):
  int timer = 0
  while(test_and_set(&sema->guard, 1) == 1):
    if(timer++ < SLEEPTIME):
      timer = 0
      sleep(1)
  sema->count += 1
  if(!queue_empty(&sema->q)):
    futex_wake(queue_head(&sema->q))
    queue_remove(&sema->q)
  sema->guard = 0
```
### Explanation
The mechanism to protect `xem_unlock` with the guard spinlock is identical to `xem_wait`.
But the count increases by 1 in this function different from the wait function which decreases the count by 1. And it checks the queue. If there is any thread, it wakes the thread up. When waking up, it uses `futex_wake` systemcall. This systemcall wakes one thread that is waiting on the queue. After waking up, it pops the thread.

## Evaluation
### Correctness: yes
With this semaphore, only 1 thread can enter the critical section. It guarantees the mutual exclusion.
### Fairness: yes
The threads wait in the order in which they entered the queue. So the first thread enters the area firstly. So the fairness is good.
### Performance (Acquire & Release): good
To improve the guard spinlock, it uses the 2 phases lock. And after enter the area which is protected by the guard, each threads pass or wait (sleep). So the performance of acquiring and releasing lock is quite good.
### Performance (Scale): bad
Because only 1 thread can enter the critical section, the others have to wait until the thread releases the lock. It slows down the scaling performance.

# Readers-writerLock
## Data Structure
### Prototype
`rwlock_t`
### Definition
```c
typedef struct {
  xem_t lock;
  xem_t writelock;
  int readers;
} rwlock_t;
```
### Explanation
The readers-writer lock has 2 locks and the number of readers. The first lock is used to protect each rwlock functions. And the second lock and readers are used to protect the critical section according to the readers-writer lock mechanism.

## Initialization
### Prototype
`int rwlock_init(rwlock_t *rwlock);`
### Definition
```
rwlock_init(rwlock_t *rwlock):
 xem_init(&rwlock->lock)
 xem_init(&rwlock->writelock)
 rwlock->readers = 0
```
## Read Lock
### Prototype
`int rwlock_acquire_readlock(rwlock_t *rwlock);`
### Definition
```
rwlock_acquire_readlock(rwlock_t *rwlock):
  xem_wait(&rwlock->lock)
  rwlock->readers += 1
  if(rwlock->readers == 1):
    xem_wait(&rwlock->writelock)
  xem_unlock(&rwlock->lock)
```
### Explanation
With a semaphore lock, this function is protected. And the first reader acquires the writer lock. Then if a writer tries to write during any reader exists, the writer has to wait.

## Write Lock
### Prototype
`int rwlock_acquire_writelock(rwlock_t *rwlock);`
### Definition
```
rwlock_acquire_writelock(rwlock_t *rwlock):
  xem_wait(&rwlock->writelock)
```
## Read Unlock
### Prototype
`int rwlock_release_readlock(rwlock_t *rwlock);`
### Definition
```
rwlock_release_readlock(rwlock_t *rwlock):
  xem_wait(&rwlock->lock)
  rwlock->readers -= 1
  if(rwlock->readers == 0):
    xem_unlock(&rwlock->writelock)
  xem_unlock(&rwlock->lock)
```
### Explanation
With a semaphore lock, this function is protected. And the last reader releases the writer lock.

## Write Unlock
### Prototype
`int rwlock_release_writelock(rwlock_t *rwlock);`
### Definition
```
rwlock_release_writelock(rwlock_t *rwlock):
  xem_unlock(&rwlock->writelock)
```

## Evaluation
### Correctness: yes
Readers don't modify anything. Only writers modify something. So lots of readers can enter the area simultaneously. But only 1 writer can enter the area. This mechanism guarantees the correctness.
### Fairness: no
It would be relatively easy for reader to starve writer. With this rwlock, a writer has to wait for all readers to end. So it isn't fair to writers.
### Performance (Scale): good
Readers can enter the area in parallel. So it makes the readers scalable. So it is quite better than the basic semaphore in terms of reading.

# Troubleshooting
## Writer Starvation
### Explanation
RW locks can be designed with different priority policies for reader vs. writer access. The lock can either be designed to always give priority to readers (read-preferring), to always give priority to writers (write-preferring) or be unspecified with regards to priority. These policies lead to different tradeoffs with regards to concurrency and starvation.
### Design Choice
Write-preferring rwlock isn't always better than read-preferring. And read-preferring isn't always better than write-preferring. So it is a choice.

In circumstances in which reads occur more often than writes, reading-preferring can be better. But the write starvation can occur.

However, if the writing is important, write-preferring can be better choice. But the performance of reading can be low.

### My design
Read-preferring rwlocks allow for maximum concurrency and can lead to write-starvation if contention is high. I want to see notable difference between the semaphore and the rwlock. So I implemented this rwlock as read-preferring.

### How to solve
I didn't implement as write-preferring. But I just designed briefly. Rwlock algorithm can be divided by 2 phases.

1. Reading phase

If only readers existed, they read in parallel.

2. Writing phase

If a writer came, it waits for the existing readers. And new readers have to wait for the others like writers. This phase is just similar to the mutex. And if there isn't any writer in the critical section, this phase ends.

Difference with read-preferring rwlock is the new readers. At read-preferring rwlock, the new readers read in parallel and writers wait for the new readers too. However, at write-preferring rwlock, the writers don't wait for the new readers.

If the writers don't wait for the new readers, the rwlock can be implemented as a writer-preferring lock.

# Test and Performance
## Setup
```
Cloud Environment: Google Cloud Platform
Machine Type     : e2-medium (2 vCPU)
Virtualization   : kvm
Operating System : Ubuntu 18.04.6 LTS
Kernel           : Linux 5.4.0-1069-gcp
Architecture     : x86-64
QEMU             : version 2.11.1(Debian 1:2.11+dfsg-1ubuntu7.39)
```
## Test Program
### How to Boot
`make CPUS=1 qemu-nox` (in ubuntu)
### How to run
`test_rwlock` (in xv6)
### Variable
```
- REP: 50
- NTHREADS: 64
- READERS_RATIO: 0.9
```
### Result
```
1. Logging the lock acquisition of a RW lock
Reader Acquired 0
Reader Acquired 1
Reader Acquired 2
Reader Acquired 3
...
Reader Released 56
Reader Released 57
Writer Acquired 58
Writer Released 58
Writer Acquired 59
Writer Released 59
...
2. Lock Efficiency Test
	a. Using a simple binary semaphore
	b. Using a readers-writer lock

	Reader Elapsed Ticks 2-a) 3437 ticks	2-b) 399 ticks
	Writer Elapsed Ticks 2-a) 365 ticks	2-b) 141 ticks
```
### Analysis
#### 1. Lock acquisition test
- Readers run in parallel. -> ok!
- Only 1 writer runs in the critical section. -> ok!
#### 2. Efficiency Test
- Speed: rwlock > semaphore