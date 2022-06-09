#include "types.h"
#include "user.h"

thread_safe_guard*
thread_safe_guard_init(int fd)
{
  thread_safe_guard *guard;

  if((guard = malloc(sizeof(thread_safe_guard))) == 0)
    return 0;

  guard->fd = fd;

  if(rwlock_init(&guard->lock) < 0){
    free(guard);
    return 0;
  }

  return guard;
}
int
thread_safe_pread(thread_safe_guard *file_guard, void *addr, int n, int off)
{
  int result;
  if(rwlock_acquire_readlock(&file_guard->lock) < 0)
    return -1;
  result = pread(file_guard->fd, addr, n, off);
  if(rwlock_release_readlock(&file_guard->lock) < 0)
    return -1;
  return result;
}
int
thread_safe_pwrite(thread_safe_guard *file_guard, void *addr, int n, int off)
{
  int result;
  if(rwlock_acquire_writelock(&file_guard->lock) < 0)
    return -1;
  result = pwrite(file_guard->fd, addr, n, off);
  if(rwlock_release_writelock(&file_guard->lock) < 0)
    return -1;
  return result;
}
void
thread_safe_guard_destroy(thread_safe_guard *file_guard)
{
  if(file_guard > 0)
    free(file_guard);
}
