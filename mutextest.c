#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char const *argv[])
{
    int n = mtxget();
    int pid = fork();
    if (pid == 0)
    {
        printf(1, "infinate loop start with nice %d!\n", nice(getpid(), 0));
        for (;;)
        {
            printf(1, "");
        }
        exit();
    }
    pid = fork();
    if (pid)
    {
        pid = getpid();
        nice(pid, 10);
        printf(1, "parent try to acquire mutex!\n");
        mtxacq(n);
        printf(1, "parent acquire mutex with nice %d!\n", nice(getpid(), 0));
        for (int i = 0; i < 100000000; i++)
        {
            printf(1, "");
        }
        printf(1, "parent release mutex!\n");
        mtxrel(n);
        wait();
        mtxdel(n);
    }
    else
    {
        sleep(1);
        pid = getpid();
        printf(1, "child try to acquire mutex with nice %d!\n", nice(getpid(), 0));
        mtxacq(n);
        printf(1, "child acquire mutex!\n");
        printf(1, "child release mutex!\n");
        mtxrel(n);
        printf(1, "child complete\n");
    }
    exit();
}
