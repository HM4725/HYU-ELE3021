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
#include "thread.h"
#include "debug.h"

extern struct ptable ptable;

/* Type: callback0
 * ------------------------
 * @group      Thread
 * @type       struct proc* (*)(struct proc*)
 * @brief      Function pointer type which has no argument.
 * @note       This type is used at thread_apply0
 */
typedef struct proc* (*callback0)(struct proc*);
/* Type: callback1
 * ------------------------
 * @group      Thread
 * @type       struct proc* (*)(struct proc*)
 * @brief      Function pointer type which has 1 argument.
 * @note       This type is used at thread_apply1
 */
typedef struct proc* (*callback1)(struct proc*, void*);

/* Function: threads_apply0
 * ------------------------
 * @group      Thread
 * @brief      Apply routine to all threads in the process.
 * @note1      Apply routine from the next thread of p to p.
 * @note2      If a routine returns non-zero value,
 *             then it stops and returns the value.
 * @param[in]  p: entry thread to apply routine.
 * @param[in]  routine: zero-argument routine to apply to threads.
 * @return     If a routine returns non-zero value, it returns it.
 *             Otherwise it returns 0.
 */
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

/* Function: threads_apply1
 * ------------------------
 * @group      Thread
 * @brief      Apply routine to all threads in the process.
 * @note1      Apply routine from the next thread of p to p.
 * @note2      If a routine returns non-zero value,
 *             then it stops and returns the value.
 * @param[in]  p: entry thread to apply routine.
 * @param[in]  routine: 1-argument routine to apply to threads.
 * @param[in]  arg: argument of routine.
 * @return     If a routine returns non-zero value, it returns it.
 *             Otherwise it returns 0.
 */
struct proc*
threads_apply1(struct proc* p,
              callback1 routine,
              void *arg)
{
  struct list_head *itr, *start;
  struct proc *th;

  start = p->thgroup.next;
  itr = start;
  do {
    th = list_entry(itr, struct proc, thgroup);
    itr = itr->next;
    if(routine(th, arg) != 0)
      return th;
  } while(itr != start);
  return 0;
}

////////////

static struct proc*
__routine_get_thread(struct proc* th, void* tid)
{
  return th->tid == (thread_t)tid ? th : 0;
}

static struct proc*
__routine_handle_orphan_thread(struct proc* th, void* main)
{
  struct proc *thmain = (struct proc*)main;
  if(th->thmain == thmain)
    th->thmain = main_thread(thmain);
  return 0;
}

static struct proc*
__routine_is_ready(struct proc* th)
{
  return th->state == RUNNABLE ? th : 0;
}

static struct proc*
__routine_is_sleeping(struct proc* th)
{
  return th->state == SLEEPING ? th : 0;
}

static struct proc*
__routine_is_ready_or_running(struct proc* th)
{
  return th->state == RUNNABLE || th->state == RUNNING ? th : 0;
}

static struct proc*
__routine_kill_thread(struct proc* th)
{
  th->killed = 1;
  return 0;
}

static struct proc*
__routine_usurp_proc(struct proc *th, void *main)
{
  th->thmain = (struct proc*)main;
  return 0;
}

////////////

static struct proc*
__get_thread(thread_t thread)
{
  return threads_apply1(myproc(),
                        __routine_get_thread,
                        (void*)thread);
}

static void
__free_thread(struct proc *th)
{
  kfree(th->kstack);
  th->kstack = 0;
  deallocustack(th->pgdir, th->ustack);
  th->pid = 0;
  th->tid = 0;
  th->parent = 0;
  th->name[0] = 0;
  th->killed = 0;
  th->tickets = 0;
  th->pass = 0;
  th->ticks = 0;
  th->privlevel = 0;
  th->retval = 0;
  th->state = UNUSED;
  list_add(&th->free, &ptable.free);
}

static void
__usurp_proc(struct proc *th)
{
  int i;
  struct proc *thmain = main_thread(th);

  th->sz = thmain->sz;
  th->ticks = thmain->ticks;
  for(i = 0; i < NOFILE; i++)
    if(thmain->ofile[i])
      th->ofile[i] = thmain->ofile[i];
  th->cwd = thmain->cwd;
  if(th->type == STRIDE){
    th->tickets = thmain->tickets;
    th->pass = thmain->pass;
  }
  threads_apply1(th, __routine_usurp_proc, th);
  thmain->tid = th->tid;
  th->tid = 0;
}

//////////

struct proc*
main_thread(struct proc *th)
{
  while(th != th->thmain)
    th = th->thmain;
  return th;
}

struct proc*
ready_thread(struct proc *th)
{
  return threads_apply0(th, __routine_is_ready);
}

struct proc*
sleeping_thread(struct proc *th)
{
  return threads_apply0(th, __routine_is_sleeping);
}

