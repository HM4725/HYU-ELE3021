#include "types.h"
#include "user.h"

int
main(void)
{
  int pid;
  int begin, term = 200;
  int x = 0;
  int result;

  printf(1, "stride test start\n");
  pid = fork();
  if(pid > 0){
    result = set_cpu_share(10);
    if(result == 0){
      printf(1, "parent (10)\n");
      begin = uptime();
      while(uptime() - begin < term){
        x++;
      }
      printf(1, "parent x: %d\n", x);
    } else {
      printf(1, "parent share(10) failed\n");
    }
    wait();
  } else if(pid == 0){
    result = set_cpu_share(40);
    if(result == 0){
      printf(1, "child (40)\n");
      begin = uptime();
      while(uptime() - begin < term){
        x++;
      }
      printf(1, "child x: %d\n", x);
    } else {
      printf(1, "child share(40) failed\n");
    }
  }
  exit();
}
