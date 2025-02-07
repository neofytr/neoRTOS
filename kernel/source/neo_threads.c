#include "neo_threads.h"

/* TODO */
// Somehow use PSP and MSP?
// Implement thread states (ACTIVE, SLEEPING, etc.)
// Implement thread priority
// Implement thread mutexes
// Implement thread semaphores
// Implement thread message queues
// Implement switching CPU states (from handler to thread (user)) and vice versa; This requires modifying LR with different EXC_RETURN values before returning from the interrupt
// Implement thread sleep function
// Implement thread yield function
// Implement thread join function
// Implement thread exit function
// Implement starting thread from whereever we want

/* Configuration Constants
 * NOTE: If you modify these values, you must also update the corresponding
 * hardcoded values in the assembly code sections marked with "HARDCODED ALERT"
 */
#define TIME_SLICE_MS (100U)     // Base time slice in milliseconds
#define TIME_SLICE_TICKS (10U)   // Number of ticks before thread switch (1 second total)
#define MAX_THREADS (10U)        // Maximum number of concurrent threads
#define PROCESSOR_MODE_BIT (24U) // Processor mode control bit position
#define STACK_ALIGNMENT (8U)     // Required stack alignment in bytes (AAPCS standard)
#define PENDSV_IRQ_NUM (14U)     // PendSV interrupt number
#define LOWEST_PRIORITY (0xFFU)  // Lowest interrupt priority for PendSV

/* Thread Management State
 * volatile qualifier used for variables accessed from both main code and ISRs
 */
volatile uint32_t last_thread_start_tick;        // Timestamp of last thread switch
volatile uint32_t last_running_thread_index = 0; // Index of previously running thread
volatile uint32_t curr_running_thread_index = 0; // Index of currently running thread
volatile uint32_t is_first_time = 1;             // First context switch flag
volatile uint32_t has_threads_started = 0;       // Thread system initialization flag

/* Thread Queue Management */
neo_thread_t *volatile thread_queue[MAX_THREADS]; // this creates a volatile pointer; the pointer is not volatile, but the data it points to is;
// if the volatile keyword is placed before the *, then the data the ptr points to is volatile and not the pointer itself; otherwise, if the volatile keyword is placed after the *, then the pointer is volatile and not the data it points to
volatile uint32_t thread_queue_len = 0;

/**
 * @brief Initialize the thread kernel system
 * Sets up SysTick timer and PendSV interrupt for thread scheduling
 */
void neo_kernel_init(void)
{
    setup_systick(TIME_SLICE_MS); // Configure system tick for thread time slicing
    __disable_irq();
    NVIC_EnableIRQ(PendSV_IRQn); // Enable PendSV for context switching
    // setting PendSV to the lowest priority; this is so that context switch happens when all interrupts are done
    NVIC_SetPriority(PendSV_IRQn, LOWEST_PRIORITY); // setting priority to 0xFF; it will still be set to 0xF0 since STM32 only implements 4 MSB bits for priority
    // by default, the priority of SysTick is set to 0x00; we set it here manually anyway
    NVIC_SetPriority(SysTick_IRQn, 0x00); // setting priority to 0x00
    __enable_irq();
}

/**
 * @brief Thread system timer handler
 * Manages thread time slicing and triggers context switches
 * NOTE: naked attribute prevents compiler from generating prologue/epilogue
 */
__attribute__((naked)) void thread_handler(void)
{
    // interrupts are already disabled when this function enters

    __asm__ volatile(
        ".extern exit_from_interrupt_\n"

        // Check if thread system is started (using r3 to keep value)
        "ldr r3, =has_threads_started\n"
        "ldr r3, [r3]\n"
        "cbz r3, threads_not_started\n" // Use cbz instead of cmp/beq

        // Check if this is first execution
        "ldr r2, =is_first_time\n"
        "ldr r2, [r2]\n"
        "cmp r2, #1\n"
        "beq first_time_thread_handler\n"

        // Load tick values and check time slice expiration
        "ldr r0, =tick_count\n"
        "ldr r1, [r0]\n" // Current tick in r1
        "ldr r0, =last_thread_start_tick\n"
        "ldr r0, [r0]\n"   // Last start tick in r0
        "sub r1, r1, r0\n" // Calculate elapsed ticks
                           // HARDCODED ALERT: Update this value if TIME_SLICE_TICKS changes
        "cmp r1, #10\n"    // Compare against TIME_SLICE_TICKS
        "blt thread_time_slice_not_expired\n"

        "first_time_thread_handler:\n");

    // Trigger PendSV exception for context switch
    SCB->ICSR |= (1U << (2 * PENDSV_IRQ_NUM));

    __asm__ volatile(
        "threads_not_started:\n"
        "thread_time_slice_not_expired:\n"
        "b exit_from_interrupt_\n");
    // interrupts are enabled when this function exits
}

