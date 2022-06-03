#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "list.h"
#include "proc.h"
#include "spinlock.h"
#include "thread.h"

struct spinlock futex;

int
futex_wait(thread_t* addr, thread_t tid)
{
  struct proc* th;

  acquire(&futex);
  if(*addr == tid){
    th = myproc();
    if(tid == th->tid)
      sleep(th, &futex);
    else {
      release(&futex);
      return -1;
    }
  }
  release(&futex);
  return 0;
}

int
futex_wake(thread_t* addr)
{
  struct proc* th;

  acquire(&futex);
  th = get_thread(myproc(), *addr);
  if(th != 0) {
    wakeup(th);
  }
  else {
    release(&futex);
    return -1;
  }
  release(&futex);
  return 0;
}
