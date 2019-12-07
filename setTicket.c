#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{   
  if(argc >= 4){
    printf(1, "setTicket: Invalid number of arguments!\n");
    exit();
  }
  
  int pid = atoi(argv[1]);
  int ticketNum = atoi(argv[2]);

  setLotteryTicket(pid, ticketNum);
  exit();
}
