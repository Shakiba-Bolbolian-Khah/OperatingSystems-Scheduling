#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"


int
main()
{
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
