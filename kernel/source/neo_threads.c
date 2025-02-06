#include "neo_threads.h"
#include "system_core.h"

void neo_kernel_init()
{
    setup_systick(1);
}

__attribute__((naked)) void thread_handler(void) // naked function
{
    /* To call a function, first push lr, do bl function, then pop lr; in the function there should be a bx lr at the last */
    __asm__ volatile("nop \n nop \n bx lr \n"); // simply return from the handler
}