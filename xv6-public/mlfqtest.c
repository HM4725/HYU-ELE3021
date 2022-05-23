#include "types.h"
#include "user.h"

int
main(void)
{
  int level[3] = {0, 0, 0};
  int n = 10000;

  printf(1, "mlfq test start\n");
  while(n > 0){
    level[getlev()]++;
    yield();
    n = n - 1;
  }
  printf(1, "privlevel 0: %d\n", level[0]);
  printf(1, "privlevel 1: %d\n", level[1]);
  printf(1, "privlevel 2: %d\n", level[2]);

  exit();
}
