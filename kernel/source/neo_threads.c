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

extern volatile uint32_t tick_count;

/* Thread Management State
 * volatile qualifier used for variables accessed from both main code and ISRs
 */
volatile uint32_t last_thread_start_tick;        // Timestamp of last thread switch
volatile uint32_t last_running_thread_index = 0; // Index of previously running thread
volatile uint32_t curr_running_thread_index = 0; // Index of currently running thread
volatile uint32_t is_first_time = 1;             // First context switch flag
volatile uint32_t has_threads_started = 0;       // Thread system initialization flag

/* Thread Queue Management */
// extra space for idle thread
volatile neo_thread_t *volatile thread_queue[MAX_THREADS + 1]; // this creates a volatile pointer; the pointer is not volatile, but the data it points to is;
// if the volatile keyword is placed before the *, then the data the ptr points to is volatile and not the pointer itself; otherwise, if the volatile keyword is placed after the *, then the pointer is volatile and not the data it points to
volatile uint32_t thread_queue_len = 0;

#define IDLE_THREAD_STACK_SIZE_IN_32_BITS 20
volatile uint32_t idle_thread_stack[IDLE_THREAD_STACK_SIZE_IN_32_BITS];
volatile neo_thread_t idle_thread;

volatile uint32_t ready_threads_bit_mask = 0;
volatile uint32_t new_threads_bit_mask = 0;
volatile uint32_t sleeping_threads_bit_mask = 0;
volatile uint32_t running_threads_bit_mask = 0;
volatile uint32_t paused_threads_bit_mask = 0;

volatile uint32_t thread_sleep_time[MAX_THREADS];

// returns the bit number of the most significant one in num
// if there is no one in num (i.e num is zero), it returns -1
static inline int8_t most_sig_one(uint32_t num)
{
    return 31 - (int8_t)__CLZ(num);
}

void idle_thread_function(void)
{
    while (true)
    {
        /* The CPU goes into sleep mode and wakes up when an interrupt occurs */
        __asm__ volatile("wfi");
    }
}

/**
 * @brief Initialize the thread kernel system
 * Sets up SysTick timer and PendSV interrupt for thread scheduling
 */
void neo_kernel_init(void)
{
    __disable_irq();
    setup_systick(TIME_SLICE_MS); // Configure system tick for thread time slicing

    NVIC_EnableIRQ(PendSV_IRQn); // Enable PendSV for context switching
    // setting PendSV to the lowest priority; this is so that context switch happens when all interrupts are done
    NVIC_SetPriority(PendSV_IRQn, LOWEST_PRIORITY); // setting priority to 0xFF; it will still be set to 0xF0 since STM32 only implements 4 MSB bits for priority
    // by default, the priority of SysTick is set to 0x00; we set it here manually anyway
    NVIC_SetPriority(SysTick_IRQn, 0x00); // setting priority to 0x00

    thread_queue[MAX_THREADS] = &idle_thread;
    idle_thread.thread_id = MAX_THREADS;

    ready_threads_bit_mask |= 1U << idle_thread.thread_id;
    uint8_t *aligned_top = (uint8_t *)(((uintptr_t)idle_thread_stack + IDLE_THREAD_STACK_SIZE_IN_32_BITS * 4) & ~(STACK_ALIGNMENT - 1));

    // Use temporary pointer to build stack frame
    uint32_t *ptr = (uint32_t *)aligned_top;

    *(--ptr) = 0x01000000;                     // xPSR (Thumb bit)
    *(--ptr) = (uint32_t)idle_thread_function; // PC
    *(--ptr) = 0;                              // LR
    *(--ptr) = 0;                              // R12
    *(--ptr) = 0;                              // R3
    *(--ptr) = 0;                              // R2
    *(--ptr) = 0;                              // R1
    *(--ptr) = 0;                              // R0

    // Initialize callee-saved registers
    for (volatile int i = 0; i < 8; i++)
    {
        *(--ptr) = 0; // R11-R4
    }

    // Only now set the thread's stack pointer to the final position
    idle_thread.stack_ptr = (uint8_t *)ptr;
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
    // we will not save the registers r4 to r11 of the current thread in this function; we will save them in the PendSV handler; so we can't clobber them in this function
    // we can save them in this function though; but we will not do that
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

        // update sleeping threads
        "b update_sleeping_threads \n"
        "return_from_update: \n"

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
    SCB->ICSR |= (1U << (2 * PENDSV_IRQ_NUM)); // will be done just using r0 to r3

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
        "stmdb sp!, {r4-r11}\n"

        /* we have now saved the registers r4 to r11; we can clobber them in the subsequent function calls */

        "skip_save:\n"
        // we now schedule which thread to run next
        "b neo_thread_scheduler\n"
        "return_from_scheduler:\n"
        // actual context switch happens from here in the neo_context_switch function
        "b neo_context_switch\n"

        "switch:\n"
        // Restore callee-saved registers
        "ldmia sp!, {r4-r11}\n"
        "cpsie i\n" // enable interrupts again
        "bx lr\n");
}

