#include "neo_threads.h"

#define TIME_SLICE (10U) // 1 second time slice to each thread

volatile uint32_t last_thread_start_tick;

volatile uint32_t last_running_thread_index = 0;
volatile uint32_t curr_running_thread_index = 0;

volatile uint32_t is_first_time = 1;

volatile uint32_t has_threads_started = 0;

#define MAX_THREADS (10U)
#define PROCESSOR_MODE_BIT (24U)
static neo_thread_t volatile *thread_queue[MAX_THREADS];
uint8_t thread_queue_len = 0;

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
        ".extern exit_from_interrupt_"
        "cpsid i \n"
        "ldr r0, =has_threads_started \n"
        "ldr r0, [r0] \n"
        "cpsie i \n"
        "cmp r0, 0 \n"
        "beq threads_not_started \n"
        "cpsie i \n"
        "ldr r0, =last_thread_start_tick \n" // Load address of last_thread_start_tick
        "ldr r2, [r0] \n"                    // Load last_thread_start_tick value; r1 still contains the current tick value
        "cpsie i \n"
        "sub r1, r1, r2 \n"                    // Subtract last_thread_start_tick from current tick value; r1 contains the time the thread has been running for;  we can still use this value as this will be the current value only
                                               // only the sys_tick handler can change the value of the tick_counter variable and since we're inside a systick handler, we can't be preempted by another one of the same type (same priority that is)
        "cmp r1, #10 \n"                     // Compare the difference with TIME_SLICE
        "ble thread_time_slice_not_expired \n" // If the difference is less than TIME_SLICE, jump to thread_time_slice_not_expired
        "bl neo_thread_scheduler \n"           // Call the scheduler
        "b neo_context_switch \n"              // switch the stack frames from old thread to new thread
        "switch: \n"                           // stack frame switched; we are in the stack frame of the new thread
        "threads_not_started: \n"
        "thread_time_slice_not_expired: \n"
        "b exit_from_interrupt_ \n"); // simply return from the handler
}

__attribute__((naked)) void neo_context_switch(void)
{
    __asm__ volatile(
        // Disable interrupts
        "cpsid i \n"

        // Check if thread queue is empty
        "ldr r0, =thread_queue_len \n"
        "ldr r0, [r0] \n"
        "cmp r0, #0 \n"
        "beq 2f \n" // Jump to enable_irq if queue empty

        "ldr r0, =is_first_time \n"
        "ldr r0, [r0] \n"
        "cmp r0, 1 \n"
        "beq first_time \n"
        // Save current stack pointer
        "ldr r0, =thread_queue \n"
        "ldr r0, [r0] \n"
        "ldr r1, =last_running_thread_index \n"
        "ldr r1, [r1] \n"
        "mov r2, %[thread_size] \n"
        "mul r1, r1, r2 \n"
        "add r0, r0, r1 \n"
        "str sp, [r0] \n"

        "first_time: \n"
        // Load new stack pointer
        "ldr r0, =thread_queue \n"
        "ldr r0, [r0] \n"
        "ldr r1, =curr_running_thread_index \n"
        "ldr r1, [r1] \n"
        "mul r1, r1, r2 \n"
        "add r0, r0, r1 \n"
        "ldr sp, [r0] \n"

        // Enable interrupts and return
        "2: \n"
        "cpsie i \n"
        "b switch \n"
        :
        : [thread_size] "i"(sizeof(neo_thread_t))
        : "r0", "r1", "r2", "memory");
}

// __attribute__((naked)) means no stack usage and only scratch registers used

__attribute__((naked)) void neo_thread_scheduler(void)
{
    __disable_irq(); // accessing shared variables
    if (!thread_queue_len)
    {
        __enable_irq();
        __asm__ volatile("b neo_thread_scheduler_return");
    }

    last_running_thread_index = curr_running_thread_index;
    curr_running_thread_index = (curr_running_thread_index + 1) & (MAX_THREADS - 1);
    if (curr_running_thread_index >= thread_queue_len)
    {
        curr_running_thread_index = 0;
    }
    last_thread_start_tick = tick_count;
    __enable_irq();

    __asm__ volatile("neo_thread_scheduler_return: \n"
                     "bx lr \n");
}

bool neo_thread_init(neo_thread_t *thread, void (*thread_function)(void *), void *thread_arg, uint8_t *stack, uint32_t stack_size) // stack_size is in bytes
{
    if (!thread || !thread_function || !stack)
    {
        return false;
    }

    // the thread function should NEVER return

    __disable_irq();
    if (thread_queue_len >= MAX_THREADS)
    {
        return false;
    }

    thread_queue[thread_queue_len++] = thread;
    __enable_irq();

    thread->stack_ptr = (uint8_t *)(((uintptr_t)stack + stack_size) & 0xFFFFFFF8U); // downward alignment of stack ptr to 8 bytes (required by AAPCS standard and execution stack)
    uint32_t *ptr = (uint32_t *)thread->stack_ptr;

    /*
     * Stack frame setup for context switching:
     *
     * When an exception occurs, ARM automatically pushes core registers onto the stack.
     * We manually pre-initialize this "exception stack frame" to control where the thread starts.
     *
     * Stack grows downward, so we push in reverse order of exception entry:
     *
     * Lower addr
     * R4-R11: Preserved registers not auto-saved
     * R0: thread_arg (parameter for thread_function)
     * R1-R3: Dummy values (would be parameters)
     * R12: Scratch register
     * LR: Return address (set to a cleanup function)
     * PC: thread_function (where execution starts)
     * xPSR: Program status (Thumb bit set)
     * Higher addr
     *
     * When the scheduler performs a context switch:
     * 1. It loads this pre-built stack frame using exception return
     * 2. CPU pops PC - switches to thread_function
     * 3. thread_function executes with thread_arg in R0
     * 4. If thread_function returns, LR points to cleanup
     */

    // Arm stack is a full stack and little endian (by default); stack_ptr points to the first byte (least significant byte) of the last item pushed onto the stack

    *(--ptr) = 0x01000000;                // xPSR (thumb bit set)
    *(--ptr) = (uint32_t)thread_function; // PC
    *(--ptr) = (uint32_t)0;               // LR; thread never returns so we don't bother with the LR of the thread function
    *(--ptr) = 0;                         // R12
    *(--ptr) = 0;                         // R3
    *(--ptr) = 0;                         // R2
    *(--ptr) = 0;                         // R1
    *(--ptr) = (uint32_t)thread_arg;      // R0 - First argument

    thread->stack_ptr = (uint8_t *)ptr;

    return true;
}

void neo_start_threads(void)
{
    has_threads_started = 1;
}