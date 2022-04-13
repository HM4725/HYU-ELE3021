#include "types.h"
#include "user.h"

int
main(int argc, char* argv[])
{
  int pid;
  int start = uptime();
  int end;
  if(argc < 2) exit();

  pid = fork();
  if(pid > 0){
    wait();
  } else if(pid == 0){
    exec(argv[1], argv + 1);
    exit();
  } else {
    printf(1, "fail!\n");
    exit();
  }
  end = uptime();
  printf(1, "Spent time: %dms\n", (end - start)*10);
  exit();
}
