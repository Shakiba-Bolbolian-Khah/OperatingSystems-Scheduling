#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  cmostime(&(p->mlfq.arrivalTime));
  p->mlfq.queueNumber = 1;
  p->mlfq.executedCycleNumber = 1;
  p->mlfq.remainedPriority = 1;
  p->mlfq.lotteryTicket = 10;
  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  

  //----
  // cmostime(&(p->mlfq.arrivalTime));
  // p->mlfq.queueNumber = 1;
  // p->mlfq.executedCycleNumber = 1;
  // p->mlfq.remainedPriority = 1;
  // p->mlfq.lotteryTicket = 10;
  //----

  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }

    //----
  // cmostime(&(np->mlfq.arrivalTime));
  // np->mlfq.queueNumber = 1;
  // np->mlfq.executedCycleNumber = 1;
  // np->mlfq.remainedPriority = 1;
  // np->mlfq.lotteryTicket = 10;
  //----

  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
// void
// schedulerrr(void)
// {
//   struct proc *p;
//   struct cpu *c = mycpu();
//   c->proc = 0;
  
//   for(;;){
//     // Enable interrupts on this processor.
//     sti();

//     // Loop over process table looking for process to run.
//     acquire(&ptable.lock);
//     for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
//       if(p->state != RUNNABLE)
//         continue;
//       cprintf("process name:%s\n",p->name);

//       // Switch to chosen process.  It is the process's job
//       // to release ptable.lock and then reacquire it
//       // before jumping back to us.
//       c->proc = p;
//       switchuvm(p);
//       p->state = RUNNING;

//       swtch(&(c->scheduler), p->context);
//       switchkvm();

//       // Process is done running for now.
//       // It should have changed its p->state before coming back.
//       c->proc = 0;
//     }
//     release(&ptable.lock);

//   }
// }

unsigned long randstate = 1;
unsigned int
rand()
{
  randstate = randstate * 1664525 + 1013904223;
  return randstate;
}


int findLottery(){
  struct proc *p;
  struct proc *winner;
  int ticketSum = 0;
  int foundPid = -1;
  
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state != RUNNABLE)
      continue;
    if(p->mlfq.queueNumber != 1)
      continue;

    ticketSum = ticketSum + p->mlfq.lotteryTicket;  
  }
  int selectedTicket;
  if(ticketSum != 0){
    selectedTicket = rand() % ticketSum;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;
      if(p->mlfq.queueNumber != 1)
        continue;

      if( (selectedTicket - p->mlfq.lotteryTicket) <= 0 ){
        winner = p;
        foundPid = winner->pid;
        break;
      }
      else{
        selectedTicket -= p->mlfq.lotteryTicket;
        continue;
      }
    }
  }
  return foundPid;
}

int findHRRN(){
  struct proc *p;
  
  float maxHRRN = -1;
  struct rtcdate currentTime;
  int waitingTime;
  float HRRN;
  struct proc *winner;
  int foundPid = -1;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state != RUNNABLE)
      continue;
    if(p->mlfq.queueNumber != 2)
      continue;

    cmostime(&currentTime);
    waitingTime = (currentTime.second - p->mlfq.arrivalTime.second) + (currentTime.minute - p->mlfq.arrivalTime.minute)*60 + (currentTime.hour - p->mlfq.arrivalTime.hour)*3600;
    HRRN = (float) waitingTime / (float) p->mlfq.executedCycleNumber;
    if( HRRN > maxHRRN ){
      maxHRRN = HRRN;
      winner = p;
      foundPid = winner->pid;
    }
  }
  
  return foundPid;
}

int findSRPF(){
  struct proc *p;
  int foundPid = -1;
  float minRemainedPriority = 500000;
  struct proc *winner;
  int repeatedMinNum = 1;
  
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state != RUNNABLE)
        continue;
    if(p->mlfq.queueNumber != 3)
      continue;

    // In order to select random between whose remained priority is the same, we always choose the first one.

    if(p->mlfq.remainedPriority < minRemainedPriority){
      winner = p;
      foundPid = winner->pid;
      minRemainedPriority = winner->mlfq.remainedPriority;
      repeatedMinNum = 1;
    }
    else if(p->mlfq.remainedPriority == minRemainedPriority){
      repeatedMinNum++;
    }
  }

  if(repeatedMinNum != 1){
    int randNum = (rand() % repeatedMinNum) +1;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if((p->state == RUNNABLE) && (p->mlfq.queueNumber == 3) && (p->mlfq.remainedPriority == minRemainedPriority)){
        if(randNum==1){
          winner = p;
          foundPid = winner->pid;
          return foundPid;
        }
        randNum--;
      }
    }
  }
  
  return foundPid;
}


void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  int isThirdQueue = 0;
  int foundPid = -1;
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);

    //---


    if( (foundPid = findLottery()) != -1) {
      isThirdQueue = 0;
    }
    else if( (foundPid = findHRRN()) != -1) {
      isThirdQueue = 0;
    }
    else if( (foundPid = findSRPF()) != -1) {
      isThirdQueue = 1;
    }
    else{
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->state != RUNNABLE)
          continue;
        c->proc = p;
        switchuvm(p);
        p->state = RUNNING;

        swtch(&(c->scheduler), p->context);
        switchkvm();
        c->proc = 0;
      }
      release(&ptable.lock);
      continue;
    }
    if(foundPid != -1){
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->pid == foundPid){
          c->proc = p;
          c->proc->mlfq.executedCycleNumber+= 1;

          if(isThirdQueue == 1){
            if( (c->proc->mlfq.remainedPriority - 0.1) < 0)
              c->proc->mlfq.remainedPriority = 0;
            else  
              c->proc->mlfq.remainedPriority = c->proc->mlfq.remainedPriority - 0.1;
          }
          break;
        }
      } 
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();
      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);  
  }
}



// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }
  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

int
changeQueue(int pid, int queueNumber)
{
  struct proc *p;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
   if(p->pid == pid) {
     p->mlfq.queueNumber = queueNumber;
     return 0;
   }
  }
  return -1;
}

int
setLotteryTicket(int pid, int newTicket)
{
  struct proc *p;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
   if((p->mlfq.queueNumber == 1) && (p->pid == pid)) {
     p->mlfq.lotteryTicket = newTicket;
     return 0;
   }
  }
  return -1;
}

// Here is the disgusting part inorder to change float to string

void
reverse(char* str, int len) 
{ 
    int i = 0, j = len - 1, temp; 
    while (i < j) { 
        temp = str[i]; 
        str[i] = str[j]; 
        str[j] = temp; 
        i++; 
        j--; 
    } 
} 

int 
intToStr(int x, char str[], int d) 
{ 
    int i = 0;
    if(x == 0 && d== 0){
      str[i++] = '0';
    } 
    while (x) { 
        str[i++] = (x % 10) + '0'; 
        x = x / 10; 
    } 
  
    // If number of digits required is more, then 
    // add 0s at the beginning 
    while (i < d) 
        str[i++] = '0'; 
  
    reverse(str, i); 
    str[i] = '\0'; 
    return i; 
} 

void 
floatToStr(float in, int afterpoint, char* res)
{ 
  int ipart = (int)in;
  float fpart = in - (float)ipart; 
  int i = intToStr(ipart, res, 0);
  res[i] = '.'; // add dot 
  for(int i = 0; i < afterpoint; i++){
    fpart = fpart * 10; 
  }
  intToStr((int)fpart, res + i + 1, afterpoint);  
}

float
strToFloat(char* s)
{
  float rez = 0, fact = 1;
  if (*s == '-'){
    s++;
    fact = -1;
  };
  for (int point_seen = 0; *s; s++){
    if (*s == '.'){
      point_seen = 1; 
      continue;
    };
    int d = *s - '0';
    if (d >= 0 && d <= 9){
      if (point_seen) fact /= 10.0f;
      rez = rez * 10.0f + (float)d;
    };
  };
  return rez * fact;

}


int
setSRPFPriority(int pid, char* newStrPriority)
{
  struct proc *p;
  float newPriority;
  newPriority = strToFloat(newStrPriority);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if((p->mlfq.queueNumber == 3) && (p->pid == pid)) {
      p->mlfq.remainedPriority = newPriority;
      return 0;
    }
  }
  return -1;  
}

void
printState(struct proc *p)
{
  switch (p->state){
    case 0:
      cprintf("UNUSED    ");
      break;
    case 1:
      cprintf("EMBRYO    ");
      break;
    case 2:
      cprintf("SLEEPING  ");
      break;
    case 3:
      cprintf("RUNNABLE  ");
      break;
    case 4:
      cprintf("RUNNING   ");
      break;
    case 5:
      cprintf("ZOMBIE    ");
      break;

  }
}


int
printInfo(void)
{
  cprintf("name      pid  state     priority  ticket  queueNum  cycle  HRRN     createTime\n");
  cprintf("-------------------------------------------------------------------------------\n");
  
  struct proc *p;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == 0 || p->state == 1)
      continue;
    int size = strlen(p->name);
    cprintf("%s",p->name);
    for (int i = 0; i < 10 - size; i++)
      cprintf(" ");
    cprintf("%d",p->pid);
    char buf[5];
    intToStr(p->pid,buf,0);
    size = strlen(buf);
    for (int i = 0; i < 5 - size; i++)
      cprintf(" ");
    printState(p);
    floatToStr(p->mlfq.remainedPriority,1,buf);
    cprintf("%s", buf);
    size = strlen(buf);
    for(int i = 0; i < 10 - size; i++)
      cprintf(" ");
    cprintf("%d", p->mlfq.lotteryTicket);
    intToStr(p->mlfq.lotteryTicket,buf,0);
    size = strlen(buf);
    for(int i = 0; i < 8 - size; i++)
      cprintf(" ");
    cprintf("%d", p->mlfq.queueNumber);
    intToStr(p->mlfq.queueNumber,buf,0);
    size = strlen(buf);
    for (int i = 0; i < 10 - size; i++)
      cprintf(" ");
    cprintf("%d", p->mlfq.executedCycleNumber);
    intToStr(p->mlfq.executedCycleNumber,buf,0);
    size = strlen(buf);
    for (int i = 0; i < 7 - size; i++)
      cprintf(" ");
    struct rtcdate currentTime;
    int waitingTime;
    float HRRN;
    cmostime(&currentTime);
    waitingTime = (currentTime.second - p->mlfq.arrivalTime.second) + (currentTime.minute - p->mlfq.arrivalTime.minute)*60 + (currentTime.hour - p->mlfq.arrivalTime.hour)*3600;
    HRRN = (float) waitingTime /(float) p->mlfq.executedCycleNumber;
    floatToStr(HRRN, 3, buf);
    cprintf("%s",buf);
    size = strlen(buf);
    for (int i = 0; i < 9 - size; i++)
      cprintf(" ");

    cprintf("%d:%d:%d", p->mlfq.arrivalTime.hour, p->mlfq.arrivalTime.minute,p->mlfq.arrivalTime.second);
    cprintf("\n");
  }

  
  return 0;
}
