#include "types.h"
#include "user.h"
#include "atomic.h"

#define SLEEPTIME 30
#define UNUSED (-1)

// Helpers
static void
queue_init(queue_t* q)
{
  q->head = 0;
  q->rear = 0;
  for(int i = 0; i < XEMQSZ; i++){
    q->queue[i] = UNUSED;
  }
}

static int
queue_empty(queue_t* q)
{
  return q->head == q->rear;
}

static thread_t*
queue_add(queue_t* q, thread_t key)
{
  int nxt;
  thread_t* addr;
  nxt = (q->rear + 1) % XEMQSZ;
  if(nxt != q->head){
    addr = &q->queue[q->rear];
    *addr = key;
    q->rear = nxt;
    return addr;
  } else {
    return 0;
  }
}

static thread_t*
queue_head(queue_t* q)
{
  return &q->queue[q->head];
}

static void
queue_remove(queue_t* q)
{
  q->queue[q->head] = UNUSED;
  q->head = (q->head + 1) % XEMQSZ;
}

int
xem_init(xem_t *sema)
{
  if(sema == 0)
    return -1;
  sema->guard = 0;
  sema->count = 1;
  queue_init(&sema->q);
  return 0;
}
int
xem_wait(xem_t *sema)
{
  int timer = 0;
  thread_t *addr, val;

  while(test_and_set(&sema->guard, 1) == 1){
    if(timer++ < SLEEPTIME){
      timer = 0;
      sleep(1);
    }
  }
  sema->count--;
  if(sema->count >= 0) {
    sema->guard = 0;
  } else {
    val = gettid();
    if((addr = queue_add(&sema->q, val)) == 0)
      return -1;
    sema->guard = 0;
    if(futex_wait(addr, val) < 0)
      return -1;
  }
  return 0;
}
int
xem_unlock(xem_t *sema)
{
  int timer = 0;
  while(test_and_set(&sema->guard, 1) == 1){
    if(timer++ < SLEEPTIME){
      timer = 0;
      sleep(1);
    }
  }
  sema->count++;
  if(!queue_empty(&sema->q)){
    futex_wake(queue_head(&sema->q));
    queue_remove(&sema->q);
  }
  sema->guard = 0;
  return 0;
}
int
rwlock_init(rwlock_t *rwlock)
{
  if(rwlock == 0)
    return -1;
  if(xem_init(&rwlock->lock) < 0)
    return -1;
  if(xem_init(&rwlock->writelock) < 0)
    return -1;
  rwlock->readers = 0;
  return 0;
}
int
rwlock_acquire_readlock(rwlock_t *rwlock)
{
  if(rwlock == 0)
    return -1;
  if(xem_wait(&rwlock->lock) < 0)
    return -1;
  rwlock->readers++;
  if(rwlock->readers == 1){
    if(xem_wait(&rwlock->writelock) < 0)
      return -1;
  }
  return xem_unlock(&rwlock->lock);
}
int
rwlock_acquire_writelock(rwlock_t *rwlock)
{
  if(rwlock == 0)
    return -1;
  return xem_wait(&rwlock->writelock);
}
int
rwlock_release_readlock(rwlock_t *rwlock)
{
  if(rwlock == 0)
    return -1;
  if(xem_wait(&rwlock->lock) < 0)
    return -1;
  rwlock->readers--;
  if(rwlock->readers == 0){
    if(xem_unlock(&rwlock->writelock) < 0)
      return -1;
  }
  return xem_unlock(&rwlock->lock);
}
int
rwlock_release_writelock(rwlock_t *rwlock)
{
  if(rwlock == 0)
    return -1;
  return xem_unlock(&rwlock->writelock);
}
