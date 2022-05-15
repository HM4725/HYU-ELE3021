#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "list.h"
#include "proc.h"
#include "spinlock.h"
#include "scheduler.h"

struct ptable ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void pushheap(struct proc*);
static void enqueue_proc(struct proc*, int);
static void dequeue_proc(struct proc*);
static int getminpass(void);

// set_cpu_share is used in sys_set_cpu_share
int
set_cpu_share(int share)
{
  struct proc *p;
  int remain;
  int minpass, mlfqpass;

  if(share < 1 || share > 100 - RESERVE)
    return -1;

  acquire(&ptable.lock);
  p = myproc();
  remain = ptable.mlfq.tickets;
  if(p->type == STRIDE)
    remain += p->tickets;
  if(remain - share >= RESERVE){
    if(p->type == MLFQ){
      dequeue_proc(p);
      minpass = getminpass();
      mlfqpass = ptable.mlfq.pass;
      p->pass = minpass < mlfqpass ? minpass : mlfqpass;
      p->type = STRIDE;
      list_add(&p->run, &ptable.stride.run);
    }
    ptable.mlfq.tickets = remain - share;
    p->tickets = share;
    release(&ptable.lock);
    return 0;
  } else {
    release(&ptable.lock);
    return -1;
  }
}

// getminpass is a function which returns
// the minimum pass value of stride heap.
static int
getminpass(void)
{
  return ptable.stride.size > 0 ?
    ptable.stride.minheap[1]->pass : MAXINT;
}

// pushheap is used for the stride scheduler.
// This heap is a min-heap for the pass of process.
// RUNNABLE, SLEEPING states are managed in it.
static void
pushheap(struct proc *p)
{
  int i = ++ptable.stride.size;
  struct proc **minheap = ptable.stride.minheap;

  while(i != 1 && p->pass < minheap[i/2]->pass){
    minheap[i] = minheap[i/2];
    i /= 2;
  }
  minheap[i] = p;
}

// pushheap is used for the stride scheduler.
static struct proc*
popheap()
{
  int parent, child;
  struct proc **minheap = ptable.stride.minheap;
  struct proc *min = minheap[1];
  struct proc *last = minheap[ptable.stride.size--];

  for(parent=1, child=2; child <= ptable.stride.size; parent=child, child*=2){
    if(child < ptable.stride.size && minheap[child]->pass > minheap[child+1]->pass) child++;
    if(last->pass <= minheap[child]->pass) break;
    minheap[parent] = minheap[child];
  }
  minheap[parent] = last;

  return min;
}

// enqueue pushes proc to MLFQ.
// In MLFQ, proc states are as following:
//   RUNNING, RUNNABLE
struct proc*
enqueue_thread(struct proc* th, void *level)
{
  struct list_head *queue;
  queue = &ptable.mlfq.queue[(int)level];
  if(th->state == RUNNABLE || th->state == RUNNING){
    list_add_tail(&th->mlfq, queue);
  }
  return 0;
}
static void
enqueue_proc(struct proc *p, int level)
{
  threads_apply1(p, enqueue_thread, (void*)level);
}

// concatqueue is used in MLFQ queues.
// It is a only way to move the process upward,
// and called when priority boost.
static void
concatqueue(int src, int dst)
{
  struct list_head *srcq = &ptable.mlfq.queue[src];
  struct list_head *dstq = &ptable.mlfq.queue[dst];
  struct list_head **spin = &ptable.mlfq.pin[src];
  struct list_head **dpin = &ptable.mlfq.pin[dst];

  if(list_empty(dstq) && *spin != srcq)
    *dpin = *spin;
  *spin = srcq;

  list_bulk_move_tail(srcq, dstq);
}

// dequeue pops proc from MLFQ.
// In MLFQ, proc states are as following:
//   RUNNING, RUNNABLE
struct proc*
dequeue_thread(struct proc *th)
{
  struct list_head **ppin;
  struct list_head *mlfq;
  int level;

  if(th->state == RUNNABLE || th->state == RUNNING){
    level = th->thmain->privlevel;
    ppin = &ptable.mlfq.pin[level];
    mlfq = &th->mlfq;
    if(*ppin == mlfq)
      *ppin = mlfq->next;
    list_del(mlfq);
  }
  return 0;
}
static void
dequeue_proc(struct proc *p)
{
  threads_apply0(p, dequeue_thread); 
}

