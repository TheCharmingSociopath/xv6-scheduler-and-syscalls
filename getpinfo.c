#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char **argv)
{
    if (argc != 2)
        printf(1, "invalid use of getpinfo");
    else
    {
        int pid = atoi(argv[1]);
        struct proc_stat pstat;
        getpinfo(pid, &pstat);
        // printf(1, "PID: %d\nTicks0: %d\nTicks1: %d\nTicks2: %d\nTicks3: %d\nTicks4: %d\nPriority: %d\n",
        //        *pstat.pid, *pstat.ticks[0], *pstat.ticks[1], *pstat.ticks[2], *pstat.ticks[3],
        //        *pstat.ticks[4], *pstat.priority);
    }
    exit();
}
