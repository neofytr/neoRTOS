#include "neo_threads.h"
#include "system_core.h"

#define PROCESSOR_MODE_BIT (24U)
#define MAX_THREADS (4)

/* Make the variables accessed inside interrupt handlers volatile */
static neo_thread_t *volatile thread_queue[MAX_THREADS]; // this makes the pointer volatile; placing volatile before the type name will make the contents of the pointer volatile
static volatile uint8_t thread_queue_index;

neo_thread_t *volatile nex_thread;
neo_thread_t *volatile curr_thread;

#define TIME_PER_THREAD 1000

static volatile uint8_t last_thread_start_ticks;
static volatile uint8_t running_thread_index;

__attribute__((naked)) void thread_handler(void)
{
    __asm__ volatile("b neo_thread_scheduler \n");
}

void neo_thread_scheduler(void)
{
}

void PendSV_handler(void)
{
}

bool init_thread(neo_thread_t *thread, void (*thread_function)(void *arg), void *thread_function_arg, uint8_t *stack, uint32_t stack_size)
{
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

    /*

    During Thread Creation:

    The stack is initialized from top to bottom with all necessary registers
    First, we push the exception stack frame (R0-R3, R12, LR, PC, xPSR)
    Then, we push the additional context registers (R4-R11)
    The thread's stack pointer is saved pointing to R4, the top of our complete stack frame

    During Context Switch (in interrupt handler):

    Current Thread → Save Phase:
    1. Push R4-R11 of current thread onto its stack
    2. Save current stack pointer in thread control block

    New Thread → Restore Phase:
    1. Load new thread's stack pointer
    2. Pop R4-R11 from new thread's stack
    3. Execute return-from-interrupt (which restores R0-R3, R12, LR, PC, xPSR)

    The Context Switch Mechanism:

    When an interrupt or exception occurs, hardware automatically pushes R0-R3, R12, LR, PC, xPSR
    The interrupt handler must manually save R4-R11 of the current thread
    After switching to the new thread's stack, R4-R11 are manually restored
    The return-from-interrupt instruction (typically triggered by loading 0xFFFFFFF9 into PC) automatically restores the exception stack frame

    Special Considerations:

    The initial values (0x66U through 0xDDU) are arbitrary but help with debugging
    Stack alignment (8-byte) is maintained throughout for ARM AAPCS compliance
    This implementation assumes no FPU usage (no S0-S31 registers saved)
    The order of registers matches the ARM AAPCS specification for maximum compatibility

    */

    /* thread_function is the function the thread will execute; thread_function_arg is the initial argument passed to the thread function */

    /* Initialize stack pointer to end of stack area */
    thread->stack_ptr = (uint8_t *)((uintptr_t)stack + stack_size);

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
    *(--ptr) = 0x11U;

    /* R12 (IP) - Intra-Procedure scratch register
     * Initialized to 0 as it's caller-saved and not needed initially
     */
    *(--ptr) = 0x22U;

    /* R3 - General-purpose register
     * Initialized to 0 as no initial argument passing is implemented
     */
    *(--ptr) = 0x33U;

    /* R2 - General-purpose register */
    *(--ptr) = 0x44U;

    /* R1 - General-purpose register */
    *(--ptr) = 0x55U;

    /* R0 - General-purpose register
     * First argument register; contains the argument to the thread function
     */
    *(--ptr) = (uintptr_t)thread_function_arg;

    /* Save the registers R4-R11 next; push these registers immediately after entering the interrupt handler and before switching stack frame when doing a context switch to another thread */
    /* Pop them after switching the stack frame and before doing bx lr from the interrupt handler */

    /* R4-R11 are callee-saved registers that must be preserved across function calls
     * These registers form the additional context that must be saved/restored during
     * context switches but are not automatically handled by the exception stack frame
     */

    /* R11 (FP) - Frame Pointer
     * Used in stack unwinding and debugging
     * Initialize to 0 as we're starting a new thread context
     */
    *(--ptr) = 0x66U;

    /* R10 - General-purpose register
     * Often used as static base register in position-independent code
     */
    *(--ptr) = 0x77U;

    /* R9 (SB) - Static Base register
     * Platform-specific use, often for accessing static data
     */
    *(--ptr) = 0x88U;

    /* R8 - General-purpose register */
    *(--ptr) = 0x99U;

    /* R7 - General-purpose register
     * Sometimes used as frame pointer in Thumb state
     */
    *(--ptr) = 0xAAU;

    /* R6 - General-purpose register */
    *(--ptr) = 0xBBU;

    /* R5 - General-purpose register */
    *(--ptr) = 0xCCU;

    /* R4 - General-purpose register */
    *(--ptr) = 0xDDU;

    /* Update thread's stack pointer to point to the top of our constructed frame */
    thread->stack_ptr = (uint8_t *)ptr;
    return true;
}