void
pinit(void)
{
  struct proc *p;
  int i;

  initlock(&ptable.lock, "ptable");

  for(i = 0; i < QSIZE; i++){
    list_head_init(&ptable.mlfq.queue[i]);
    ptable.mlfq.pin[i] = &ptable.mlfq.queue[i];
  }
  list_head_init(&ptable.stride.run);
  list_head_init(&ptable.sleep);
  list_head_init(&ptable.free);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    list_add_tail(&p->free, &ptable.free);

  ptable.mlfq.tickets = 100;
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  if(!list_empty(&ptable.free)){
    p = list_first_entry(&ptable.free, struct proc, free);
    list_del(&p->free);
    goto found;
  }

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  list_head_init(&p->children);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    list_add(&p->free, &ptable.free);
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  list_head_init(&p->thgroup);
  p->thmain = p;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;
  enqueue_proc(p, p->privlevel);

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  sz = curproc->thmain->sz;
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
  curproc->thmain->sz = sz;
  switchuvm(curproc);
  release(&ptable.lock);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz,
                          curproc->ustack)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    list_add(&np->free, &ptable.free);
    return -1;
  }
  np->ustack = curproc->ustack;
  np->sz = curproc->sz;
  np->parent = curproc;
  list_add_tail(&np->sibling, &curproc->children);
  np->thmain = np;
  list_head_init(&np->thgroup);

  *np->tf = *curproc->tf;
  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  np->type = MLFQ;

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;
  enqueue_proc(np, np->thmain->privlevel);

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *p, *curproc = myproc();
  struct list_head *children, *itr;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  children = &curproc->children;
  for(itr = children->next; itr != children; itr = itr->next){
    p = list_entry(itr, struct proc, sibling);
    p->parent = initproc;
    if(p->state == ZOMBIE)
      wakeup1(initproc);
  }
  list_bulk_move_tail(&curproc->children,
                      &initproc->children);

  // Jump into the scheduler, never to return.
  if(curproc->type == MLFQ){
    dequeue_proc(curproc);
  } else { // STRIDE
    ptable.mlfq.tickets += curproc->tickets;
    list_del(&curproc->run);
  }
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

