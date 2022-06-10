#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

char stdout = 1;
thread_safe_guard *guard;

#define NBLOCKS 256
#define NTHREAD 8
#define WARP    6
#define BSIZE   512

void* reader(void *arg){
  int tid = gettid();
  int offset = (int)arg;
  int n = NBLOCKS * BSIZE / NTHREAD;
  char *buf;
  int result;

  buf = malloc(sizeof(char) * n);
  memset(buf, 0, n);
  result = thread_safe_pread(guard, buf, n, offset);
  printf(stdout, "[%d] pread finish\n", tid);
  free(buf);
  return (void*)result;
}

void* writer(void *arg){
  int tid = gettid();
  int offset = (int)arg;
  int n = NBLOCKS * BSIZE / NTHREAD;
  char *buf;
  int result;

  buf = malloc(sizeof(char) * n);
  memset(buf, 0, n);
  strcpy(buf, "  [");
  buf[3] = tid < 10 ? ' ' : '0' + (tid / 10);
  buf[4] = '0' + (tid % 10);
  strcpy(buf+5, "] changed\n");
  strcpy(buf+BSIZE, "->[");
  buf[BSIZE+3] = tid < 10 ? ' ' : '0' + (tid / 10);
  buf[BSIZE+4] = '0' + (tid % 10);
  strcpy(buf+BSIZE+5, "] changed\n");

  result = thread_safe_pwrite(guard, buf, n, offset);
  printf(stdout, "[%d] pwrite finish\n", tid);

  free(buf);
  return (void*)result;
}

int main() {
  int i, fd, off;
  thread_t thrd[WARP*NTHREAD];
  int status;
  char buf[BSIZE * 2] = {'a', '\n', 0};

  fd = open("testfile", O_CREATE|O_RDWR);
  guard = thread_safe_guard_init(fd);
  off = 0;
  while(off < NBLOCKS * BSIZE){
    off += BSIZE * 2;
    write(fd, buf, BSIZE*2);
  }
  for(i = 0; i < WARP*NTHREAD; i++){
    void* (*start_routine)(void*) = \
      i % WARP == (WARP-1) ? writer : reader;
    off = NBLOCKS * BSIZE / NTHREAD * (i / WARP);
    thread_create(&thrd[i], start_routine, (void*)off); 
  }
  for(i = 0; i < WARP*NTHREAD; i++){
    thread_join(thrd[i], (void**)&status);
  }
  thread_safe_guard_destroy(guard);
  exit();
}
