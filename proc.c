#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#define min(a, b) ((a) < (b) ? (a) : (b))

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

  p->nice = 15;
  p->ctime = ticks;
  p->sstime = ticks;
  p->estime = ticks;
  p->rutime = 0;
  p->retime = 0;
  p->sltime = 0; 

  p->stacksz = KERNBASE - 2 * PGSIZE;

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

  changeprocstate(p, RUNNABLE);

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
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz, curproc->stacksz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
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

  np->nice = curproc->nice;

  acquire(&ptable.lock);

  changeprocstate(np, RUNNABLE);

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
  changeprocstate(curproc, ZOMBIE);
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
        cprintf("pid%d RUNNING ticks: %d\n", pid, p->rutime);
        cprintf("pid%d SLEEPING ticks: %d\n", pid, p->sltime);
        cprintf("pid%d RUNNABLE(wait) ticks: %d\n", pid, p->retime);
        cprintf("pid%d turnaround ticks: %d\n", pid, p->etime - p->ctime);
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

int nice(int pid, int inc)
{
  struct proc *currproc = myproc();
  // struct cpu *currcpu = mycpu();
  struct proc *p = (void *)0;
  struct proc *pp;
  acquire(&ptable.lock);

  for(pp = ptable.proc; pp < &ptable.proc[NPROC]; pp++)
  {
    if (pp->pid == pid)
    {
      p = pp;
      break;
    }
  }

  // process not found, fail
  if (!p)
  {
    cprintf("nice(): process %d not found\n", pid);
    release(&ptable.lock);
    return -1;
  }

  if (inc == 0)
  {
    release(&ptable.lock);
    return p->nice;
  }

  // change the process's nice value
  if (p->nice + inc > 31)
  {
    p->nice = 31;
  }
  else if (p->nice + inc < 0)
  {
    p->nice = 0;
  }
  else
  {
    p->nice += inc;
  }

  // if the priority becomes lower than any process on the ready list, switch to that process
  int min_nice = currproc->nice;
  if (p->state == RUNNABLE)
  {
    for(pp = ptable.proc; pp < &ptable.proc[NPROC]; pp++)
    {
      if (pp != p && pp->state == RUNNABLE)
      {
        min_nice = min(min_nice, pp->nice);
      }
    }
    if (p->nice < min_nice)
    {
      cprintf("nice(): %d -> priority of process %d becomes lower than any process on the ready list\n", currproc->pid, p->pid);
      changeprocstate(currproc, RUNNABLE);
      sched();
    }
  }
  else
  {
    cprintf("nice(): process %d is not RUNNABLE, no need to switch\n",pid);
  }
  cprintf("nice(): %d calls nice(pid=%d, inc=%d) = %d\n", currproc->pid, pid, inc, p->nice);
  
  release(&ptable.lock);
  return p->nice;
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct proc *pp;
  struct cpu *c = mycpu();
  double cur_nice;
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();
    if (c->apicid != 0) continue; // lock other cpu for debug
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    double min_nice = 32.0;
    pp = (void *)0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if(p->state != RUNNABLE)
        continue;
      cur_nice = (double)p->nice - (double)(ticks - p->sstime) / 20.0;
      if (cur_nice < min_nice)
      {
        min_nice = cur_nice;
        pp = p;
      }
    }
    if (min_nice < 32)
    {
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      // cprintf("scheduler() on cpu%d: run %d\n", c->apicid, pp->pid); // debug
      c->proc = pp;
      switchuvm(pp);
      changeprocstate(pp, RUNNING);

      swtch(&(c->scheduler), pp->context);
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

void 
changeprocstate(struct proc *p, int to)
{
  struct proc *curproc = p;
  int from = curproc->state;
  if(from == SLEEPING)
  {
    if(to == SLEEPING)
      return;
    curproc->estime = ticks;
    curproc->sltime += (curproc->estime - curproc->sstime);
    curproc->sstime = curproc->estime;
    if(to == RUNNING)
    {
      panic("changeprocstate(): error state SLEEPING to RUNNING\n");
    }
  }
  else if(from == RUNNABLE)
  {
    if(to == RUNNABLE)
      return;
    curproc->estime = ticks;
    curproc->retime += (curproc->estime - curproc->sstime);
    curproc->sstime = curproc->estime;
    if(to == SLEEPING)
    {
      panic("changeprocstate(): error state RUNNABLE to SLEEPING");
    }
  }
  else if(from == RUNNING)
  {
    if(to == RUNNING)
      return;
    curproc->estime = ticks;
    curproc->rutime += (curproc->estime - curproc->sstime);
    curproc->sstime = curproc->estime;
    }
    else{
      curproc->sstime = ticks;
    }
  if(to == ZOMBIE)
  {
    curproc->etime = ticks;
  }
  curproc->state = to;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  changeprocstate(myproc(), RUNNABLE);
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
  changeprocstate(p, SLEEPING);

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
      changeprocstate(p, RUNNABLE);
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
        changeprocstate(p, RUNNABLE);
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

struct mutex
{
  int value;
  struct spinlock lock;
  struct proc *queue[NPROC];
  struct proc *curproc;
  int curnice, orinice;
  int start;
  int end;
  int used;
};
struct mutex mutexs[100];

int mtxget(void)
{
  struct mutex *s;
  for (int i = 0; i < 100; i++)
  {
    s = &mutexs[i];
    if (!s->used)
    {
      s->value = 1;
      initlock(&s->lock, "mutex_lock");
      s->end = s->start = 0;
      s->used = 1;
      return i;
    }
  }
  return -1;
}

int mtxacq(int n)
{
  if (n < 0 || n > 100 || !mutexs[n].used)
  {
    return -1;
  }
  struct mutex *s = &mutexs[n];
  acquire(&s->lock);
  s->value--;
  if (s->value < 0)
  {
    s->queue[s->end] = myproc();
    s->end = (s->end + 1) % NPROC;
    if (s->curnice > myproc()->nice)
    {
      nice(s->curproc->pid, myproc()->nice - s->curnice);
      s->curnice = s->curproc->nice;
      cprintf("priority donate %d nice to %d\n", s->curproc->pid, s->curnice);
    }
    sleep(myproc(), &s->lock);
  }
  s->curproc = myproc();
  s->curnice = myproc()->nice;
  s->orinice = s->curnice;
  // cprintf("%d %d %d\n",s->curproc->pid,s->curnice,s->orinice);
  
  release(&s->lock);
  return 0;
}

int mtxrel(int n)
{
  if (n < 0 || n > 100 || !mutexs[n].used)
  {
    return -1;
  }
  struct mutex *s = &mutexs[n];
  acquire(&s->lock);
  s->value++;
  if (s->value <= 0)
  {
    wakeup(s->queue[s->start]);
    s->queue[s->start] = 0;
    s->start = (s->start + 1) % NPROC;
  }
  //cprintf("%d %d %d\n",s->curproc->pid, s->curnice, s->orinice);
  if (s->orinice - s->curnice > 0)
  {
    nice(s->curproc->pid, s->orinice - s->curnice);
    s->curnice = s->orinice;
    cprintf("priority drop %d original nice %d\n", s->curproc->pid, s->curnice);
  }

  release(&s->lock);
  return 0;
}

int mtxdel(int n)
{
  if (n < 0 || n > 100 || !mutexs[n].used)
  {
    return -1;
  }
  struct mutex *s = &mutexs[n];
  memset(s, 0, sizeof(struct mutex));
  return 0;
}
