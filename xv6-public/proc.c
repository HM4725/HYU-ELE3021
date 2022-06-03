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
#include "debug.h"

struct ptable ptable;

static struct proc *initproc;

int nproc = NPROC;
int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void pushheap(struct proc*);
static void enqueue_proc(struct proc*);
static void dequeue_proc(struct proc*);
static int getminpass(void);

// [Thread routines]
static struct proc*
__routine_set_stride(struct proc *th)
{
  th->type = STRIDE;
  return 0;
}

static struct proc*
__routine_is_pinned(struct proc *th, void *pin)
{
  return &th->mlfq == (struct list_head*)pin ? th : 0;
}

static struct proc*
__routine_enqueue_remain(struct proc *th, void *p)
{
  struct proc *pivot = (struct proc *)p;
  if(th != pivot && (th->state == RUNNABLE || th->state == RUNNING))
    list_add_after(&th->mlfq, &pivot->mlfq);
  return 0;
}

static struct proc*
__routine_dequeue_proc(struct proc *th)
{
  if(th->state == RUNNABLE || th->state == RUNNING)
    list_del(&th->mlfq);
  return 0;
}

static struct proc*
ready_proc(struct proc *p)
{
  struct proc *np;
  struct list_head *q;
  struct list_head *start, *itr;
  int level = main_thread(p)->privlevel;

  if(p->state != RUNNABLE && p->state != RUNNING)
    panic("ready_proc");

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

/* Function: set_cpu_share
 * ------------------------
 * @group      Stride
 * @brief      Guarantee the fair share of cpu time to process
 *             according to the stride scheduling algorithm.
 * @note1      If the type of the current process was MLFQ,
 *             it changes the type to STRIDE.
 *             Otherwise if the type was STRIDE, then it just
 *             modifies the tickets of process.
 * @note2      The main thread manages pass and tickets.
 * @param[in]  share: the share of cpu time (unit: %)
 * @return     If it successes then 0 else -1
 */
int
set_cpu_share(int share)
{
  struct proc *p;
  struct proc *thmain;
  int remain;
  int minpass, mlfqpass;

  if(share < 1 || share > 100 - RESERVE)
    return -1;

  acquire(&ptable.lock);
  p = myproc();
  thmain = main_thread(p);
  remain = ptable.mlfq.tickets;
  if(p->type == STRIDE)
    remain += thmain->tickets;
  if(remain - share >= RESERVE){
    if(p->type == MLFQ){
      dequeue_proc(p);
      minpass = getminpass();
      mlfqpass = ptable.mlfq.pass;
      thmain->pass = minpass < mlfqpass ? minpass : mlfqpass;
      threads_apply0(thmain, __routine_set_stride);
      list_add(&p->run, &ptable.stride.run);
    }
    ptable.mlfq.tickets = remain - share;
    thmain->tickets = share;
    release(&ptable.lock);
    return 0;
  } else {
    release(&ptable.lock);
    return -1;
  }
}

/* Function: getminpass
 * ------------------------
 * @group      Stride
 * @brief      Get a minimum pass value of the stride heap.
 * @note       If there isn't any process in the stride heap,
 *             it returns a maximum value.
 * @return     Minimum pass value of the stride heap
 */
static int
getminpass(void)
{
  return ptable.stride.size > 0 ?
    main_thread(ptable.stride.minheap[1])->pass : MAXINT;
}

/* Function: pushheap
 * ------------------------
 * @group      Stride
 * @brief      Push a stride type process.
 * @note1      In stride heap, thread states are as following:
 *             RUNNABLE, SLEEPING
 * @note2      In a process, only one thread has to be in the heap.
 * @param[in]  p: stride type process
 */
static void
pushheap(struct proc *p)
{
  int i = ++ptable.stride.size;
  struct proc *thmain = main_thread(p);
  struct proc **minheap = ptable.stride.minheap;

  while(i != 1 && thmain->pass < main_thread(minheap[i/2])->pass){
    minheap[i] = minheap[i/2];
    i /= 2;
  }
  minheap[i] = p;
}

/* Function: popheap
 * ------------------------
 * @group      Stride
 * @brief      Pop a process which has a minimum pass value
 *             from the stride minheap
 * @note       When it is called, the size of heap must be more than 1
 * @return     Stride type process which has a minimum pass
 */
static struct proc*
popheap()
{
  int parent, child;
  struct proc **minheap = ptable.stride.minheap;
  struct proc *min = minheap[1];
  struct proc *last = minheap[ptable.stride.size--];

  for(parent=1, child=2; child <= ptable.stride.size; parent=child, child*=2){
    if(child < ptable.stride.size && 
       main_thread(minheap[child])->pass >
       main_thread(minheap[child+1])->pass)
      child++;
    if(main_thread(last)->pass <= main_thread(minheap[child])->pass)
      break;
    minheap[parent] = minheap[child];
  }
  minheap[parent] = last;

  return min;
}

/* Function: is_proc_pinned
 * ------------------------
 * @group      MLFQ
 * @brief      Check whether the process is pinned.
 * @param[in]  p: process
 * @param[in]  pin: pin
 * @return     If p is pinned then 1 else 0
 */
static int
is_proc_pinned(struct proc *p, struct list_head* pin)
{
  struct proc* th;
  th = threads_apply1(p, __routine_is_pinned, pin);
  return th != 0 ? 1 : 0;
}

/* Function: pin_next_thread
 * -------------------------
 * @group      MLFQ
 * @brief      Pin on next thread in MLFQ
 * @param[in]  th: current thread
 * @param[in]  self: if 1, include current thread
 *                   else (0), exclude current thread
 */
static void
pin_next_thread(struct proc *th, int self){
  struct proc *nxt;
  struct list_head **ppin;
  int level;

  level = main_thread(th)->privlevel;
  ppin = &ptable.mlfq.pin[level];
  if(is_proc_pinned(th, *ppin)){
    nxt = ready_thread(th);
    if(nxt != 0 && (self || nxt != th)){
      *ppin = &nxt->mlfq;
    } else {
      nxt = ready_or_running_thread(th);
      if(nxt == 0)
        panic("pin next thread");
      nxt = ready_proc(nxt);
      *ppin = nxt != 0 ? &nxt->mlfq : &ptable.mlfq.queue[level];
    }
  }
}

/* Function: pin_next_proc
 * -------------------------
 * @group      MLFQ
 * @brief      Pin on next process in MLFQ.
 * @param[in]  p: current process
 * @param[in]  self: if 1, include current process
 *                   else (0), exclude current process
 */
static void
pin_next_proc(struct proc *p, int self){
  struct proc *nxt;
  struct list_head **ppin;
  int level;

  level = main_thread(p)->privlevel;
  ppin = &ptable.mlfq.pin[level];
  if(is_proc_pinned(p, *ppin)){
    nxt = ready_or_running_thread(p);
    if(nxt == 0)
      panic("pin next proc");
    nxt = ready_proc(nxt);
    if(nxt != 0){
      *ppin = &nxt->mlfq;
    } else {
      if(self){
        nxt = ready_thread(p);
        *ppin = nxt != 0 ? &nxt->mlfq : &ptable.mlfq.queue[level];
      } else {
        *ppin = &ptable.mlfq.queue[level];
      }
    }
  }
}

/* Function: enqueue_thread
 * -------------------------
 * @group      MLFQ
 * @brief      Enqueue a thread to MLFQ
 * @note1      In MLFQ, thread states are as following:
 *             RUNNING, RUNNABLE
 * @note2      It guarantees gathering of threads of a process.
 *             ex) [p3t0]-[p1t0]-[p1t2]-[p1t1]-[p2t0]
 * @param[in]  th: thread to enqueue
 */
void
enqueue_thread(struct proc* th)
{
  struct proc *pivot;
  struct list_head *q;
  int level;

  if(th->state != RUNNABLE)
    panic("enqueue unready thread");

  pivot = ready_or_running_thread(th);
  if(th == pivot){ // no proc of the thread in queue
    level = main_thread(th)->privlevel;
    q = &ptable.mlfq.queue[level];
    list_add_tail(&th->mlfq, q);
  } else
    list_add_after(&th->mlfq, &pivot->mlfq);
}

/* Function: enqueue_proc
 * -------------------------
 * @group      MLFQ
 * @brief      Enqueue all threads of a process to MLFQ
 * @note1      In MLFQ, thread states are as following:
 *             RUNNING, RUNNABLE
 * @note2      Unlike the stride data structure, MLFQ has multiple
 *             threads of a process.
 * @note3      It guarantees gathering of threads of a process.
 *             ex) [p3t0]-[p1t0]-[p1t2]-[p1t1]-[p2t0]
 * @param[in]  p: process to enqueue
 */
static void
enqueue_proc(struct proc *p)
{
  struct list_head *q;
  int level;

  if(p->state != RUNNABLE && p->state != RUNNING)
    panic("enqueue proc: RUNNING, RUNNABLE");

  level = main_thread(p)->privlevel;
  q = &ptable.mlfq.queue[level];

  list_add_tail(&p->mlfq, q);
  threads_apply1(p, __routine_enqueue_remain, p);
}

/* Function: concatqueue
 * -------------------------
 * @group      MLFQ
 * @brief      Concatenate src queue to dst queue of MLFQ.
 * @note       It is called when the priority boost occurs.
 * @param[in]  src: the level of source queue
 * @param[in]  dst: the level of destination queue
 * @example    src: 1, dst: 0
 *             [queue0]     [queue1]     [queue2]
 *             ->[queue0~queue1]  []     [queue2]
 */
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

/* Function: dequeue_thread
 * -------------------------
 * @group      MLFQ
 * @brief      Dequeue a thread from MLFQ
 * @param[in]  th: thread to dequeue
 */
void
dequeue_thread(struct proc *th)
{
  pin_next_thread(th, 0);
  list_del(&th->mlfq);
}

/* Function: dequeue_proc
 * -------------------------
 * @group      MLFQ
 * @brief      Dequeue all threads of the process from MLFQ
 * @param[in]  p: process to dequeue
 */
static void
dequeue_proc(struct proc *p)
{
  pin_next_proc(p, 0);
  threads_apply0(p, __routine_dequeue_proc);
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

  if(!list_empty(&ptable.free)){
    p = list_first_entry(&ptable.free, struct proc, free);
    nproc--;
    list_del(&p->free);
    goto found;
  }

  return 0;

found:
  p->state = EMBRYO;

  list_head_init(&p->children);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    nproc++;
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
  p->pid = nextpid++;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  p->type = MLFQ;
  p->privlevel = 0;
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
  enqueue_proc(p);

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
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

static struct proc*
__search_thmain(struct proc *th1, struct proc *th2)
{
  return get_thread(th2, th1->thmain->tid);
}

static struct proc*
__routine_fork_thread(struct proc *th, void *main){
  int i;
  struct proc *nth;
  struct proc *thmain = (struct proc*)main;

  if(th->tid == 0)
    return 0;
  if(th->state != RUNNABLE &&
     th->state != RUNNING  &&
     th->state != SLEEPING){
    return 0;
  }

  if((nth = allocproc()) == 0){
    return th;
  }

  nth->pgdir = thmain->pgdir;

  nth->parent = thmain->parent;
  list_add_tail(&nth->sibling, &nth->parent->children);

  for(i = 0; i < NOFILE; i++)
    if(thmain->ofile[i])
      nth->ofile[i] = thmain->ofile[i];
  nth->cwd = thmain->cwd;

  safestrcpy(nth->name, thmain->name, sizeof(thmain->name));

  nth->pid = thmain->pid;
  nth->ustack = th->ustack;
  nth->tid = th->tid;

  list_add_tail(&nth->thgroup, &thmain->thgroup);
  if(th->thmain->tid == 0)
    nth->thmain = thmain;
  else {
    nth->thmain = __search_thmain(th, nth);
    if(nth->thmain == 0) nth->thmain = thmain;
  }

  return 0;
}

static struct proc*
__routine_rollback_thread(struct proc *th){
  list_del(&th->sibling);
  kfree(th->kstack);
  th->kstack = 0;
  th->state = UNUSED;
  nproc++;
  list_add(&th->free, &ptable.free);
  return 0;
}

int
fork(void)
{
  int i, pid, delta, sz;
  struct proc *np, *nxt;
  struct proc *curproc;
  struct proc *curmain;
  struct proc *th, *nth;
  struct list_head *start, *itr1, *itr2;

  acquire(&ptable.lock);

  curproc = myproc();
  curmain = main_thread(curproc);

  sz = thread_size(curproc);
  if(sz > nproc){
    release(&ptable.lock);
    kprintf_error("require: %d, remain: %d\n", sz, nproc);
    return -1;
  }

  // Allocate process.
  if((np = allocproc()) == 0){
    release(&ptable.lock);
    return -1;
  }
  np->pid = nextpid++;

  // Copy process state from proc.
  // main
  if((np->pgdir = copyuvm(curmain)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    nproc++;
    list_add(&np->free, &ptable.free);
    release(&ptable.lock);
    return -1;
  }

  for(i = 0; i < NOFILE; i++)
    if(curmain->ofile[i])
      np->ofile[i] = filedup(curmain->ofile[i]);
  np->cwd = idup(curmain->cwd);

  np->sz = curmain->sz;
  np->type = MLFQ;
  np->privlevel = 0;
  np->tid = 0;
  np->thmain = np;
  list_head_init(&np->thgroup);
  np->parent = curproc;
  list_add_tail(&np->sibling, &curproc->children);
  np->ustack = curmain->ustack;
  safestrcpy(np->name, curmain->name, sizeof(curmain->name));

  // thread
  if(threads_apply1(curmain, __routine_fork_thread, (void*)np) != 0){
    for(i = 0; i < NOFILE; i++){
      if(np->ofile[i]){
        fileclose(np->ofile[i]);
        np->ofile[i] = 0;
      }
    }
    begin_op();
    iput(np->cwd);
    end_op();
    np->cwd = 0;
    threads_apply0(np, __routine_rollback_thread);
    freevm(np->pgdir);
    release(&ptable.lock);
    return -1;
  }

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

  pid = np->pid;

  enqueue_proc(nxt);

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *p, *th, *curproc = myproc();
  struct list_head *children, *itr;
  int fd;
  int null = 0;

  if(curproc == initproc)
    panic("init exiting");

  // Terminate threads
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
  p->type = 0;
  p->killed = 0;
  p->tickets = 0;
  p->pass = 0;
  p->ticks = 0;
  p->privlevel = 0;
  p->state = UNUSED;
  nproc++;
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
mlfqselect(){
  struct proc *p;
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
      if(p->state == RUNNING){
        pin_next_thread(p, 0);
        if(*ppin == q)
          pin_next_proc(p, 0);
        if(*ppin == q)
          continue;
      }
      if(main_thread(p)->privlevel != l)
        panic("mlfqselect");
      *ppin = &p->mlfq;
      return p;
    }
  }

  return 0;
}

static void
priority_boost(){
  struct proc *p;
  struct list_head *q;
  struct list_head *itr;
  int l, baselevel = QSIZE-1;
  // RUNNABLE, RUNNING
  for(l = 1; l <= baselevel; l++){
    q = &ptable.mlfq.queue[l];
    for(itr = q->next; itr != q; itr = itr->next){
      p = list_entry(itr, struct proc, mlfq);
      p->privlevel = 0;
      p->ticks = 0;
    }
    concatqueue(l, 0);
  }
  // SLEEPING
  q = &ptable.sleep;
  for(itr = q->next; itr != q; itr = itr->next){
    p = list_entry(itr, struct proc, sleep);
    p->privlevel = 0;
    p->ticks = 0;
  }
  ptable.mlfq.ticks = 0;
}

void
mlfqlogic(struct proc *p){
  struct proc *th;
  struct proc *thmain = main_thread(p);
  int l, baselevel = QSIZE-1;

  l = thmain->privlevel;
  if(l < baselevel && thmain->ticks >= TA(l)){
    dequeue_proc(p);
    thmain->privlevel = l+1;
    thmain->ticks = 0;
    th = ready_or_running_thread(p);
    if(th != 0)
      enqueue_proc(th);
  } else if(thmain->ticks % TQ(l) == 0){
    pin_next_proc(p, 1);
  } else {
    pin_next_thread(p, 1);
  }
}

void
stridelogic(struct proc *p){
  struct list_head *q;
  struct list_head *itr;
  struct proc *pitr;
  struct proc *nxt, *thmain;
  int minpass;
  int i;

  // Pass overflow handling
  minpass = p == 0 || p->type == MLFQ ?
    ptable.mlfq.pass : main_thread(p)->pass;
  if(minpass > BARRIER){
    for(i = 1; i <= ptable.stride.size; i++){
      main_thread(ptable.stride.minheap[i])->pass -= minpass;
    }
    q = &ptable.stride.run;
    for(itr = q->next; itr != q; itr = itr->next){
      pitr = list_entry(itr, struct proc, run);
      main_thread(pitr)->pass -= minpass;
    }
    ptable.mlfq.pass -= minpass;
  }

  // Pass increases by stride
  if(p == 0 || p->type == MLFQ){
    ptable.mlfq.pass += STRD(ptable.mlfq.tickets);
  } else if(p->type == STRIDE){
    thmain = main_thread(p);
    thmain->pass += STRD(thmain->tickets);
    if((nxt = ready_thread(p)) != 0){
      pushheap(nxt);
    } else if((nxt = sleeping_thread(p)) != 0){
      pushheap(nxt);
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
      switchkvm();

      if(p->type == MLFQ)
        mlfqlogic(c->proc);
      else
        list_del(&p->run);
    }

    // Priority boost
    if(ptable.mlfq.ticks >= BOOSTINTERVAL){
      priority_boost();
    }

    // Log stride
    if(p == 0 || p->state == SLEEPING)
      stridelogic(p);
    else
      stridelogic(c->proc);

    c->proc = 0;

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
  struct proc *thmain = main_thread(p);
  struct proc *nxt = ready_thread(p);

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;

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
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *thmain;
  struct proc *p;

  acquire(&ptable.lock);  //DOC: yieldlock
  p = myproc();
  thmain = main_thread(p);
  thmain->ticks++;
  if(thmain->type == MLFQ)
    ptable.mlfq.ticks++;
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
  if(p->type == MLFQ)
    dequeue_thread(p);
  p->state = SLEEPING;
  list_add(&p->sleep, &ptable.sleep);

  sched();

  // Tidy up.
  myproc()->chan = 0;

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
        enqueue_thread(p);
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
      terminate_proc(p);
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING){
        list_del(&p->sleep);
        p->state = RUNNABLE;
        if(p->type == MLFQ)
          enqueue_thread(p);
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
  [ZOMBIE]    "zombie",
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  cprintf("remain: %d\n", nproc);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = p->killed ? "killed" : states[p->state];
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
