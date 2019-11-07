#include "param.h"

struct proc_stat {
    int inuse[NPROC]; // whether this slot of the process table is in use (1 or 0)
    int pid[NPROC];   // PID of each process
    int priority[NPROC]; // current priority level of each process (0-3)
    int ticks[NPROC][5]; // number of ticks each process has accumulated at each of 4 priorities
};
