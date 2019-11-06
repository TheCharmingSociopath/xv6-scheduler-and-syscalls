#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char **argv)
{
    if(argc < 3)
        printf(1, "invalid use of set_priority");
    else{
        int pid = atoi(argv[1]);
        int priority = atoi(argv[2]);
        set_priority(pid, priority);
    }
  exit();
}
