#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[])
{
    // while (1);
    int pid, n = 1, mypid = getpid(); // n: Number of children, g = generator
    double g = 10;

    if (argc != 3)
    {
        printf(2, "Incorrect Usage. \n Use as: %s n speed\n", argv[0]);
        exit();
    }
    n = atoi(argv[1]);
    g = atoi(argv[2]) / 10;

    for (int i = 0; i < n; ++i)
    {
        // printf(1, "grdgrs\n");
        int x = 0;
        if ((pid = fork()) < 0)
            printf(1, "%d failed in fork\n", mypid);
        else if (pid > 0)
        {
            for (double j = 0; j < 1000000.0; j += g)
                x += (0.069 * 69.69);
            printf(1, "Parent %d creating a child %d.\n", mypid, pid);
            set_priority(pid, (pid * 25) % 30 + 50);
        }
        else
        {
            for (double j = 0; j < 8000000.0; j += g)
                x += (0.069 * 69.69);
            printf(1, "Child %d ended.\n", getpid());
            break;
        }
    }
    exit();
}
