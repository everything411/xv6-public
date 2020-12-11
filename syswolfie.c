#include "types.h"
#include "defs.h"
#include "x86.h"
#include "syscall.h"

const char wolfie_data[] = 
" __          __   _  __ _      \n"
" \\ \\        / /  | |/ _(_)     \n"
"  \\ \\  /\\  / /__ | | |_ _  ___ \n"
"   \\ \\/  \\/ / _ \\| |  _| |/ _ \\\n"
"    \\  /\\  / (_) | | | | |  __/\n"
"     \\/  \\/ \\___/|_|_| |_|\\___|\n";

int sys_wolfie(void)
{
    char *buf;
    int n;
    if (argint(1, &n) < 0 || argptr(0, &buf, n) < 0)
    {
        return -1;
    }
    int wolfie_len = sizeof(wolfie_data);
    if (n < wolfie_len)
    {
        return -1;
    }
    strncpy(buf, wolfie_data, wolfie_len);
    return wolfie_len;
}