/**
 * @brief PendSV exception handler for context switching
 * Saves and restores thread contexts
 */
__attribute__((naked)) void PendSV_handler(void)
{
    /* Arm exception execution has tail-chaining in which if one interrupt handler is executed just after another interrupt, the stack pop in the previous handler and stack push in the next handler is skipped */
    /* This is because the push contents of both the handlers will be the same as no new application code has executed between them; this reduces exception call o */
    __asm__ volatile(
        // Check if context save is needed
        "cpsid i\n"
        "ldr r0, =is_first_time\n"
        "ldr r0, [r0]\n"
        "cmp r0, #1\n"
        "beq skip_save\n"

        // Save registers R4-R11 (callee-saved registers)
        "push {r4-r11}\n"

        "skip_save:\n"
        // we now schedule which thread to run next
        "b neo_thread_scheduler\n"
        "return_from_scheduler:\n"
        // actual context switch happens from here in the neo_context_switch function
        "b neo_context_switch\n"

        "switch:\n"
        // Restore callee-saved registers
        "pop {r4-r11}\n"
        "cpsie i\n" // enable interrupts again
        "bx lr\n");
}

/**
 * @brief Performs the actual context switch between threads
 * Handles stack pointer updates and thread state transitions
 */
__attribute__((naked)) void neo_context_switch(void)
{
    __asm__ volatile(
        // Load thread_queue_len directly into r3 to check if there are threads
        "ldr r3, =thread_queue_len\n"
        "ldr r3, [r3]\n"
        "cbz r3, 2f\n" // branch if r3 is zero

        // Check first time switch (using r3 since we don't need thread_queue_len anymore)
        "ldr r3, =is_first_time\n"
        "ldr r3, [r3]\n"
        "cmp r3, #1\n"
        "beq 1f\n" // Local forward reference instead of label

        // Save current thread's SP
        "ldr r2, =thread_queue\n" // Load queue base into r2
        "ldr r3, =last_running_thread_index\n"
        "ldr r3, [r3]\n"
        "ldr r0, [r2, r3, lsl #2]\n" // Use indexed addressing mode
        "str sp, [r0]\n"             // Store SP directly - stack_ptr is first element

        "1:\n" // first_time_switch
        // Load new thread's SP
        "ldr r2, =thread_queue\n" // r2 already has this if not first time
        "ldr r3, =curr_running_thread_index\n"
        "ldr r3, [r3]\n"
        "ldr r0, [r2, r3, lsl #2]\n" // Use indexed addressing mode
        "ldr sp, [r0]\n"             // Load SP directly - stack_ptr is first element

        // Clear first time flag - only need one register now
        "ldr r0, =is_first_time\n"
        "mov r3, #0\n"
        "str r3, [r0]\n"

        "2:\n"
        "b switch\n" ::: "r0", "r2", "r3", "memory" // Updated clobber list
    );
}

/**
 * @brief Thread scheduler implementation
 * Determines which thread should run next based on round-robin scheduling
 */
