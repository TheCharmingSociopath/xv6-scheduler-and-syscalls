#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

int main(int argc, char *argv[])
{

    int pid;
    int status = 0, a = 4, b = 6;
    pid = fork();
    if (pid == 0)
    {
        char *com = argv[1];
        char **ar = argv + 1;
        exec(com, ar);
        printf(1, "exec %s failed\n", argv[1]);
    }
    else
    {
        status = waitx(&a, &b);
    }
    printf(1, "\nWait Time = %d\n Run Time = %d\n with Status %d \n", a, b, status);
    exit();
}
