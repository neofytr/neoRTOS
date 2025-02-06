#include "neo_threads.h"

#define TIME_SLICE (10U) // 1 second time slice to each thread

volatile uint32_t last_thread_start_tick;

volatile uint32_t last_running_thread_index = 0;
volatile uint32_t curr_running_thread_index = 0;

volatile uint32_t is_first_time = 1;

volatile uint32_t has_threads_started = 0;

#define MAX_THREADS (10U)
#define PROCESSOR_MODE_BIT (24U)
neo_thread_t volatile *thread_queue[MAX_THREADS];
uint32_t thread_queue_len = 0;

void neo_kernel_init()
{
    setup_systick(100); // interrupt every 100 miliseconds
    // each tick count value represents 100 miliseconds
    // smallest unit of time measured by system is 100 miliseconds
    NVIC_EnableIRQ(PendSV_IRQn);
}

__attribute__((naked)) void thread_handler(void)
{
    __asm__ volatile(
        ".extern exit_from_interrupt_" // external symbol
        "cpsid i \n"
        "ldr r0, =has_threads_started \n"
        "ldr r0, [r0] \n"
        "cpsie i \n"
        "cmp r0, #0 \n"
        "beq threads_not_started \n"

        "cpsid i \n"
        "ldr r0, =is_first_time \n"
        "ldr r0, [r0] \n"
        "cpsie i \n"
        "cmp r0, #1 \n"
        "beq first_time_thread_handler \n"

        "cpsid i \n"
        "ldr r1, =tick_count \n"
        "ldr r1, [r1] \n"
        "ldr r0, =last_thread_start_tick \n"
        "ldr r2, [r0] \n"
        "cpsie i \n"
        "sub r1, r1, r2 \n"
        "cmp r1, #10 \n"
        "blt thread_time_slice_not_expired \n"

        "first_time_thread_handler:   \n");

#define PendSV_Interrupt_Number (14U)

    // Writing 1 to this bit is the only way to set the PendSV exception state to pending.
    SCB->ICSR |= (1U << (2 * PendSV_Interrupt_Number)); // trigger PendSV exception

#undef PendSV_Interrupt_Number

    __asm__ volatile(
        "threads_not_started: \n"
        "thread_time_slice_not_expired: \n"
        "b exit_from_interrupt_ \n");
}

__attribute__((naked)) void PendSV_handler(void)
{
    __asm__ volatile(
        "cpsid i \n"
        "ldr r0, =is_first_time \n"
        "ldr r0, [r0] \n"
        "cpsie i \n"
        "cmp r0, #1 \n"
        "beq skip_save \n"
        "push {r4-r11} \n"

        "skip_save: \n"
        "b neo_thread_scheduler \n"
        "return_from_scheduler: \n"
        "b neo_context_switch \n"

        "switch: \n"
        // After switch, restore R4-R11
        "pop {r4-r11} \n"

        "bx lr \n");
}

__attribute__((naked)) void neo_context_switch(void)
{
    __asm__ volatile(
        "cpsid i \n"

        "ldr r0, =thread_queue_len \n"
        "ldr r0, [r0] \n"
        "cmp r0, #0 \n"
        "beq 2f \n"

        "ldr r0, =is_first_time \n"
        "ldr r0, [r0] \n"
        "cmp r0, #1 \n"
        "beq first_time_switch \n"

        // Save current SP to thread_queue[last_running_thread_index]
        "ldr r0, =thread_queue \n"
        "ldr r1, =last_running_thread_index \n"
        "ldr r1, [r1] \n"
        "lsl r1, r1, #2 \n" // Multiply by 4 for pointer array index
        "add r0, r0, r1 \n"
        "ldr r0, [r0] \n" // Load the thread pointer
        "str sp, [r0] \n"

        "first_time_switch: \n"
        // Load SP from thread_queue[curr_running_thread_index]
        "ldr r0, =thread_queue \n"
        "ldr r1, =curr_running_thread_index \n"
        "ldr r1, [r1] \n"
        "lsl r1, r1, #2 \n" // Multiply by 4 for pointer array index
        "add r0, r0, r1 \n"
        "ldr r0, [r0] \n" // Load the thread pointer
        "ldr sp, [r0] \n"
        "ldr r0, =is_first_time \n"
        "mov r1, #0 \n"
        "str r1, [r0] \n"

        "2: \n"
        "cpsie i \n"
        "b switch \n" ::: "r0", "r1", "memory");
}

