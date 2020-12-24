#include "types.h"
#include "stat.h"
#include "user.h"
char buffer[1024] = {0};
void test_nice()
{
    int pid = getpid();
    int ni = nice(pid, 0);
    printf(1, "%d\n", ni);

    ni = nice(pid, 10);
    printf(1, "%d\n", ni);

    ni = nice(pid, 10);
    printf(1, "%d\n", ni);

    ni = nice(pid, -50);
    printf(1, "%d\n", ni);
    ni = nice(pid, 10);
    printf(1, "%d\n", ni);
}
#define PROCNUM 5
int main(int argc, char *argv[])
{
    test_nice();
    int str_length;
    int curpid = getpid();;
    for (int i = 0; i < PROCNUM; i++)
    {
        int pid = fork();
        // child
        if (pid == 0)
        {
            curpid = getpid();

            str_length = sprintf(buffer, "child pid %d start to run\n", curpid);
            write(1, buffer, str_length);

            for (int j = 1; j <= 100000000; j++)
            {
                printf(1, ""); // to avoid optimizing
            }
            exit();
        }
        // father
        else
        {
            str_length = sprintf(buffer, "%d forked process %d\n", curpid, pid);
            write(1, buffer, str_length);
            // int ni = nice(pid, pid % 10);
            int ni = nice(pid, 5);
            str_length = sprintf(buffer, "nice() = %d\n", ni);
            write(1, buffer, str_length);
        }
    }
    for (int i = 0; i < PROCNUM; i++)
    {
        wait();
    }
    exit();
}
