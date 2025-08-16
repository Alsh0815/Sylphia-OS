extern "C" void __cxa_pure_virtual()
{
    while (1)
    {
        asm volatile("cli; hlt");
    }
}