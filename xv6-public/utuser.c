#include "types.h"
#include "user.h"

/*
  push 20 (arg2)
  push 10 (arg1)
  push ret (call func)
  push ebp
  push locals
 */
int func(int arg1, int arg2){
  uint *ret = (uint *)&arg1 - 1;
  uint *ebp = (uint *)&arg1 - 2;

  printf(1, "func start\n");
  printf(1, "%s: %p: %d\n", "arg2 ", &arg2, arg2);
  printf(1, "%s: %p: %d\n", "arg1 ", &arg1, arg1);
  printf(1, "%s: %p: %x\n", "ret  ", ret, *ret);
  printf(1, "%s: %p: %x\n", "ebp  ", ebp, *ebp);
  printf(1, "%s: %p\n", "local", &ret);
  printf(1, "func end\n");
  return 0;
}

int main(int argc, char *argv[]){
  printf(1, "stack base: %p\n", (&argv + 5));
  printf(1, "heap base: %p\n", malloc(1)+8);
/*
  printf(1, "pid: %d\n", getpid());
  printf(1, "location of code  : %p\n", (void *) main);
  printf(1, "location of heap  : %p\n", (void *) malloc(1));
  printf(1, "location of stack : %p\n", (void *) &argc);
*/
  func(10, 20);
  exit();
}
