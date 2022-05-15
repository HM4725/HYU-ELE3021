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
    if(routine(th) != 0)
      return th;
    itr = itr->next;
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
    if(routine(th, arg) != 0)
      return th;
    itr = itr->next;
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
__routine_select_next(struct proc* th)
{
  return th->state == RUNNABLE ? th : 0;
}

struct proc*
next_thread(struct proc *th)
{
  return threads_apply0(th, __routine_select_next); 
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
  struct proc *thmain = curth->thmain;
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
  nth->thmain = thmain;
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
      //nth->ofile[i] = filedup(thmain->ofile[i]);
  //nth->cwd = idup(thmain->cwd);
  nth->cwd = thmain->cwd;

  safestrcpy(nth->name, thmain->name, sizeof(thmain->name));

  acquire(&ptable.lock);

  nth->state = RUNNABLE;
  if(nth->type == MLFQ)
    enqueue_thread(nth, (void*)thmain->privlevel);

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
  struct proc *curth = myproc();
  struct proc *th;

  acquire(&ptable.lock);
  for(;;){
    if((th = __get_thread(thread)) == 0){
      cprintf("%d\n", th->tid);
      release(&ptable.lock);
      return -1;
    }
    if(th->state == ZOMBIE){
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