/**
 * @brief Performs the actual context switch between threads
 * Handles stack pointer updates and thread state transitions
 */
__attribute__((naked)) void neo_context_switch(void)
{
    /* we have now saved the registers r4 to r11; we can clobber them */
    /* interrupts are disabled before entering this function */

    __asm__ volatile(
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
 * @brief Bare metal thread scheduler implementation
 *
 * This function implements a round-robin scheduler with support for thread states
 * and idle thread handling. It runs with interrupts disabled and uses naked attribute
 * to manage its own stack frame.
 *
 * Key Features:
 * - Round-robin scheduling policy
 * - Support for READY and RUNNING thread states
 * - Idle thread fallback when no threads are ready
 * - First-time initialization handling
 *
 * @note This function is marked as naked and should only be called from assembly
 * @note Registers r4-r11 are already saved before entering this function
 * @note Interrupts are disabled when entering this function
 */

__attribute__((naked)) void neo_thread_scheduler(void)
{
    // this function is called with interrupts disabled
    /* Handle first-time scheduling initialization */
    if (is_first_time)
    {
        /* Find first ready thread or default to idle */
        uint32_t ready_bits = ready_threads_bit_mask & ~(1U << MAX_THREADS);
        int8_t index;

        if (ready_bits)
        {
            uint32_t clz_result;
            __asm__ volatile("clz %0, %1" : "=r"(clz_result) : "r"(ready_bits));
            index = 31 - (int8_t)clz_result;
        }
        else
        {
            index = -1;
        }

        curr_running_thread_index = (index == -1) ? MAX_THREADS : (uint32_t)index;

        goto enable_and_return;
    }

    /* Main scheduling logic */
    last_running_thread_index = curr_running_thread_index;

    /* Update previous thread state if it was running */
    if (running_threads_bit_mask & (1U << last_running_thread_index))
    {
        running_threads_bit_mask &= ~(1U << last_running_thread_index);
        ready_threads_bit_mask |= 1U << last_running_thread_index;
    }

    /* Find next thread to run */
    uint32_t ready_bits = ready_threads_bit_mask & ~(1U << MAX_THREADS);
    int8_t index = -1;

    if (!ready_bits)
    {
        /* Find next thread after the last running thread */
        uint32_t next_threads = ready_bits & ~((1U << last_running_thread_index) - 1);
        if (!next_threads)
        {
            /* Found a thread with higher index */
            uint32_t clz_result;
            __asm__ volatile("clz %0, %1" : "=r"(clz_result) : "r"(next_threads));
            index = 31 - (int8_t)clz_result;
        }
        else
        {
            /* Wrap around to lowest index */
            uint32_t clz_result;
            __asm__ volatile("clz %0, %1" : "=r"(clz_result) : "r"(ready_bits));
            index = 31 - (int8_t)clz_result;
        }
    }

    /* If no ready threads found, run idle thread */
    curr_running_thread_index = (index == -1) ? MAX_THREADS : (uint32_t)index;

enable_and_return:
    /* Update thread state and timing information */
    running_threads_bit_mask = 1U << curr_running_thread_index;
    ready_threads_bit_mask &= ~(1U << curr_running_thread_index);
    last_thread_start_tick = tick_count;

    /* Return to assembly context switch code */
    __asm__ volatile("b return_from_scheduler");
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
    thread->thread_id = thread_queue_len;
    thread_queue[thread_queue_len++] = thread;

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
    new_threads_bit_mask |= 1U << thread->thread_id;
    __enable_irq(); // enable interrupts only after the thread has been initialized
    return true;
}

/**
 * @brief Start a new thread
 * Doesn't actually start the thread as in the thread starts executing; it just changes its state to READY
 * The thread will start executing when it's scheduled
 * @param thread Pointer to thread structure
 * @return true if thread was started, false otherwise
 */
__attribute__((naked)) bool neo_thread_start(neo_thread_t *thread)
{
    __asm__ volatile("cpsid i \n");
    has_threads_started = 1;
    if (new_threads_bit_mask & (1U << thread->thread_id))
    {
        ready_threads_bit_mask |= 1U << thread->thread_id;
        __asm__ volatile("cpsie i \n"
                         "mov r0, #1 \n" // return true if thread was new and we started it
                         "bx lr \n");
    }
    __asm__ volatile("cpsie i \n"
                     "mov r0, #0 \n" // return false otherwise since the thread was not new to begin with
                     "bx lr \n");
}

/**
 * @brief Start all new threads
 * Changes the state of all new threads to READY
 * Threads will start executing when they are scheduled
 */
void neo_thread_start_all_new(void)
{
    __disable_irq();
    int8_t index;
    while ((index = most_sig_one(new_threads_bit_mask)) != -1) // efficient way to find the ready threads
    {
        ready_threads_bit_mask |= 1U << index;  // add the thread to the ready threads
        new_threads_bit_mask &= ~(1U << index); // remove the thread from the new threads
    }
    has_threads_started = 1;
    __enable_irq();
}

/**
 * @brief Resume a paused thread
 * Doesn't actually resume the thread as in the thread starts executing again; it just changes its state to READY
 * The thread will come back to life when it's scheduled again
 * @param thread Pointer to thread structure
 * @return true if thread was paused and resumed, false otherwise
 */
__attribute__((naked)) bool neo_thread_resume(neo_thread_t *thread)
{
    __asm__ volatile("cpsid i \n");
    if (paused_threads_bit_mask & (1U << thread->thread_id))
    {
        ready_threads_bit_mask |= 1U << thread->thread_id;
        paused_threads_bit_mask &= ~(1U << thread->thread_id);
        __asm__ volatile("cpsie i \n"
                         "mov r0, #1 \n" // return true if thread was paused and we resumed it
                         "bx lr \n");
    }
    __asm__ volatile("cpsie i \n"
                     "mov r0, #0 \n" // return false otherwise since the thread was not paused to begin with
                     "bx lr \n");
}

/**
 * @brief Pause the current thread
 * Pauses the current thread and triggers a context switch
 * The thread is paused until it's manually resumed
 */
__attribute__((naked)) void neo_thread_pause(void)
{
    __asm__ volatile("cpsid i \n");
    paused_threads_bit_mask |= 1U << curr_running_thread_index;
    ready_threads_bit_mask &= ~(1U << curr_running_thread_index);
    running_threads_bit_mask &= ~(1U << curr_running_thread_index);
    SCB->ICSR |= (1U << (2 * PENDSV_IRQ_NUM));
    __asm__ volatile("cpsie i \n"
                     "bx lr \n");
}

__attribute__((naked)) void update_sleeping_threads()
{
    __asm__ volatile(
        // Load addresses of key variables
        "push {r4, r5, r6} \n" // Save registers we'll use
        "ldr r0, =sleeping_threads_bit_mask \n"
        "ldr r1, =thread_sleep_time \n"
        "ldr r2, =ready_threads_bit_mask \n"
        "mov r6, #0 \n" // Initialize loop counter

        "check_thread: \n"
        "cmp r6, #10 \n" // Check if we've processed all possible threads
        "beq done \n"

        // Check if this thread is sleeping
        "ldr r3, [r0] \n" // Load sleeping mask
        "mov r4, #1 \n"
        "lsl r4, r4, r6 \n" // Create bit mask for current thread
        "tst r3, r4 \n"     // Test if thread is sleeping
        "beq next_thread \n"

        // Decrement sleep time
        "lsl r5, r6, #2 \n"   // r5 = thread_index * 4
        "ldr r3, [r1, r5] \n" // Load sleep time
        "sub r3, r3, #1 \n"   // Decrement
        "str r3, [r1, r5] \n" // Store updated time

        // Check if sleep completed
        "cmp r3, #0 \n"
        "bne next_thread \n"

        // Wake up thread
        "ldr r3, [r0] \n"   // Load sleeping mask
        "bic r3, r3, r4 \n" // Clear sleeping bit
        "str r3, [r0] \n"   // Update sleeping mask

        "ldr r3, [r2] \n"   // Load ready mask
        "orr r3, r3, r4 \n" // Set ready bit
        "str r3, [r2] \n"   // Update ready mask

        "next_thread: \n"
        "add r6, r6, #1 \n" // Increment thread counter
        "b check_thread \n"

        "done: \n"
        "pop {r4, r5, r6} \n" // Restore registers
        "b return_from_update \n");
}

/* A subtle problem is that the thread calling sleep gets context switched just before the call; this can lead to the thread waiting more than expected; fundamental flaw */
__attribute__((naked)) void neo_thread_sleep(uint32_t time)
{
    // time is in multiple of 100ms
    __asm__ volatile("cpsid i \n");
    // set the thread state to SLEEPING; everytime systick interrupt occurs, all threads that are SLEEPING have their sleep_time decremented by 1
    sleeping_threads_bit_mask |= 1U << curr_running_thread_index;
    ready_threads_bit_mask &= ~(1U << curr_running_thread_index);
    running_threads_bit_mask &= ~(1U << curr_running_thread_index);
    // set the sleep time of the thread
    thread_sleep_time[curr_running_thread_index] = time;
    // trigger context switch
    SCB->ICSR |= (1U << (2 * PENDSV_IRQ_NUM));
    __asm__ volatile("cpsie i \n"
                     "bx lr \n");
}