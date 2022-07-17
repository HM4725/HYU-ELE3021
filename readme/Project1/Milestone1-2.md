# Milestone1 (Step 2)

## 1) Assignment Summary
### What to do
Combine the stride scheduling algorithm with MLFQ.
### Requirements
```
• Make a system call (i.e. set_cpu_share) that requests a portion of CPU and 
  guarantees the calling process to be allocated that much of a CPU time.
• The total amount of stride processes are able to get at most 80% of the CPU time.
  Exception handling is required for exceeding requests.
• The rest of the CPU time (20%) should run for the MLFQ scheduling which is 
  the default scheduling policy in this project.
```
### Details
```
• If a process wants to get a certain amount of CPU share, then it should invoke 
  a new system call to request the desired amount of CPU share.
• When a process is newly created, it initially enters the MLFQ. The process will be
  managed by the stride scheduler only if the set_cpu_share() system call has been 
  invoked.
• The total sum of CPU share requested from the processes in the stride queue can not
  exceed 80% of the total CPU time. Exception handling needs to be properly implemented 
  to handle oversubscribed requests.
• Do not allocate CPU share if the request induces surpass of the CPU share limit.
```
### Required system call
```
• set_cpu_share: inquires to obtain a cpu share(%).
  – int sys_set_cpu_share(void): wrapper
  – int set_cpu_share(int): system call
  – Return 0 if successful, otherwise a negative number.
```

## 2) My design
### MLFQ in Stride scheduler
Stride scheduler wraps MLFQ. The number of tickets of this stride scheduler is 100. Each process which obtained a cpu share has tickets. Like these processes, MLFQ has tickets too. But it has the remaining tickets. And the number of remaining tickets must be 20 or more. That is, MLFQ must have 20 or more tickets.

For example, process 1 has 10 tickets and process 2 has 15 tickets. Then MLFQ has 75 tickets. In this example, if process 3 arrived and requested 8 tickets, then MLFQ gives 8 tickets to process 3. Now MLFQ has 67 tickets. If MLFQ continues to give out tickets and the number reaches at 20, then it refuses to give out tickets.

![milestone1-step2-1](uploads/47addbd9b1bf076564a49bb27806b25c/milestone1-step2-1.png)

### Process structure
* MLFQ structure

MLFQ has tickets and pass value like another process in the stride scheduler.

```cpp
struct queue {
  struct proc *head;
  struct proc *tail;
}
struct mlfq {
  // stride part
  uint tickets;
  uint pass;
  // mlfq part
  uint ticks;
  struct queue queue[3];
}
```
The initial `tickets` of `mlfq` is `100`. After a process calls `set_cpu_share`, `mlfq` gives out the tickets to the process. If the sum of requests exceeds `80`, then it refuses giving out.

If it combined with the stride scheduler, it cannot use global `ticks`. So it needs own `ticks` for MLFQ scheduling.

* ptable structure

Main point of the stride scheduler is to pop the process which has minimum pass value.
So I chose `minheap` as the data structure. The time complexity of `get_minimum` is **O(1)**, and `push` & `pop` is **O(logN)**. In this `minheap`, there are only `RUNNABLE` state processes.

```cpp
struct minheap {
  uint size;
  struct proc* nodes[NPROC];
};
struct {
  struct spinlock lock;
  struct proc proc[NPROC];
  struct mlfq mlfq;
  struct minheap strideq;
} ptable;
```

* proc structure

```cpp
struct proc {
  uint sz;                     // Size of process memory (bytes)
  pde_t* pgdir;                // Page table
  char *kstack;                // Bottom of kernel stack for this process
  enum procstate state;        // Process state
  int pid;                     // Process ID
  struct proc *parent;         // Parent process
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
  // Added lines
  enum scheduletype type;
  // mlfq part
  uint ticks;
  uint privlevel;
  struct proc *prev;
  struct proc *next;
  // stride part
  uint tickets;
  uint pass;
};
```

### Process CRUD
I looked at the stride queue from the point of view of CRUD like [MLFQ](https://hconnect.hanyang.ac.kr/2022_ele3021_13024/2022_ele3021_2017030182/-/wikis/Milestone1-(step1)#process-crud).

**1. CREATE (push queue)**
```
p := {process to push}
p.type = STRIDETYPE
p.tickets = {cpu share}
mlfq.tickets -= {cpu share}
p.pass = 0
strideq.push(p)
```
* Time complexity: **O(logN)**

**2. READ (select next process)**
```
p := {just yielded process}
if p->type == STRIDETYPE:
  p->pass += STRIDE(p->tickets)
  strideq.push(p)
else if p->type == MLFQTYPE:
  mlfq.pass += STRIDE(mlfq.tickets)
np := strideq.minproc()
if p->pass > mlfq.pass:
  strideq.minpop()
  select np
else:
  select mlfq.next()
```
* Time complexity: **O(logN)**

**3. UPDATE (update cpu share)**
```
p := {running process}
mlfq.tickets += p->tickets
p->tickets = {new tickets}
mlfq.tickets -= p->tickets
```
* Time complexity: **O(1)**

**4. DELETE (pop queue)**
```
p := {process to run, sleep or delete}
strideq.pop(p)
```
* Time complexity: **O(logN)**

### Prevent monopolizing
To prevent the cpu monopoly of a new process, this scheduler sets the pass value of the new process to the minimum pass value.
```
p := {new process}
p.type = STRIDETYPE
p.tickets = {cpu share}
mlfq.tickets -= {cpu share}
p.pass = strideq.minproc()->pass
strideq.push(p)
``` 

### Prevent overflow
Each pass value increases by each stride.
```
pass += stride
```
If there is no limit, this 32-bit system can overflow with `pass`.

So this scheduler prevents it by setting `MAXPASS`.
Below is the algorithm.
```
p->pass += STRIDE(t->ticket)
if p->pass >= MAXPASS:
  min := min(stride[0].pass, mlfq.pass)
  for proc in stride:
    proc->pass -= min
  mlfq.pass -= min
```
The time complexity of this algorithm is O(N). But if we set `MAXPASS` as very huge number like $`2^{30}`$, it occurs few and far between.
