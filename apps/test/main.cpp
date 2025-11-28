#include "../_header/syscall.hpp"

extern "C" void main()
{
    const char *msg = "Hello from User Mode!\n";

    for (int i = 0; msg[i] != '\0'; ++i)
    {
        PutChar(msg[i]);
    }

    Exit();
}