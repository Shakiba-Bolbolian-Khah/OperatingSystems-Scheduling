#include "types.h"
#include "x86.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int
sys_setTicket(void)
{
  int ticketNum;

  if(argint(0, &ticketNum) < 0)
    return -1; 

  struct proc *p = myproc();
  p->mlfq.lotteryTicket = ticketNum;
  return 0;
}

int 
sys_changeQueue(void)
{
  int pid, queueNumber;

  if(argint(0, &pid) < 0)
    return -1; 
  if(argint(1,&queueNumber) < 0)
    return -1;

  return changeQueue(pid, queueNumber);

}

int sys_setLotteryTicket(void)
{
  int pid, newTicket;

  if(argint(0, &pid) < 0)
    return -1;
  if(argint(1, &newTicket) < 0)
    return -1;

  return  setLotteryTicket(pid, newTicket);
}



int 
sys_setSRPFPriority(void)
{
  int pid;
  char* newPriority;
  if(argint(0, &pid) < 0)
    return -1;
  if(argstr(1, &newPriority) < 0)
    return -1;
  return  setSRPFPriority(pid, newPriority);
}

int
sys_printInfo(void)
{
  return printInfo();
}