#include "types.h"
#include "user.h"

int
main(void)
{
  int n = 300;
  while(n > 0){
    yield();
    printf(1, "my privlevel: %d\n", getlev());
    n = n - 1;
  }
  exit();
}
