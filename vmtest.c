#include "types.h"
#include "stat.h"
#include "user.h"
char data[0x1000];
void f(int n)
{
    int m;
    if (n % 100 == 0)
    {
        printf(1, "recursive test %d: %p\n", n, &m);
    }
    if (n != 10000)
    {
        f(n + 1);
    }
    else
    {
        m = fork();
        if (m)
        {
            wait();
        }
        else
        {
            // trap
            printf(1, "%s\n", (uint)&m - 0x1000 * 8);
        }
    }
}
void f2(int n)
{
    char m[0x100];
    if (n % 100 == 0)
    {
        printf(1, "recursive test %d: %p\n", n, &m);
    }
    f2(n + 1);
}
void f3()
{
    void *p = malloc(0x1000000);
    printf(1, "p=%p\n", p + 0x1000000);
    int m;
    uint addr = (uint)&m;
    uint addr2 = addr;
    for (int i = 0; ; i++)
    {
        if (i % 100 == 0)
        {
            printf(1, "%p = %d stack=%x\n", addr, *(char *)addr, addr2 - addr);
        }
        else
        {
            printf(1, "", *(char *)addr);
        }
        addr -= 0x1000;
    } 
}
void f4()
{
    char *s;
    for (int i = 0; ; i++)
    {
        s = sbrk(0x1000);
        if (i % 100 == 0)
        {
            printf(1, "sbrk() = %p\n", s);
        }
    } 
    
}
int main(int argc, char *argv[])
{
    uint m = 0xdeadbeef;
    void *heap = malloc(0x1000);
    printf(1, "main is %p\n", main);
    printf(1, "data is %p\n", data);
    printf(1, "stack is %p\n", &m);
    printf(1, "heap is %p\n", heap);
    f(0);
    // f2(0);
    // f3();
    // f4();
    // for (uint i = 0; ; i++)
    // {
    //     printf(1, "%p ", (&m)[i]);
    // }
    
    ((void (*)())0x7ffff000)();
    // ((void (*)())0xffffffff)();
    // no exit here, cause trap at 0xffffffff
    exit();
}
