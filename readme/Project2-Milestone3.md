# Thread
>  A LWP(Light-weight Process) is a process that shares resources such as address space with other LWP(s), and allows multitasking to be done at the user level.

![thread](uploads/69708dff2571600345d3e3fc5b252c6b/thread.png)
## Thread Data Structure
I regarded the previous PCB(`struct proc`) as the TCB(Thread Control Block). If a new process created, then 1 TCB is created. Then the TCB is used to manage the process. All process has at lease 1 thread. And the first thread is called the main thread. And if a process has N threads, then there are N TCB of the process. My TCB is below:
```c
struct proc {
  uint sz;                     // Size of process memory (bytes)
  pde_t* pgdir;                // Page table
  char *kstack;                // Bottom of kernel stack for this process
  uint ustack;
  enum procstate state;        // Process state
  int pid;                     // Process ID
  struct proc *parent;         // Parent process
  struct list_head children;
  struct list_head sibling;
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
  enum schedtype type;
  // Stride fields
  int tickets;
  int pass;
  // MLFQ fields
  uint ticks;
  int privlevel;
  // General queue fields
  struct list_head sleep;
  struct list_head mlfq;
  struct list_head free;
  struct list_head run;
  // Thread
  thread_t tid;
  struct proc *thmain;
  struct list_head thgroup;
  void *retval;
  // Used for thread fork
  int logging;
};
```
I used [linked-list](#linux-kernel-linked-list) to manage the thread group.

And each TCB has fields: `ustack`, `tid`, `thmain`, `thgroup`. Each thread has own identifier(`tid`) and user stack(`ustack`). `tid` is the thread identifier and `ustack` is the base address of user stack. And `thmain` points to a thread which create itself. `tid` of `thmain` can be 0 or not. If 0, the main thread of the thread group. And if not 0, `thmain` is just a thread which calls `thread_create`.

![thread_group](uploads/995f2bb8ae8dd347fe3900e63b4629ae/thread_group.png)

## Thread Create
```
int thread_create(thread_t * thread, void * (*start_routine)(void *), void *arg);
@group      Thread
@brief      Create a thread
@note       The thread shares the memory space in the process.
@param[out] thread: thread identifier
@param[in]  start_routine: new thread starts from start_routine
@param[in]  arg: argument of start_routine
                 To pass multiple arguments,
                 send a pointer to a structure
@return     On success 0 and on error -1
```
`thread_create` has to observe 3 points.

### 1. Thread has own kernel stack and user stack.

#### Kernel stack

In `allocproc`, the new thread has own kernel stack.

#### User stack

To allocate a own user stack, I added a function to allocate a user stack. When this function is called, it allocates a user stack and a guard page below the stack.
```c
int
allocustack(pde_t *pgdir, uint ustack)
{
  uint sb;

  sb = PGROUNDDOWN(ustack);
  // allocate ustack
  if(allocuvm(pgdir, sb - PGSIZE, ustack + USTACKSIZE) == 0)
    return 0;
  // allocate guard page
  clearpteu(pgdir, (char*)(sb - PGSIZE));
  return ustack;
}
```

### 2. Thread starts from `start_routine`.

To guarantee it, I set the first user stack.
```c
allocustack(nth->pgdir, sp - USTACKSIZE)
nth->ustack = sp - USTACKSIZE;
sp -= 4;
*(uint *)sp = (uint)arg;
sp -= 4;
*(uint *)sp = MAGICEXIT;
...
nth->tf->esp = sp;
nth->tf->eip = (uint)start_routine;
```
According to the calling convention, this systemcall pushes argument and return address manually. And it set esp to the stack pointer and set eip to `start_routine`. So after return to user mode, it starts from the routine with the argument.

And the tricky point is `MAGICEXIT`. The address is over the user page. So if a thread forgets to invoke `thread_exit` at last, the segmentation fault will occur. Then OS traps the segfault. But this OS can be aware of this situation, and terminate the thread normally. Look at the following:
```c
void
trap(struct trapframe *tf)
{
...
  switch(tf->trapno){
  default:
    if(tf->eip == MAGICEXIT && myproc()->tid > 0)
      thread_exit((void*)tf->eax);
...
}
```

### 3. Threads share resources with other threads in the process.
Most of systemcalls refer to the main thread. So at most case, there is no need to set the fields of subthread. But the new threads copies some fields of the thread which called this systemcall.
```c
nth->pid = thmain->pid;
nth->pgdir = thmain->pgdir;
nth->parent = thmain->parent;
list_add_tail(&nth->sibling, &thmain->parent->children);
nth->type = thmain->type;
```
The key to share the address space is the page directory. The subthread copies the page directory shallowly, not deeply. Compared to `fork` which uses `copyuvm`, it just saves the value of the page directory. So now the subthread can use the same address space with other threads in the same process.

## Thread Exit
```
void thread_exit(void *retval);
@group      Thread
@brief      Terminate the thread
@note       Evenif a thread didn't call this function,
            it's ok. OS calls this function implicitly.
@param[in]  retval: retval is sent to the joining thread
```
This systemcall is similar to `exit`. But different point is that it returns a value. So before becoming a zombie, it saves the return value to own TCB field.
```c
curth->retval = retval;
wakeup1(curth->thmain);
curth->state = ZOMBIE;
```

## Thread Join
```
int thread_join(thread_t thread, void **retval);
 * @group      Thread
 * @brief      Wait for a thread which exited.
 * @param[in]  thread: identifier of the thread which
                       current thread is waiting for.
 * @param[out] retval: the pointer for a return value
 *                     which exited thread sent.
 * @return     On success 0 and on error -1
```
This systemcall is similar to `wait`. The current thread waits for a thread to exit.
After it exits, the current gets the return value of it and cleans up the resources of it.

## Exit
> When a LWP calls the exit system call, all LWPs are terminated and all resources used for each LWP must be cleaned up, and the kernel should be able to reuse it at a later point of time. Also, no LWP should survive for a long time after the exit system call is executed.

At first after this systemcall is called, the current thread sends kill signals to all threads. And if scheduled thread is a subthread, it just calls `thread_exit`. But if it is the main thread, then it waits for all subthreads to exit. After cleaning up all threads but the main thread, the main thread executes the original exit routine.
```c
if(curproc->killed == 0)
  terminate_proc(curproc);
if(curproc->tid == 0){
  curproc->killed = 0;
  while(!list_empty(&curproc->thgroup)){
    th = list_first_entry(&curproc->thgroup,
                          struct proc, thgroup);
    kprintf_warn("join orphan thread %d\n", th->tid);
    wakeup(th);
    thread_join(th->tid, (void**)&null);
  }
} else {
  thread_exit((void*)null);
}
```

## Kill
> If more than one LWP is killed, all LWPs must be terminated and the resources for each LWPs in that process must be cleaned up. After a kill for a LWP is called, no LWP in that group should survive for a long time.

When a thread calls exit, the thread sends kill signals to others. The exit systemcall uses the kill mechanism. So after implementing the exit, there is nothing to do with the kill systemcall.

## Exec
> If you call the exec system call, the resources of all LWPs are cleaned up so that the image of another program can be loaded and executed normally in one LWP. At this time, the process executed by the exec must be guaranteed to be executed as a general process thereafter.

At first after this systemcall is called, the current threads monopolizes the process. After monopolizing, the alone thread executes the original exec routine.
```c
int
monopolize_proc(struct proc *p)
{
  struct proc *th;
  int null = 0;

  acquire(&ptable.lock);
  if(p != p->thmain){
    wakeup1(p->thmain);
    __usurp_proc(p);
  }
  terminate_proc(p);
  p->killed = 0;
  release(&ptable.lock);

  while(!list_empty(&p->thgroup)){
    th = list_first_entry(&p->thgroup,
                          struct proc, thgroup);
    if(thread_join(th->tid, (void**)&null) < 0)
      return -1;
  }
  return 0;
}
```

## Sbrk
> When multiple LWPs simultaneously call the sbrk system call to extend the memory area, memory areas must not be allocated to overlap with each other, nor should they be allocated a space of a different size from the requested size. The expanded memory area must be shared among LWPs.

This systemcall uses `growproc`. In this function, it refers to `sz` of the thread. When a function wants a shared resource of a process, it refers to the field of the main thread.
```c
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();
  struct proc *thmain = main_thread(curproc);

  acquire(&ptable.lock);
  sz = thmain->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0){
      release(&ptable.lock);
      return -1;
    }
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0){
      release(&ptable.lock);
      return -1;
    }
  }
  thmain->sz = sz;
  invalidate_tlb(curproc);
  release(&ptable.lock);
  return 0;
}
```

## Pipe
> All LWPs must share a pipe, and reading or writing data should be syn- chronized and not be duplicated.

When a function wants a shared resource of a process, it refers to the field of the main thread. So like `sbrk`, pipe and file systemcalls refer to the main thread.

## Sleep
> When a specific LWP executes a sleep system call, only the requested LWP should be sleeping for the requested time. If a LWP is terminated, the sleep- ing LWP should also be terminated.

The original sleep changed the state of 1 PCB. Now this systemcall changes the state of 1 TCB. So there is nothing to modify.

## Corner case: Nested Thread
[`thmain`](#thread-data-structure) of a thread doesn't always points to the main thread. It points to a parent-thread which created the child-thread. This design made the nested thread possible.
### Join
When joining a exited thread, it waits for only child-thread.
```c
if(th->state == ZOMBIE && th->thmain == curth){
  *retval = th->retval;
  list_del(&th->thgroup);
  list_del(&th->sibling);
  __free_thread(th);
  release(&ptable.lock);
  return 0;
}
```
### Orphan Thread
Like fork and wait, if the parent-thread exits before the child-thread exits, there is a possibility for the child-thread to become a orphan thread. So like passing children to initproc in `exit`, the thread passes child-threads to the main thread when `thread_exit`.
```c
static struct proc*
__routine_handle_orphan_thread(struct proc* th, void* main)
{
  struct proc *thmain = (struct proc*)main;
  if(th->thmain == thmain)
    th->thmain = main_thread(thmain);
  return 0;
}
void
thread_exit(void *retval)
{
...
threads_apply1(curth, __routine_handle_orphan_thread, curth);
...
}
```

## Scheduler
### sched
When a thread arrives at `sched` from `yield`, `sleep` and `exit`, there can be 4 cases.
#### 1. Used up all time quantum
Stride time quantum is 5, and MLFQ time quantum is multiple of 5. So after 5 ticks, it has to pass the cpu occupancy to the scheduler.
#### 2. There isn't any runnable thread
If there is no runnable thread in the current process, it has to pass the cpu to the scheduler.
#### 3. There is a runnable thread: not itself
If there is a next thread in the process, it mimics the scheduler. It doesn't change the address space. It just modify esp of the task state (`vswitchuvm`). When a interrupt occurs in the user mode, cpu finds the kernel stack from the task state. And because each thread has own kernel stack, it has to modify the stack in the task state.
#### 4. There is a runnable thread: itself
If the next runnable thread is itself, just modify the state as running.
```c
thmain->ticks++;
if(thmain->type == MLFQ)
  ptable.mlfq.ticks++;
if(nxt == 0 || thmain->ticks % DTQ == 0){
  swtch(&p->context, mycpu()->scheduler);
} else {
  if(p != nxt){
    vswitchuvm(nxt);
    mycpu()->proc = nxt;
    nxt->state = RUNNING;
    swtch(&p->context, nxt->context);
  } else {
    p->state = RUNNING;
  }
}
```

### MLFQ
`enqueue` of project1 is to push a pcb to MLFQ. But the process of project2 can have many TCBs. So I decided to divide `enqueue` to `enqueue_thread` and `enqueue_proc`. First one is used to push a thread and the other is used to push all threads of a process. And `enqueue_proc` pushes only threads which is runnable or running because MLFQ of mine manages only runnable and running state processes.

And a thread is pushed behind another thread of which process is same. So it guarantees that threads of the same process grouped together. It makes easier for the scheduler to find next runnable process.
![thread_mlfq](uploads/92f5d8356560780343f4828f72b047d4/thread_mlfq.png)

And `dequeue` was divided to `dequeue_thread` and `dequeue_proc` like `enqueue`.

### Stride
Unlike MLFQ, only one thread of a process is pushed to the stride heap. If a runnable thread exists in a process, then the runnable thread is pushed to the heap. But if there is no runnable thread but a sleeping thread, then the sleeping thread is pushed to the heap, because the stride heap of mine manages runnable and sleeping state processes. Because only one thread is pushed to the stride heap, multiple cores cannot run the same process. The multi-core case is the shortcoming of my design. But it is ok because the original stride scheduler is hard to apply to the multi-core correctly too. 

## Fork
> When you call fork() on an LWP, you should copy the entire process. So if you have 10 LWPs within a process and call fork() on one of the LWPs, you should have a new process with 10 threads.

### Fork-All Model
![thread_fork_all](uploads/b3b07b4fb4d8c37d09ab0c815b595cb1/thread_fork_all.png)
### Algorithm
```
fork:
  cp = {current thread which calls fork}
  cmain = main_thread(cp)
// 1) Allocate threads
  // allocate main thread
  nmain = copy(cmain)
  nmain->pgdir = copyuvm(cmain)
  // allocate other threads
  for th in cmain->sub_threads:
    np = copy(th)
// 2) Fill kernel stacks
  for (nth, th) in (fork_proc, original_proc):
    if th->pid == cp->tid:
      *nth->tf = *th->tf
      nth->tf->eax = 0
      nth->state = RUNNABLE
    else:
      memmove(nth->kstack, th->kstack, KSTACKSIZE)
      align_variables(th->kstack)
```
### Fill kernel stacks
The critical problem of fork-all model is the kernel stack of non-fork-call threads. At upper case, thread0 and thread2 of process0 didn't invoke the fork systemcall. But they are forked too. Their kernel stack may be filled with some local variables including the trapframe and the context. If the forked thread0 of process1 uses the original variables of process0, then the later scheduled thread1 of process0 can use the corrupted variables by the forked thread. So there is need to align the kernel stack of the forked thread.

This os solved the problem by just finding variables which have the address value of previous thread then aligning it. It is like a sloppy aligning.
```c
// kstack & state
start = &curmain->thgroup;
itr1 = start;
itr2 = &np->thgroup;
nxt = 0;
do {
  th = list_entry(itr1, struct proc, thgroup);
  nth = list_entry(itr2, struct proc, thgroup);
  if(th == curproc){
    nxt = nth;
    *nth->tf = *th->tf;
    nth->tf->eax = 0;
    nth->state = RUNNABLE;
  } else {
    // Modify local variables on kstack
    int ip, *pip;
    delta = (int)nth->kstack - (int)th->kstack;
    memmove(nth->kstack, th->kstack, KSTACKSIZE);
    ip = (int)nth->kstack;
    for(; ip < (int)nth->kstack + KSTACKSIZE ;ip += 4){
      pip = (int*)ip;
      if(*pip > (int)th->kstack && *pip < (int)th->kstack + KSTACKSIZE){
        *pip += delta;
      }
    }
    delta = (int)th->context - (int)th->kstack;
    nth->context = (struct context*)(delta + (int)nth->kstack);
    delta = (int)th->context->ebp - (int)th->kstack;
    nth->context->ebp = delta + (uint)nth->kstack;
    delta = (int)th->tf - (int)th->kstack;
    nth->tf = (struct trapframe*)(delta + (int)nth->kstack);
    nth->state = th->state;
    if(nth->state == SLEEPING){
      nth->chan = th->chan == th ? nth : th->chan;
      list_add(&nth->sleep, &ptable.sleep);
    }
  }
  itr1 = itr1->next;
  itr2 = itr2->next;
} while(itr1 != start);
```
## Wait
> You should also be able to wait for the child process normally by the wait system call. Note the parent-child relationship between processes after the fork.

To keep the stable status within thread_create, thread_exit, thread_join, fork, exit, wait, I expanded parent-child rules.

### Thread Side (expand)
1. The parent-thread is responsible for releasing the child-sub-thread.
2. If the parent-thread exits before the child-sub-thread, pass the child-sub-thread to the main thread.
3. The main thread is responsible for releasing all subthreads.
### Process Side (original)
4. The parent process is responsible for releasing the child process.
5. If the parent process exits before the child process, pass the child process to initproc.
### Both Side (expand)
6. If the parent thread which invoked fork exits before the child process, pass the child process to the main thread.

Thanks to these expanded rules, when a main thread of a process became a zombie, the parent process can wait only for the main thread of the child process. And keep the process parent-child relationship normally.

# Design Pattern
## Linux Kernel Linked-List
I referred to [linux kernel linked-list](https://github.com/torvalds/linux/blob/master/include/linux/list.h).
```c
#define container_of(ptr, type, member) ({              \
  const typeof( ((type *)0)->member ) *__mptr = (ptr);  \
  (type *)( (char *)__mptr - offsetof(type,member) );})

#define offsetof(TYPE, MEMBER) ((size_t) & ((TYPE *)0)->MEMBER)

#define list_entry(ptr, type, member) \
  container_of(ptr, type, member)

struct list_head {
  struct list_head *prev, *next;
};
```
![linked-list](uploads/a68294bfa6dbf31906d384faf0ad1789/linked-list.png)

In this design, pcb and proc have several `list_head`. `list_head` helps coding efficiently. Just add it to a data structure. Then when you want to access the data structure, use `list_entry` macro. Because of this linked-list, I could group and manage data structures easily.
```c
struct proc {
  ...
  struct list_head children;
  struct list_head sibling;
  struct list_head sleep;
  struct list_head mlfq;
  struct list_head free;
  struct list_head run;
  struct list_head thgroup;
};
```
```c
struct mlfq {
  ...
  struct list_head queue[QSIZE];
};
struct ptable {
  ...
  struct list_head sleep;
  struct list_head free;
};
```
P.S. Because I used a portion of linux code, I added the GPL2 license.

## High Order Function
C language doesn't support high order function. But with function pointer, we can mimic it. And `list_head` with high order function has powerful effect. This os manages threads with linked-list. And it can apply a routine to all threads with high order function.
```c
typedef struct proc* (*callback0)(struct proc*);

struct proc*
threads_apply0(struct proc* p,
              callback0 routine)
{
  struct list_head *itr, *start;
  struct proc *th;

  start = p->thgroup.next;
  itr = start;
  do {
    th = list_entry(itr, struct proc, thgroup);
    itr = itr->next;
    if(routine(th) != 0)
      return th;
  } while(itr != start);
  return 0;
}
```
It apply 0 argument routine from next thread to current thread. `threads_apply0` applies a 0-argument routine and `threads_apply1` applies a 1-argument routine. And if the routine returns 0, then it continues to apply. Otherwise if the routine returns non-zero value, then it stops the iteration and returns the value. `threads_apply` can be used *when finding a certain thread*. And it can be used *when apply a method to all threads in a process*.

Following is the former case.
```c
static struct proc*
__routine_get_thread(struct proc* th, void* tid)
{
  return th->tid == (thread_t)tid ? th : 0;
}
struct proc*
get_thread(struct proc* p, thread_t thread)
{
  return threads_apply1(p, __routine_get_thread,
                        (void*)thread);
}
```

Next is the latter case.
```c
static struct proc*
__routine_kill_thread(struct proc* th)
{
  th->killed = 1;
  return 0;
}
void
terminate_proc(struct proc* p){
  threads_apply0(p, __routine_kill_thread);
}
```

# Test and Performance
## Setup
```
Cloud Environment: Google Cloud Platform
Machine Type     : e2-medium
Virtualization   : kvm
Operating System : Ubuntu 18.04.6 LTS
Kernel           : Linux 5.4.0-1069-gcp
Architecture     : x86-64
QEMU             : version 2.11.1(Debian 1:2.11+dfsg-1ubuntu7.39)
```
### How to boot
`make CPUS=1 qemu-nox` (in ubuntu)

## Test Thread
### How to run
`test_thread2` (in xv6)
### Purpose
Test how this OS runs threads safely.
### Result
0. racingtest
```
0. racingtest start
12612886
0. racingtest finish
```
1. basictest
```
1. basictest start
01234567891234567890123456789012345678901234567890
1. basictest finish
```
2. jointest1
```
2. jointest1 start
thread_join!!!
thread_exit...
thread_exit...
thread_exit...
thread_exit...
tthread_exit...
thread_exit...
thread_exit...
thread_exit...
threadhread_exit...
_exit...

2. jointest1 finish
```
3. jointest2
```
3. jointest2 start
thread_exit...
thread_exit...
thread_exit...
threadthread_exit...
thread_exit...
thread_exit...
threathread_exit...
thread_exit...
_exit...
d_exit...
thread_join!!!

3. jointest2 finish
```
4. stresstest
```
4. stresstest start
1000
2000
3000
4000
5000
6000
7000
8000
9000
10000
11000
12000
13000
14000
15000
16000
17000
18000
19000
20000
21000
22000
23000
24000
25000
26000
27000
28000
29000
30000
31000
32000
33000
34000
35000

4. stresstest finish
```
5. exittest1
```
5. exittest1 start
thread_exit ...thread_exit ...
thread_exit ...
thread_exit ...
thread_ethread_exit ...
thread_exit ...
thread_exit ...
thread_exttttttt
5. exittest1 finish
```
6. exittest2
```
6. exittest2 start
6. exittest2 finish
```
7. forktest
```
7. forktest start
parent
parent
child
panic at fork in forktest
child
child
child
panic at fork in forktest
panic at fork in forktest
7. forktest finish
zombie!
zombie!
zombie!
```
8. exectest
```
8. exectest start
echo is executed!
8. exectest finish
```
9. sbrktest
```
9. sbrktest start
9. sbrktest finish
```
10. killtest
```
10. killtest start
10. killtest finish
```
11. pipetest
```
11. pipetest start
11. pipetest finish
```
12. sleeptest
```
12. sleeptest start
12. sleeptest finish
```
13. stridetest
```
13. stridetest start
10% : 682149682
 2% : 145284129
13. stridetest finish
```
### After Finish
```
remain: 62
1 0 0 sleep  init 80105360 80105464 80106109 80107235 8010703c
2 0 0 sleep  sh 80105360 801002ca 8010113c 80106432 80106109 80107235 8010703c
```
This os can keep the normal state even after the stress thread test.

## Running Time
### How to run
`time usertests` (in xv6)
### Purpose
Check the total running time of a process.
### Result
```
ALL TESTS PASSED
Spent time: 44510ms
```
The result is similar with project1.

# Unresolved
## inode lock with Fork-all model
![fork-ilock](uploads/ab437aa8ea4fc9dcc618a9524cbd366a/fork-ilock.png)

Let's think the case that a thread slept during writing files and the other thread invoked fork systemcall. After thread0 of process0 wakes up, it will unlock the inode. But after thread0 of process1 wakes up, it will unlock the inode too. Actually `ilock` was called once, but `iunlock` will be called twice. At the point where it is called twice, the panic rarely occurs like below.
```
lapicid 0: panic: iunlock
 80101941 80100654 80101238 80106492 801060f9 8010721d 8010702c 0 0 0
```
I didn't learn the file system yet. So I remain this point as unresolved.