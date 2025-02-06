/**
 * @file system_setup.h
 * @brief SysTick timer and LED configuration for ARM Cortex-M4 microcontroller
 *
 * This implementation provides initialization and configuration for:
 * 1. System tick timer (SysTick) for precise timing control
 * 2. GPIO configuration for LED control
 * 3. Basic timing utility functions
 *
 * The system operates at 16MHz by default and configures SysTick for millisecond precision timing.
 */

#include "system_core.h"
#include "core_cm4.h"

/* GPIO Configuration Constants */
#define GPIOA_EN (0U) /* GPIOA enable bit position in AHB1ENR register */
#define PIN5 (5U)     /* LED pin number (PA5) */

/* SysTick Control Register Bit Positions */
#define COUNTER_ENABLE (0)    /* Enable counter bit */
#define CLOCK_SOURCE (2)      /* Clock source selection bit: 0=External, 1=Processor */
#define SYSTICK_INTERRUPT (1) /* SysTick exception request bit */

/* System Clock Definition */
#define SYS_CLOCK 16000000U /* Default system clock frequency (16MHz) */

/* Global tick counter for timing operations */
static volatile uint32_t tick_count;

/**
 * @brief SysTick interrupt handler
 *
 * Increments the global tick counter on each SysTick interrupt.
 *
 * Note: This function doesn't require interrupt protection as it's the only
 * function modifying tick_count besides main(), which handles its own protection.
 */
__attribute__((naked)) void SysTick_handler(void)
{
    /* LR is loaded with EXC_RETURN upon exception entry; this must be popped into PC to return from exception properlyi */
    __asm__ volatile(
        "ldr r0, =tick_count   \n"  // Load address of tick_count
        "ldr r1, [r0]          \n"  // Load tick_count value
        "add r1, r1, #1        \n"  // Increment tick_count
        "str r1, [r0]          \n"  // Store updated value back
        "push {lr}             \n"  // Save LR to stack; required for exception return (EXC_RETURN)
        "bl thread_handler      \n" // Branch with link to thread handler
        "pop {lr}              \n"  // Restore LR from stack; required for exception return (EXC_RETURN)
        "bx lr                 \n"  // Return from interrupt; stack must be the way it was before interrupt entry as setup by the CPU and LR should be loaded with appropriate EXC_RETURN value to return from exception
    );
}

/**
 * @brief Safely retrieve the current tick count
 *
 * @return uint32_t Current value of tick_count with interrupt protection
 *
 * This function provides thread-safe access to the tick counter by
 * temporarily disabling interrupts during the read operation.
 */
uint32_t get_tick_count(void)
{
    uint32_t curr_count;
    __disable_irq(); /* Disable interrupts for atomic operation */
    curr_count = tick_count;
    __enable_irq(); /* Re-enable interrupts */
    return curr_count;
}

/**
 * @brief Configure GPIO for LED operation
 *
 * Configures PA5 as a general-purpose output for LED control:
 * 1. Enables GPIOA clock in AHB1 bus
 * 2. Sets PA5 to general purpose output mode (01)qu
 */
void LED_setup(void)
{
    /* Enable GPIOA clock */
    SET_BIT(RCC->AHB1ENR, GPIOA_EN);

    /* Configure PA5 as general purpose output */
    SET_BIT(GPIOA->MODER, PIN5 * 2);       /* Set bit 10 */
    CLEAR_BIT(GPIOA->MODER, PIN5 * 2 + 1); /* Clear bit 11 */
}

/**
 * @brief Check if specified time has elapsed since start point
 *
 * @param time Duration to check in milliseconds
 * @param start_tick_count Reference starting point in tick counts
 * @return bool true if specified time has elapsed, false otherwise
 *
 * Usage example:
 * @code
 * uint32_t start = get_tick_count();
 * if(has_time_passed(1000, start)) {
 *     // 1 second has passed
 * }
 * @endcode
 */
bool has_time_passed(uint32_t time, uint32_t start_tick_count)
{
    return (get_tick_count() - start_tick_count) >= time;
}

/**
 * @brief Initialize and configure the SysTick timer
 *
 * @param systick_interrupt_period Desired interrupt period in milliseconds
 *
 * Configures the SysTick timer with the following settings:
 * 1. Uses processor clock as source
 * 2. Enables counter and interrupt
 * 3. Calculates and sets reload value for desired period
 *
 * The reload value is calculated as:
 * LOAD = (desired_period_ms * clock_freq / 1000) - 1
 *
 * @note The actual period might have slight deviation due to integer division
 */
void setup_systick(uint32_t systick_interrupt_period)
{
    /* Configure SysTick control register */
    SET_BIT(SysTick->CTRL, COUNTER_ENABLE);    /* Enable counter */
    SET_BIT(SysTick->CTRL, CLOCK_SOURCE);      /* Use processor clock */
    SET_BIT(SysTick->CTRL, SYSTICK_INTERRUPT); /* Enable interrupt */

    /* Enable SysTick interrupt in NVIC */
    NVIC_EnableIRQ(SysTick_IRQn);

    /* Calculate and set reload value
     * Formula: (period_ms * clock_freq / 1000) - 1
     * The 0x00FFFFFF mask ensures value fits in 24-bit register
     */
    SysTick->LOAD = 0x00FFFFFF & (unsigned int)(systick_interrupt_period * SYS_CLOCK / 1000U - 1);
}

__attribute__((naked)) void default_thread_handler(void)
{
    __asm__ volatile("bx lr \n"); // simply return from the handler
}

__attribute__((weak, alias("default_thread_handler"), naked)) void thread_handler(void);

/* Undefine all local macros */
#undef GPIOA_EN
#undef PIN5
#undef COUNTER_ENABLE
#undef CLOCK_SOURCE
#undef SYSTICK_INTERRUPT
#undef SYS_CLOCK