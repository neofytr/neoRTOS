#include "neo_threads.h"

#define TIME_SLICE (1000) // 1 second time slice to each thread

volatile uint32_t last_thread_start_tick;
volatile uint32_t curr_running_thread_index = 0;

#define MAX_THREADS (10)
static neo_thread_t volatile *thread_queue[MAX_THREADS];
uint8_t thread_queue_index = 0;

void neo_kernel_init()
{
    setup_systick(100); // interrupt every 100 miliseconds
    // each tick count value represents 100 miliseconds
    // smallest unit of time measured by system is 100 miliseconds
}

__attribute__((naked)) void thread_handler(void) // naked function; interrupts are disabled before calling this function and enabled before returning from systick
{
    /* To call a function, first push lr, do bl function, then pop lr; in the function there should be a bx lr at the last */
    __asm__ volatile(
        "cpsid i \n"
        "ldr r0, =last_thread_start_tick \n" // Load address of last_thread_start_tick
        "ldr r2, [r0] \n"                    // Load last_thread_start_tick value; r1 still contains the current tick value
        "cpsie i \n"
        "sub r1, r1, r2 \n"                    // Subtract last_thread_start_tick from current tick value; r1 contains the time the thread has been running for
        "cmp r1, #1000 \n"                     // Compare the difference with TIME_SLICE
        "ble thread_time_slice_not_expired \n" // If the difference is less than TIME_SLICE, jump to thread_time_slice_not_expired
        "push {lr} \n"
        "bl neo_thread_scheduler \n" // Call the scheduler
        "pop {lr} \n"
        "thread_time_slice_not_expired: \n"
        "bx lr \n"); // simply return from the handler
}

__attribute__((naked)) void neo_thread_scheduler(void)
{
    if (!thread_queue_index)
    {
        __asm__volatile("b neo_thread_scheduler_return");
    }

    __disable_irq(); // accessing shared variables
    curr_running_thread_index = (curr_running_thread_index + 1) & (MAX_THREADS - 1);
    last_thread_start_tick = tick_count;
    __enable_irq();

    __asm__ volatile("neo_thread_scheduler_return: \n"
                     "bx lr \n");
}