struct proc*
ready_or_running_thread(struct proc *th)
{
  return threads_apply0(th, __routine_is_ready_or_running);
}

//////////
void
terminate_proc(struct proc* p){
  threads_apply0(p, __routine_kill_thread);
}

int
monopolize_proc(struct proc *p)
{
  struct proc *th;
  int null = 0;

  acquire(&ptable.lock);
  wakeup1(p->thmain);
  if(p != p->thmain)
    __usurp_proc(p);
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

/* Function: thread_create
 * ------------------------
 * @group      Thread
 * @brief      Create a thread
 * @note       The thread shares the memory space in the process.
 * @param[out] thread: thread identifier
 * @param[in]  start_routine: new thread starts from start_routine
 * @param[in]  arg: argument of start_routine
 *                  To pass multiple arguments,
 *                  send a pointer to a structure
 * @return     On success 0 and on error -1
 */
int
thread_create(thread_t *thread,
              void *(*start_routine)(void *),
              void *arg)
{
  uint sp;
  struct proc *nth;
  struct proc *curth = myproc();
  struct proc *thmain = main_thread(curth);
  struct proc *thlast = list_last_entry(&thmain->thgroup,
                                        struct proc,
                                        thgroup);

  // Allocate process.
  if((nth = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  nth->pid = thmain->pid;
  nth->pgdir = thmain->pgdir;
  nth->parent = thmain->parent;
  list_add_tail(&nth->sibling, &thmain->parent->children);
  nth->type = thmain->type;

  // Set user stack
  sp = PGROUNDDOWN(thlast->ustack) - PGSIZE;
  if(allocustack(nth->pgdir, sp - USTACKSIZE) == 0){
    kfree(nth->kstack);
    nth->kstack = 0;
    list_del(&nth->sibling);
    nth->state = UNUSED;
    list_add(&nth->free, &ptable.free);
    return -1;
  }
  nth->ustack = sp - USTACKSIZE;
  sp -= 4;
  *(uint *)sp = (uint)arg;
  sp -= 4;
  *(uint *)sp = MAGICEXIT;

  // Set thread
  nth->tid = thlast->tid + 1;
  nth->thmain = curth;
  list_add_tail(&nth->thgroup, &thmain->thgroup);
  *thread = nth->tid;

  // Set trapframe
  *nth->tf = *curth->tf;
  nth->tf->eax = 0;
  nth->tf->esp = sp;
  nth->tf->eip = (uint)start_routine;

  safestrcpy(nth->name, thmain->name, sizeof(thmain->name));

  acquire(&ptable.lock);

  nth->state = RUNNABLE;
  if(nth->type == MLFQ)
    enqueue_thread(nth);
  invalidate_tlb(curth);

  release(&ptable.lock);

  return 0;
}

/* Function: thread_exit
 * ------------------------
 * @group      Thread
 * @brief      Terminate the thread
 * @note       Evenif a thread didn't call this function,
 *             it's ok. OS calls this function implicitly.
 * @param[in]  retval: retval is sent to the joining thread
 */
void
thread_exit(void *retval)
{
  struct proc *curth = myproc();
  int fd;

  if(curth == curth->thmain){
    kprintf_warn("exit fail! pid: %d (main)\n", curth->pid);
    return;
  }
  for(fd = 0; fd < NOFILE; fd++)
    curth->ofile[fd] = 0;
  curth->cwd = 0;

  acquire(&ptable.lock);

  curth->retval = retval;
  wakeup1(curth->thmain);

  threads_apply1(curth, __routine_handle_orphan_thread, curth);

  if(curth->type == MLFQ)
    dequeue_thread(curth);
  curth->state = ZOMBIE;
  sched();
  panic("zombie thread exit");
}

/* Function: thread_join
 * ------------------------
 * @group      Thread
 * @brief      Wait for a thread which exited.
 * @note       Evenif a thread didn't call this function,
 *             it's ok. OS calls this function implicitly.
 * @param[in]  thread: identifier of the thread which
                       current thread is waiting for.
 * @param[out] retval: the pointer for a return value
 *                     which exited thread sent.
 */
int
thread_join(thread_t thread, void **retval)
{
  struct proc *th, *curth;

  acquire(&ptable.lock);
  for(;;){
    curth = myproc();
    if((th = __get_thread(thread)) == 0 || curth->killed){
      kprintf_trace("join fail! pid: %d, tid: %d\n", curth->pid, thread);
      release(&ptable.lock);
      return -1;
    }
    if(th->state == ZOMBIE && th->thmain == curth){
      *retval = th->retval;
      list_del(&th->thgroup);
      list_del(&th->sibling);
      __free_thread(th);
      release(&ptable.lock);
      return 0;
    }
    sleep(curth, &ptable.lock);
  }
}
