#include "../_header/syscall.hpp"

extern "C" int main(int argc, char **argv)
{
    const char *msg = "Standard I/O Test App\nType something and press Enter "
                      "(or just type):\n";
    int len = 0;
    while (msg[len])
        len++;
    Write(1, msg, len);

    /*
    char buf[128];
    while (true)
    {
        int read_len = Read(0, buf, 128);
        if (read_len > 0)
        {
            // Echo back
            Write(1, buf, read_len);

            // If Enter (\n) is detected, maybe print a prompt?
            // For now just raw echo.
        }

        // Simple exit condition: 'q' at start
        if (read_len > 0 && buf[0] == 'q')
            break;
    }
    */

    const char *exit_msg = "\nExiting...\n";
    len = 0;
    while (exit_msg[len])
        len++;
    Write(1, exit_msg, len);

    Exit();
    return 0;
}
