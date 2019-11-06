#include "types.h"
#include "stat.h"
#include "user.h"
 
int main(int argc, char *argv[])
{
    #ifdef PBS
    while (1);
    #else

    #ifdef FCFS
    // sleep(10);
    while(1);
    #else

    #endif
    #endif
    exit();
}
