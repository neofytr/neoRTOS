#include "neo_threads.h"
#include "system_core.h"

#define PROCESSOR_MODE_BIT (24U)
#define MAX_THREADS (4U)       // must be a power of 2
#define TIME_PER_THREAD (1000) // a thread runs for one second

static volatile neo_thread_t *thread_queue[MAX_THREADS];
static volatile int8_t thread_queue_index = 0;

/* Ticks when the last thread was started */
static volatile uint32_t last_thread_start_ticks;
static volatile uint8_t running_thread_index;

void thread_handler(void)
{
    if (TIME_PER_THREAD <= get_tick_count() - last_thread_start_ticks)
    {
        // Save current SP to thread queue
        __asm__ volatile("str sp, [%0]"
                         :
                         : "r"(&thread_queue[running_thread_index]->stack_ptr)
                         : "memory");

        running_thread_index = (running_thread_index + 1) & (MAX_THREADS - 1);

        // Restore SP from next thread
        __asm__ volatile("ldr sp, [%0]"
                         :
                         : "r"(&thread_queue[running_thread_index]->stack_ptr)
                         : "memory");
        last_thread_start_ticks = get_tick_count();
    }
    return;
}
void init_neo()
{
    // sysclock_init()
    setup_systick(1); // systick returns an interrupt every millisecond
}

bool init_thread(neo_thread_t *thread, void (*thread_function)(void *arg), void *thread_function_arg)
{
    if (thread_queue_index >= MAX_THREADS)
    {
        return false;
    }

    thread_queue[thread_queue_index++] = thread;
    /*
     * Thread Stack Frame Initialization for ARM Cortex-M4
     * ------------------------------------------------
     * This implementation initializes a thread's stack frame to exploit the processor's
     * interrupt return mechanism (exception exit) for context switching. The approach
     * creates a "fake" exception stack frame that the processor will pop during an
     * interrupt return instruction (LR = 0xFFFFFFF9).
     *
     * Exception Stack Frame Layout (Bottom to Top):
     * - R0-R3: General-purpose registers
     * - R12: IP (Intra-Procedure scratch register)
     * - LR: Link Register (Return address)
     * - PC: Program Counter (Thread entry point)
     * - xPSR: Program Status Register
     *
     * Important Implementation Notes:
     * 1. FPU must be disabled as this implementation doesn't handle FPU context
     * 2. Stack grows downward on ARM architecture
     * 3. Stack must be 8-byte aligned as per ARM AAPCS requirements
     * 4. Thread functions must be endless loops and never return
     *
     * Context Switch Mechanism:
     * When executing the interrupt return sequence, the processor will:
     * 1. Pop all registers from the stack frame
     * 2. Update the execution context based on the popped values
     * 3. Begin execution at the address specified in PC
     */

    /* thread_function is the function the thread will execute; thread_function_arg is the initial argument passed to the thread function */

    /* Initialize stack pointer to end of stack area */
    thread->stack_ptr = (uint8_t *)thread->stack + 4 * STACK_SIZE;

    uint32_t *ptr = (uint32_t *)thread->stack_ptr;

    /*
     * Stack Frame Construction (in reverse order of exception stack frame)
     * Each register is pushed onto the stack to create the initial thread context
     */

    /* xPSR - Program Status Register
     * Set Thumb state bit (bit 24) to ensure execution in Thumb state
     * All other bits are initialized to 0 as per reset behavior
     */
    *(--ptr) = 0x1U << PROCESSOR_MODE_BIT;

    /* PC - Program Counter
     * Set to thread_function address
     * This is where execution will begin when the context is restored
     */
    *(--ptr) = (uintptr_t)thread_function;

    /* LR - Link Register
     * Set to 0 since thread_function should never return
     * In a proper implementation, this could point to an error handler
     */
    *(--ptr) = 0x0U;

    /* R12 (IP) - Intra-Procedure scratch register
     * Initialized to 0 as it's caller-saved and not needed initially
     */
    *(--ptr) = 0x0U;

    /* R3 - General-purpose register
     * Initialized to 0 as no initial argument passing is implemented
     */
    *(--ptr) = 0x0U;

    /* R2 - General-purpose register */
    *(--ptr) = 0x0U;

    /* R1 - General-purpose register */
    *(--ptr) = 0x0U;

    /* R0 - General-purpose register
     * First argument register; contains the argument to the thread function
     */
    *(--ptr) = (uintptr_t)thread_function_arg;

    /* Update thread's stack pointer to point to the top of our constructed frame */
    thread->stack_ptr = (uint8_t *)ptr;
}