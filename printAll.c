#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{   
  if(argc >= 2){
    printf(1, "printProcess: Invalid number of arguments!\n");
    exit();
  }

  printInfo();
  exit();
}
