#include "types.h"
#include "stat.h"
#include "user.h"
char str[1024] = {0};
#define TEST_WOLFIE(addr, size)      \
    memset(str, 0, 1024);          \
    result = wolfie((addr), (size)); \
    printf(1, "wolfie(" #addr "=%p," #size ") == %d\n%s\n",(addr),(result),(addr));
int main(int argc, char *argv[])
{
    int result;
    TEST_WOLFIE(str, 1024);
    TEST_WOLFIE(str, 10);

    TEST_WOLFIE(str, 10240);
    TEST_WOLFIE(str, 192);
    TEST_WOLFIE(str, 193);
    exit();
}
