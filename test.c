#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"


int
main()
{

  // printf(1, "kharaki\n");
  // char h[1];
  // h[0] = '5';
  // int i =0, j =0, k =0;
  // while(i < 5858){
  //   while (j<98989){
  //     while (k<98956)
  //     {
  //       k++;
  //     }
  //     j++;
  //   }
  //   i++;
  // }

      
    
    
  //   i++;
  // setLotteryTicket(2, 15);
  // setSRPFPriority(1, h);
  // changeQueue(2,2);
  // printInfo();
  int pid;
  for(int i = 0; i < 3; i++){
    pid = fork();
    if(!pid){
      int j = 0;
        while (1)
        {
          j++;
        }   
    }  
  }
  for( int i =0; i < 3; i++)
    wait();

  exit();
}
