#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{   
  if(argc >= 4){
    printf(1, "setSRPF: Invalid number of arguments!\n");
    exit();
  }
  int pid = atoi(argv[1]);
  char* priority = argv[2];


  setSRPFPriority(pid, priority);
  exit();
}
