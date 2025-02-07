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

        // Check if thread system is started
        "ldr r0, =has_threads_started\n"
        "ldr r0, [r0]\n"
        "cmp r0, #0\n"
        "beq threads_not_started\n"

        // Check if this is first execution
        "ldr r0, =is_first_time\n"
        "ldr r0, [r0]\n"
        "cmp r0, #1\n"
        "beq first_time_thread_handler\n"

        // Check if time slice has expired
        "ldr r1, =tick_count\n"
        "ldr r1, [r1]\n"
        "ldr r0, =last_thread_start_tick\n"
        "ldr r2, [r0]\n"
        "sub r1, r1, r2\n"
        // HARDCODED ALERT: Update this value if TIME_SLICE_TICKS changes
        "cmp r1, #10\n" // Compare against TIME_SLICE_TICKS
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
        "b neo_thread_scheduler\n"
        "return_from_scheduler:\n"
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
    // interrupts are disabled when this function enters
    __asm__ volatile(

        // Check if there are any threads
        "ldr r0, =thread_queue_len\n"
        "ldr r0, [r0]\n"
        "cmp r0, #0\n"
        "beq 2f\n"

        // Check if this is first switch
        "ldr r0, =is_first_time\n"
        "ldr r0, [r0]\n"
        "cmp r0, #1\n"
        "beq first_time_switch\n"

        // Save current thread's stack pointer
        "ldr r0, =thread_queue\n"
        "ldr r1, =last_running_thread_index\n"
        "ldr r1, [r1]\n"
        "lsl r1, r1, #2\n" // Multiply by 4 for pointer array indexing
        "add r0, r0, r1\n"
        "ldr r0, [r0]\n"
        "str sp, [r0]\n"

        "first_time_switch:\n"
        // Load new thread's stack pointer
        "ldr r0, =thread_queue\n"
        "ldr r1, =curr_running_thread_index\n"
        "ldr r1, [r1]\n"
        "lsl r1, r1, #2\n"
        "add r0, r0, r1\n"
        "ldr r0, [r0]\n"
        "ldr sp, [r0]\n"

        // Clear first time flag
        "ldr r0, =is_first_time\n"
        "mov r1, #0\n"
        "str r1, [r0]\n"

        "2:\n"
        "b switch\n" ::: "r0", "r1", "memory");
}

/**
 * @brief Thread scheduler implementation
 * Determines which thread should run next based on round-robin scheduling
 */
__attribute__((naked)) void neo_thread_scheduler(void)
{
    // interrupts are disabled when this function enters
    __asm__ volatile(

        // Check if thread queue is empty
        "ldr r0, =thread_queue_len\n"
        "ldr r0, [r0]\n"
        "cmp r0, #0\n"
        "beq enable_and_return\n"

        // Handle first-time scheduling
        "ldr r0, =is_first_time\n"
        "ldr r0, [r0]\n"
        "cmp r0, #0\n"
        "beq main_scheduling_logic\n"

        "ldr r0, =curr_running_thread_index\n"
        "mov r1, #0\n"
        "str r1, [r0]\n"
        "b enable_and_return\n"

        "main_scheduling_logic:\n"
        // Update thread indices
        "ldr r0, =curr_running_thread_index\n"
        "ldr r1, [r0]\n"

        // Save last running thread
        "ldr r2, =last_running_thread_index\n"
        "str r1, [r2]\n"

        // Calculate next thread index
        "add r1, r1, #1\n"
        // HARDCODED ALERT: Update this value if MAX_THREADS changes
        "mov r2, #10\n" // MAX_THREADS value
        "sub r2, r2, #1\n"
        "and r1, r1, r2\n"

        // Check if index exceeds queue length
        "ldr r2, =thread_queue_len\n"
        "ldr r2, [r2]\n"
        "cmp r1, r2\n"
        "blt store_thread_index\n"

        // Reset to beginning of queue
        "mov r1, #0\n"
        "str r1, [r0]\n"
        "ldr r0, =last_running_thread_index\n"
        "mov r1, #1\n"
        "str r1, [r0]\n"
        "b update_last_thread_start_tick\n"

        "store_thread_index:\n"
        "str r1, [r0]\n"

        "update_last_thread_start_tick:\n"
        // Update timestamp for next time slice
        "ldr r0, =tick_count\n"
        "ldr r1, [r0]\n"
        "ldr r0, =last_thread_start_tick\n"
        "str r1, [r0]\n"

        "enable_and_return:\n"
        "b return_from_scheduler\n" ::: "r0", "r1", "r2", "memory");
    // interrupts are enabled when this function exits
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