#include "param.h"

struct proc_stat {
    int pid[NPROC];   // PID of each process
    float runtime[NPROC]; // Use suitable unit of time
    int num_run[NPROC]; // number of time the process is executed
    int priority[NPROC]; // current priority level of each process (0-4)
    int ticks[NPROC][5]; // number of ticks each process has received at each of the 5 priority queues
};