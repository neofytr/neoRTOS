#include "neo_threads.h"
#include "system_core.h"

#define TIME_SLICE (1000) // 1 second time slice to each thread

volatile uint32_t last_thread_start_tick;

void neo_kernel_init()
{
    setup_systick(100); // interrupt every 100 miliseconds
    // each tick count value represents 100 miliseconds
    // smallest unit of time measured by system is 100 miliseconds
}

__attribute__((naked)) void thread_handler(void) // naked function
{
    /* To call a function, first push lr, do bl function, then pop lr; in the function there should be a bx lr at the last */
    __disable_irq(); // disable interrupts since we are modifying global variables
    __asm__ volatile(
        "TIME_SLICE .equ 1000 \n"              // Define TIME_SLICE as 1000
        "ldr r0, =last_thread_start_tick \n"   // Load address of last_thread_start_tick
        "ldr r2, [r0] \n"                      // Load last_thread_start_tick value; r1 still contains the current tick value
        "sub r1, r1, r2 \n"                    // Subtract last_thread_start_tick from current tick value; r1 contains the time the thread has been running for
        "cmp r1, #TIME_SLICE \n"               // Compare the difference with TIME_SLICE
        "ble thread_time_slice_not_expired \n" // If the difference is less than TIME_SLICE, jump to thread_time_slice_not_expired
        "push {lr} \n"
        "bl neo_thread_scheduler \n" // Call the scheduler
        "pop {lr} \n"
        "thread_time_slice_not_expired: \n"
        "bx lr \n"); // simply return from the handler
    __enable_irq();  // enable interrupts since we are done modifying global variables
}

__attribute__((naked)) void neo_thread_scheduler(void)
{
}