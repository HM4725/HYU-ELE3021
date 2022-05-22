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

typedef struct proc* (*callback0)(struct proc*);
typedef struct proc* (*callback1)(struct proc*, void*);

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

struct proc*
__routine_get_thread(struct proc* th, void* tid)
{
  return th->tid == (thread_t)tid ? th : 0;
}

static struct proc*
__get_thread(thread_t thread)
{
  return threads_apply1(myproc(),
                        __routine_get_thread,
                        (void*)thread);
}

struct proc*
main_thread(struct proc *th)
{
  while(th != th->thmain)
    th = th->thmain;
  return th;
}

struct proc*
__routine_is_ready(struct proc* th)
{
  return th->state == RUNNABLE ? th : 0;
}

struct proc*
ready_thread(struct proc *th)
{
  return threads_apply0(th, __routine_is_ready);
}

struct proc*
__routine_is_sleeping(struct proc* th)
{
  return th->state == SLEEPING ? th : 0;
}

struct proc*
sleeping_thread(struct proc *th)
{
  return threads_apply0(th, __routine_is_sleeping);
}

struct proc*
__routine_is_ready_or_running(struct proc* th)
{
  return th->state == RUNNABLE || th->state == RUNNING ? th : 0;
}

struct proc*
ready_or_running_thread(struct proc *th)
{
  return threads_apply0(th, __routine_is_ready_or_running);
}

int
thread_create(thread_t *thread,
              void *(*start_routine)(void *),
              void *arg)
{
  int i;
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
  nth->sz = thmain->sz;
  nth->parent = thmain->parent;
  list_add_tail(&nth->sibling, &thmain->parent->children);
  nth->type = thmain->type;
  nth->privlevel = thmain->privlevel;

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

  // Set files
  for(i = 0; i < NOFILE; i++)
    if(thmain->ofile[i])
      nth->ofile[i] = thmain->ofile[i];
  nth->cwd = thmain->cwd;

  safestrcpy(nth->name, thmain->name, sizeof(thmain->name));

  acquire(&ptable.lock);

  nth->state = RUNNABLE;
  if(nth->type == MLFQ)
    enqueue_thread(nth);
  invalidate_tlb(curth);

  release(&ptable.lock);

  return 0;
}

void
thread_exit(void *retval)
{
  struct proc *curth = myproc();
  int fd;

  if(curth == curth->thmain)
    return;
  for(fd = 0; fd < NOFILE; fd++)
    curth->ofile[fd] = 0;
  curth->cwd = 0;

  acquire(&ptable.lock);

  curth->retval = retval;
  wakeup1(curth->thmain);

  if(curth->type == MLFQ)
    dequeue_thread(curth);
  curth->state = ZOMBIE;
  sched();
  panic("zombie thread exit");
}

static void
freethread(struct proc *th)
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
      freethread(th);
      release(&ptable.lock);
      return 0;
    }
    sleep(curth, &ptable.lock);
  }
}

struct proc*
__routine_kill_thread(struct proc* th)
{
  th->killed = 1;
  return 0;
}

void
terminate_proc(struct proc* p){
  threads_apply0(p, __routine_kill_thread);
}

struct proc*
__routine_usurp_proc(struct proc *th, void *main)
{
  th->thmain = (struct proc*)main;
  return 0;
}

// usurp_main doesn't guarantee the order of threads.
// If it is called, the other threads have to be
// terminated.
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
