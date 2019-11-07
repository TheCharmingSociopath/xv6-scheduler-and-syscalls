#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "proc_stat.h"

struct proc *q0[NPROC], *q1[NPROC], *q2[NPROC], *q3[NPROC], *q4[NPROC];
int c0 = -1, c1 = -1, c2 = -1, c3 = -1, c4 = -1;
int clkPerPrio[5] = {1, 2, 4, 8, 16};
struct proc_stat pstat_var;

struct
{
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int cpuid()
{
  return mycpu() - cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu *
mycpu(void)
{
  int apicid, i;

  if (readeflags() & FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i)
  {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc *
myproc(void)
{
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
static struct proc *
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == UNUSED)
      goto found;
#ifdef MLFQ
  p->priority = 0;
  p->clicks = 0;
  q0[++c0] = p;
  pstat_var.priority[p->pid] = 0;
  for (int j = 0; j < 4; j++)
    pstat_var.ticks[p->pid][j] = 0;
  pstat_var.pid[p->pid] = p->pid;
#endif
#ifdef PBS
  p->priority = 60;
#endif
  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
#ifdef PBS
  p->priority = 60;
#endif
#ifdef MLFQ
  p->priority = 0;
#endif
  p->clicks = 0;
  q0[++c0] = p;
  pstat_var.priority[p->pid] = p->priority;
  for (int j = 0; j < 4; j++)
    pstat_var.ticks[p->pid][j] = 0;
  pstat_var.pid[p->pid] = p->pid;
  release(&ptable.lock);

  // Allocate kernel stack.
  if ((p->kstack = kalloc()) == 0)
  {
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe *)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint *)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context *)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;
  p->ctime = ticks;
  p->etime = 0;
  p->rtime = 0;
  p->iotime = 0;
  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  acquire(&ptable.lock);
  initproc = p;
  if ((p->pgdir = setupkvm()) == 0)
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
  p->tf->eip = 0; // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if (n > 0)
  {
    if ((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  else if (n < 0)
  {
    if ((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }

  // Copy process state from proc.
  if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0)
  {
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

  for (i = 0; i < NOFILE; i++)
    if (curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);
  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void exit(void)
{
  struct proc *curproc = myproc(), *p;
  int fd;

  if (curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for (fd = 0; fd < NOFILE; fd++)
  {
    if (curproc->ofile[fd])
    {
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
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->parent == curproc)
    {
      p->parent = initproc;
      if (p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  curproc->etime = ticks;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->parent != curproc)
        continue;
      havekids = 1;
      if (p->state == ZOMBIE)
      {
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
    if (!havekids || curproc->killed)
    {
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock); //DOC: wait-sleep
  }
}

// waitx updates rtime and wtime
// Return -1 if this process has no children.
int waitx(int *wtime, int *rtime)
{
  struct proc *p, *curproc = myproc();
  int havekids, pid;

  acquire(&ptable.lock);
  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->parent != curproc)
        continue;
      havekids = 1;
      if (p->state == ZOMBIE)
      {
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
        *rtime = p->rtime;
        *wtime = p->etime - p->ctime - p->rtime;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || curproc->killed)
    {
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock); //DOC: wait-sleep
  }
}

int set_priority(int pid, int priority)
{

  struct proc *p;
  int oldPriority = 100;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      oldPriority = p->priority;
      p->priority = priority;
    }
  }
  return oldPriority;
}

int getpinfo(int pid, struct proc_stat *ps)
{
  // struct proc_stat p;
  // for (p = pstat_var; p < &ptable.proc[NPROC]; p++)
  // {
  //   ps->pid[pid] = pid;
  //   ps->priority[pid] = pstat_var.priority[pid];
  //   for (int i = 0; i < 4; i++)
  //   {
  //     ps->ticks[pid][i] = pstat_var.ticks[pid][i];
  //   }
  // }
  cprintf("%s\t%s\t%s\t%s\t%s\t%s\t%s\n", "PID", "Ticks0", "Ticks1", "Ticks2", "Ticks3", "Ticks4", "Priority");
  cprintf("%d\t%d\t%d\t%d\t%d\t%d\t%d\n", pstat_var.pid[pid], pstat_var.ticks[pid][0], pstat_var.ticks[pid][1], pstat_var.ticks[pid][2],
          pstat_var.ticks[pid][3], pstat_var.ticks[pid][4], pstat_var.priority[pid]);
  return 0;
}

int ps(void)
{
  static char *states[] = {
      [UNUSED] "unused  ",
      [EMBRYO] "embryo  ",
      [SLEEPING] "sleeping",
      [RUNNABLE] "runnable",
      [RUNNING] "running ",
      [ZOMBIE] "zombie  "};
  struct proc *p;
  cprintf("%s\t%s\t%s\t%s\t\t%s\n", "Name", "PID", "State", "Creation Time", "Priority");
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid <= 0)
      continue;
    cprintf("%s\t%d\t%s\t%d\t\t%d\n", p->name, p->pid, states[p->state], p->ctime, p->priority);
  }
  return 0;
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.

void scheduler(void)
{

  struct proc *p;
  // struct proc *currproc = myproc();
  struct cpu *c = mycpu();
  // c->proc = 0;
  for (;;)
  {
    // Enable interrupts on this processor.
    sti();
    // Loop over process table looking for process to run.

#ifdef DEFAULT // Round Robin
    acquire(&ptable.lock);
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
#else
#ifdef FCFS
    acquire(&ptable.lock);
    struct proc *minP = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      // if (p->pid < 3)
      //   continue;
      if (p->state == RUNNABLE)
      {
        if (minP != 0)
        {
          if (p->ctime < minP->ctime)
            minP = p;
        }
        else
          minP = p;
      }
    }
    if (minP != 0)
    {
      p = minP; //the process with the smallest creation time
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
      swtch(&c->scheduler, p->context);
      switchkvm();
      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
#else
#ifdef PBS
    acquire(&ptable.lock);
    int min_priority = 101;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->state != RUNNABLE)
        continue;
      if (p->priority < min_priority)
        min_priority = p->priority;
    }
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->state != RUNNABLE)
        continue;
      if (p->priority != min_priority)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&c->scheduler, p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
#else
#ifdef MLFQ
    int i, j;
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    // struct proc *proc;
    for (i = 0; i <= c0; i++)
    {
      if (q0[i]->state != RUNNABLE)
        continue;
      p = q0[i];
      p->clicks++;
      c->proc = q0[i];
      switchuvm(p);
      p->state = RUNNING;
      swtch(&c->scheduler, p->context);
      switchkvm();
      pstat_var.ticks[p->pid][0] = p->clicks;
      if (p->clicks == clkPerPrio[0])
      {
        /*copy proc to lower priority queue*/
        p->priority = pstat_var.priority[p->pid] = 1;
        q1[++c1] = p;

        /*delete proc from q0*/
        q0[i] = 0;
        for (j = i; j < c0; j++)
          q0[j] = q0[j + 1];
        q0[c0--] = 0;
        p->clicks = 0;
      }
      c->proc = 0;
    }
    for (i = 0; i <= c1; i++)
    {
      if (q1[i]->state != RUNNABLE)
        continue;

      p = q1[i];
      p->clicks++;
      c->proc = q1[i];
      switchuvm(p);
      p->state = RUNNING;
      swtch(&c->scheduler, p->context);
      switchkvm();
      pstat_var.ticks[p->pid][1] = p->clicks;
      ;
      if (p->clicks == clkPerPrio[1])
      {
        /*copy proc to lower priority queue*/
        p->priority = pstat_var.priority[p->pid] = 2;
        q2[++c2] = p;

        /*delete proc from q1*/
        q1[i] = 0;
        for (j = i; j < c1; j++)
          q1[j] = q1[j + 1];
        q1[c1--] = 0;
        p->clicks = 0;
      }
      c->proc = 0;
    }
    for (i = 0; i <= c2; i++)
    {
      if (q2[i]->state != RUNNABLE)
        continue;

      p = q2[i];
      c->proc = q2[i];
      p->clicks++;
      switchuvm(p);
      p->state = RUNNING;
      swtch(&c->scheduler, p->context);
      switchkvm();
      pstat_var.ticks[p->pid][2] = p->clicks;
      ;
      if (p->clicks == clkPerPrio[2])
      {
        /*copy proc to lower priority queue*/
        p->priority = pstat_var.priority[p->pid] = 3;
        q3[++c3] = p;

        /*delete proc from q2*/
        q2[i] = 0;
        for (j = i; j < c2; j++)
          q2[j] = q2[j + 1];
        q2[c2--] = 0;
        p->clicks = 0;
      }
      c->proc = 0;
    }
    for (i = 0; i <= c3; i++)
    {
      if (q3[i]->state != RUNNABLE)
        continue;

      p = q3[i];
      c->proc = q3[i];
      p->clicks++;
      switchuvm(p);
      p->state = RUNNING;
      swtch(&c->scheduler, p->context);
      switchkvm();
      pstat_var.ticks[p->pid][3] = p->clicks;
      if (p->clicks == clkPerPrio[3])
      {
        /*copy proc to lower priority queue*/
        p->priority = 4;
        pstat_var.priority[p->pid] = 4;
        q4[++c4] = p;

        /*delete proc from q3*/
        q3[i] = 0;
        for (j = i; j < c3; j++)
          q3[j] = q3[j + 1];
        q3[c3--] = 0;
        p->clicks = 0;
      }
      c->proc = 0;
    }
    for (i = 0; i <= c4; i++)
    {
      if (q4[i]->state != RUNNABLE)
        continue;

      p = q4[i];
      c->proc = q4[i];
      p->clicks++;
      switchuvm(p);
      p->state = RUNNING;
      swtch(&c->scheduler, p->context);
      switchkvm();
      pstat_var.priority[p->pid] = p->priority;
      pstat_var.ticks[p->pid][4] = p->clicks;
      /*move process to end of its own queue */
      q4[i] = 0;
      for (j = i; j < c4; j++)
        q4[j] = q4[j + 1];
      q4[c4] = p;
      c->proc = 0;
    }
    release(&ptable.lock);
#else
#endif
#endif
#endif
#endif
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&ptable.lock))
    panic("sched ptable.lock");
  if (mycpu()->ncli != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched running");
  if (readeflags() & FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
  acquire(&ptable.lock); //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first)
  {
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
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if (p == 0)
    panic("sleep");

  if (lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if (lk != &ptable.lock)
  {                        //DOC: sleeplock0
    acquire(&ptable.lock); //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if (lk != &ptable.lock)
  { //DOC: sleeplock2
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
#ifdef PBS
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
#else
#ifdef FCFS
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
#else
#ifdef DEFAULT
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
#else
#ifdef MLFQ
  int i;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state == SLEEPING && p->chan == chan)
    {
      p->clicks = 0;
      p->state = RUNNABLE;
      if (p->priority == 0)
      {
        for (i = ++c0; i > 0; --i)
        {
          q0[i] = q0[i - 1];
        }
        q0[0] = p;
      }
      else if (p->priority == 1)
      {
        for (i = ++c1; i > 0; --i)
        {
          q1[i] = q1[i - 1];
        }
        q1[0] = p;
      }
      else if (p->priority == 2)
      {
        for (i = ++c2; i > 0; --i)
        {
          q2[i] = q2[i - 1];
        }
        q2[0] = p;
      }
      else if (p->priority == 3)
      {
        for (i = ++c3; i > 0; --i)
        {
          q3[i] = q3[i - 1];
        }
        q3[0] = p;
      }
      else
      {
        for (i = ++c4; i > 0; --i)
        {
          q4[i] = q4[i - 1];
        }
        q4[0] = p;
      }

      for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
        if (p->state == SLEEPING && p->chan == chan)
          p->state = RUNNABLE;
    }
  }
#else
#endif
#endif
#endif
#endif
}

// Wake up all processes sleeping on chan.
void wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      p->killed = 1;
      // Wake process from sleep if necessary.
      if (p->state == SLEEPING)
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
void procdump(void)
{
  static char *states[] = {
      [UNUSED] "unused",
      [EMBRYO] "embryo",
      [SLEEPING] "sleep ",
      [RUNNABLE] "runble",
      [RUNNING] "run   ",
      [ZOMBIE] "zombie"};
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if (p->state == SLEEPING)
    {
      getcallerpcs((uint *)p->context->ebp + 2, pc);
      for (i = 0; i < 10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