void
freeproc(struct proc *p)
{
  kfree(p->kstack);
  p->kstack = 0;
  freevm(p->pgdir);
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->killed = 0;
  p->tickets = 0;
  p->pass = 0;
  p->ticks = 0;
  p->privlevel = 0;
  p->state = UNUSED;
  list_add(&p->free, &ptable.free);
}
// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  struct list_head *children, *itr;
  int pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    children = &curproc->children;
    for(itr = children->next; itr != children; itr = itr->next){
      p = list_entry(itr, struct proc, sibling);
      if(p->tid == 0 && p->state == ZOMBIE){
        pid = p->pid;
        list_del(itr);
        freeproc(p);
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(list_empty(children) || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

struct proc*
next_proc(struct proc *p)
{
  struct proc *np;
  struct list_head *q, *start, *itr;
  int level = p->thmain->privlevel;

  q = &ptable.mlfq.queue[level];
  start = &p->mlfq;
  for(itr = start->next; itr != start; itr = itr->next){
    if(!list_is_head(itr, q)){
      np = list_entry(itr, struct proc, mlfq);
      if(np->pid != p->pid && np->state == RUNNABLE)
        return np;
    }
  }
  return 0;
}

struct proc*
mlfqselect(){
  struct proc *p, *np;
  struct list_head *q;
  struct list_head **ppin;
  int l;

  for(l = 0; l < QSIZE; l++){
    q = &ptable.mlfq.queue[l];
    if(!list_empty(q)){
      ppin = &ptable.mlfq.pin[l];
      if(*ppin == q){
        p = list_first_entry(q, struct proc, mlfq);
      } else {
        p = list_entry(*ppin, struct proc, mlfq);
      }
      np = next_proc(p);
      *ppin = np != 0 ? &np->mlfq : q;
      return p;
    }
  }

  return 0;
}

void
mlfqlogic(struct proc *p){
  struct proc *np;
  struct proc *thmain = p->thmain;
  struct list_head **ppin;
  struct list_head *q;
  struct list_head *itr;
  struct proc *pitr;
  int l, baselevel = QSIZE-1;

  l = thmain->privlevel;
  if(l < baselevel && thmain->ticks >= TA(l)){
    dequeue_proc(p);
    thmain->privlevel = l+1;
    thmain->ticks = 0;
    enqueue_proc(p, l+1);
  } else {
    ppin = &ptable.mlfq.pin[l];
    q = &ptable.mlfq.queue[l];
    if(*ppin == q){
      np = next_thread(p);
      *ppin = np != 0 ? &np->mlfq : &ptable.mlfq.queue[l];
    }
  }
  // Priority boost
  if(ptable.mlfq.ticks >= BOOSTINTERVAL){
    // RUNNABLE, RUNNING
    for(l = 1; l <= baselevel; l++){
      q = &ptable.mlfq.queue[l];
      for(itr = q->next; itr != q; itr = itr->next){
        pitr = list_entry(itr, struct proc, mlfq);
        pitr->privlevel = 0;
        pitr->ticks = 0;
      }
      concatqueue(l, 0);
    }
    // SLEEPING
    q = &ptable.sleep;
    for(itr = q->next; itr != q; itr = itr->next){
      pitr = list_entry(itr, struct proc, sleep);
      pitr->privlevel = 0;
      pitr->ticks = 0;
    }
    ptable.mlfq.ticks = 0;
  }
}

void
stridelogic(struct proc *p){
  struct list_head *q;
  struct list_head *itr;
  struct proc *pitr;
  int minpass;
  int i;

  // Pass overflow handling
  minpass = p == 0 || p->type == MLFQ ?
    ptable.mlfq.pass : p->pass;
  if(minpass > BARRIER){
    for(i = 1; i <= ptable.stride.size; i++){
      ptable.stride.minheap[i]->pass -= minpass;
    }
    q = &ptable.stride.run;
    for(itr = q->next; itr != q; itr = itr->next){
      pitr = list_entry(itr, struct proc, run);
      pitr->pass -= minpass;
    }
    ptable.mlfq.pass -= minpass;
  }

  // Pass increases by stride
  if(p == 0 || p->type == MLFQ){
    ptable.mlfq.pass += STRD(ptable.mlfq.tickets);
  } else if(p->type == STRIDE){
    if(p->state == RUNNABLE || p->state == SLEEPING){
      p->pass += STRD(p->tickets);
      pushheap(p);
    }
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;

  for(;;){
    sti();

    acquire(&ptable.lock);

    // Select next process
    p = getminpass() < ptable.mlfq.pass ?
      popheap() : mlfqselect();

    // Run process
    if(p != 0 && p->state == RUNNABLE) {
      if(p->type == STRIDE)
        list_add(&p->run, &ptable.stride.run);

      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      // switchkvm();  same mapping - redundant?

      if(p->type == MLFQ){
        mlfqlogic(c->proc);
      }
      c->proc = 0;
    }

    // Log stride
    stridelogic(p);

    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();
  struct proc *thmain = p->thmain;
  struct proc *thnext = next_thread(p);

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;

  thmain->ticks++;
  if(thmain->type == MLFQ)
    ptable.mlfq.ticks++;
  if(thnext == 0 || thmain->ticks % DTQ == 0){
    swtch(&p->context, mycpu()->scheduler);
  } else {
    if(p != thnext){
    // TODO: Fix vswitch panic bug
      //vswitchuvm(thnext);
      switchuvm(thnext);
      mycpu()->proc = thnext;
      thnext->state = RUNNING;
      swtch(&p->context, thnext->context);
    } else {
      p->state = RUNNING;
    }
  }
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p;

  acquire(&ptable.lock);  //DOC: yieldlock
  p = myproc();
  if(p->type == STRIDE)
    list_del(&p->run);
  p->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  if(p->type == MLFQ){
    dequeue_thread(p);
  } else {
    list_del(&p->run);
  }
  p->state = SLEEPING;
  list_add(&p->sleep, &ptable.sleep);

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
void
wakeup1(void *chan)
{
  struct proc *p;
  struct list_head *q;
  struct list_head *itr;

  q = &ptable.sleep;
  itr = q->next;
  while(itr != q){
    p = list_entry(itr, struct proc, sleep);
    itr = itr->next;
    if(p->chan == chan){
      list_del(&p->sleep);
      p->state = RUNNABLE;
      if(p->type == MLFQ)
        enqueue_thread(p, (void*)p->thmain->privlevel);
    }
  }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING){
        list_del(&p->sleep);
        p->state = RUNNABLE;
        if(p->type == MLFQ)
          enqueue_thread(p, (void*)p->thmain->privlevel);
      }
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %d %d %s %s", p->pid, p->privlevel, p->tid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