__attribute__((naked)) void neo_thread_scheduler(void)
{
    __asm__ volatile(
        // Load thread_queue_len and check if empty
        "ldr r3, =thread_queue_len\n"
        "ldr r3, [r3]\n"
        "cbz r3, enable_and_return\n" // Using original label

        // Check first-time scheduling
        "ldr r2, =is_first_time\n"
        "ldr r2, [r2]\n"
        "cbz r2, main_scheduling_logic\n" // Using original label

        // First time initialization
        "ldr r2, =curr_running_thread_index\n"
        "mov r3, #0\n"
        "str r3, [r2]\n"
        "b enable_and_return\n"

        "main_scheduling_logic:\n"
        "ldr r2, =curr_running_thread_index\n"
        "ldr r1, [r2]\n" // Load current index

        // Save last running thread
        "ldr r0, =last_running_thread_index\n"
        "str r1, [r0]\n"

        // Calculate next thread index (r1 = current index)
        "add r1, #1\n" // Increment
        // HARDCODE ALERT: Update this value (MAX_THREADS - 1) if MAX_THREADS changes
        "and r1, #9\n" // Mask with (MAX_THREADS-1) = 9

        // Check if index exceeds queue length (r3 still has queue_len)
        "cmp r1, r3\n"
        "blt store_thread_index\n" // Using original label

        // Reset indices if we wrapped around
        "mov r1, #0\n"   // Reset curr_index to 0
        "str r1, [r2]\n" // Store curr_index
        "ldr r2, =last_running_thread_index\n"
        "mov r1, #1\n"
        "str r1, [r2]\n"
        "b update_last_thread_start_tick\n"

        "store_thread_index:\n"
        "str r1, [r2]\n" // r2 still points to curr_running_thread_index

        "update_last_thread_start_tick:\n"
        "ldr r2, =tick_count\n"
        "ldr r1, [r2]\n"
        "ldr r2, =last_thread_start_tick\n"
        "str r1, [r2]\n"

        "enable_and_return:\n"
        "b return_from_scheduler\n" ::: "r0", "r1", "r2", "r3", "memory");
}

/**
 * @brief Initialize a new thread
 * @param thread Pointer to thread structure
 * @param thread_function Thread entry point function
 * @param thread_arg Argument passed to thread function
 * @param stack Pointer to thread's stack memory
 * @param stack_size Size of stack in bytes
 * @return true if initialization successful, false otherwise
 */
bool neo_thread_init(neo_thread_t *thread, void (*thread_function)(void *),
                     void *thread_arg, uint8_t *stack, uint32_t stack_size)
{
    // Validate parameters
    if (!thread || !thread_function || !stack)
    {
        return false;
    }

    // Check thread limit
    __disable_irq();
    if (thread_queue_len >= MAX_THREADS)
    {
        __enable_irq();
        return false;
    }

    // Add thread to queue
    thread_queue[thread_queue_len++] = thread;
    __enable_irq();

    // Align stack pointer to 8-byte boundary (AAPCS requirement)
    thread->stack_ptr = (uint8_t *)(((uintptr_t)stack + stack_size) & ~(STACK_ALIGNMENT - 1));
    uint32_t *ptr = (uint32_t *)thread->stack_ptr;

    /*
     * Initialize Thread Stack Frame
     *
     * Stack layout (from high to low address):
     * - xPSR: Program Status Register (Thumb bit set)
     * - PC: Program Counter (thread_function)
     * - LR: Link Register (unused as thread shouldn't return)
     * - R12: General Purpose Register
     * - R3-R1: Parameter Registers (unused)
     * - R0: First Parameter Register (thread_arg)
     * - R11-R4: Callee-saved Registers
     */

    *(--ptr) = 0x01000000;                // xPSR (Thumb bit)
    *(--ptr) = (uint32_t)thread_function; // PC
    *(--ptr) = 0;                         // LR
    *(--ptr) = 0;                         // R12
    *(--ptr) = 0;                         // R3
    *(--ptr) = 0;                         // R2
    *(--ptr) = 0;                         // R1
    *(--ptr) = (uint32_t)thread_arg;      // R0

    // Initialize callee-saved registers
    for (volatile int i = 0; i < 8; i++) // without volatile, memset is used which I have not defined
    {
        *(--ptr) = 0; // R11-R4
    }

    thread->stack_ptr = (uint8_t *)ptr;
    return true;
}

/**
 * @brief Start the thread system
 * Enables threading and sets up final configurations
 */
void neo_start_threads(void)
{
    __disable_irq();
    has_threads_started = 1;
    __enable_irq();
}