// __attribute__((naked)) means no stack usage and only scratch registers used

__attribute__((naked)) void neo_thread_scheduler(void)
{
    __asm volatile(
        // Disable interrupts
        "cpsid i                                \n\t"

        // Check if thread_queue_len is zero
        "ldr r0, =thread_queue_len             \n\t"
        "ldr r0, [r0]                          \n\t"
        "cmp r0, #0                            \n\t"
        "beq enable_and_return                 \n\t" // Jump to enable_and_return if zero

        // Check if is_first_time
        "ldr r0, =is_first_time                \n\t"
        "ldr r0, [r0]                          \n\t"
        "cmp r0, #0                            \n\t"
        "beq main_scheduling_logic             \n\t" // If not first time, continue to main logic

        // Handle first time case
        "ldr r0, =curr_running_thread_index    \n\t"
        "mov r1, #0                            \n\t"
        "str r1, [r0]                          \n\t"
        "b enable_and_return                   \n\t" // Jump to enable_and_return

        // Main scheduling logic
        "main_scheduling_logic:                 \n\t"
        "ldr r0, =curr_running_thread_index    \n\t"
        "ldr r1, [r0]                          \n\t"

        // Save last running thread index
        "ldr r2, =last_running_thread_index    \n\t"
        "str r1, [r2]                          \n\t"

        // Increment and mask current thread index
        "add r1, r1, #1                        \n\t"
        "mov r2, #10                  \n\t" // MAX_THREADS used here, change if ever change the macro MAX_THREADS
        "sub r2, r2, #1                        \n\t"
        "and r1, r1, r2                        \n\t"

        // Check if current index >= thread_queue_len
        "ldr r2, =thread_queue_len             \n\t"
        "ldr r2, [r2]                          \n\t"
        "cmp r1, r2                            \n\t"
        "blt store_thread_index                \n\t"

        // Reset indices if we went past thread_queue_len
        "mov r1, #0                            \n\t"
        "str r1, [r0]                          \n\t"
        "ldr r0, =last_running_thread_index    \n\t"
        "mov r1, #1                            \n\t"
        "str r1, [r0]                          \n\t"
        "b update_last_thread_start_tick       \n\t"

        // Store new current thread index if within bounds
        "store_thread_index:                    \n\t"
        "str r1, [r0]                          \n\t"

        // Update last_thread_start_tick
        "update_last_thread_start_tick:        \n\t"
        "ldr r0, =tick_count                   \n\t"
        "ldr r1, [r0]                          \n\t"
        "ldr r0, =last_thread_start_tick       \n\t"
        "str r1, [r0]                          \n\t"

        // Enable interrupts and return
        "enable_and_return:                     \n\t"
        "cpsie i                               \n\t"
        "b return_from_scheduler                                \n\t"

        ::: "r0", "r1", "r2", "memory");
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
        __enable_irq();
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

    for (volatile int i = 0; i < 8; i++) // volatile or the compiler optimizes it to a memset; a function i have not defined in system_calls.c
    {
        *(--ptr) = 0; // R4-R11
    }

    thread->stack_ptr = (uint8_t *)ptr;

    return true;
}

void neo_start_threads(void)
{
    __disable_irq();
    has_threads_started = 1;
    NVIC_SetPriority(PendSV_IRQn, 0xFF); // give lowest priority to PendSV; useful for context switching
    __enable_irq();
}