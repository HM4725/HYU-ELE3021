#include "types.h"
#include "user.h"

int
main(void)
{
  int n = 3;
  while(n > 0){
    printf(1, "[test]: %d\n", n);
    yield();
    n = n - 1;
  }
  exit();
}
