#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

char stdout = 1;

#define NBLOCKS 32768
#define BSIZE 512

#define REP 4
#define NTESTS 10
#define MAGIC ('\n')

int randint(int seed)
{
  return (seed << 7) * seed + 13;
}

int main()
{
  int n, fd;
  char *buf;
  char *fname = "test_bigfile";
  char magic = MAGIC;

  for(int r = 0; r < REP; r++){
    if((fd = open(fname, O_CREATE|O_RDWR)) < 0){
      printf(stdout, "error: open %s failed.\n", fname);
      exit();
    }
    printf(stdout, "[%d] create %s\n", r, fname);

    n = sizeof(char) * BSIZE * NBLOCKS;
    if((buf = malloc(sizeof(char) * n)) < 0){
      printf(stdout, "error: malloc failed.\n");
      exit();
    }
    memset(buf, magic, sizeof(char) * n);

    if(write(fd, buf, n) != n){
      printf(stdout, "error: write failed.\n");
      exit();
    }
    close(fd);
    printf(stdout, "    write %d MB pass.\n", n/(1024*1024));

    if((fd = open(fname, O_RDONLY)) < 0){
      printf(stdout, "error: open %s failed.\n", fname);
      exit();
    }

    if(read(fd, buf, n) != n){
      printf(stdout, "error: read failed.\n");
      exit();
    }

    for(int i = 0; i < NTESTS; i++){
      if(buf[randint(uptime()) % n] != magic){
        printf(stdout, "error: read & write mismatch.\n");
        exit();
      }
    }
    close(fd);
    printf(stdout, "    read %d MB pass.\n", n/(1024*1024));

    unlink(fname);
    printf(stdout, "[%d] rm %s\n", r, fname);
  }
  exit();
}
