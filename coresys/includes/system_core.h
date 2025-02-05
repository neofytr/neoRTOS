#include <stdint.h>
#include <stdbool.h>

#define SET_BIT(reg, bit) ((reg) |= ((1UL) << (bit)))
#define CLEAR_BIT(reg, bit) ((reg) &= ~((1UL) << (bit)))
#define TOGGLE_BIT(reg, bit) ((reg) ^= ((1UL) << (bit)))

static volatile uint32_t tick_count = 0; // global tick counter; each tick measures 1 milisecond passed

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
void setup_systick(uint32_t systick_interrupt_period);

/**
 * @brief Safely retrieve the current tick count
 *
 * @return uint32_t Current value of tick_count with interrupt protection
 *
 * This function provides thread-safe access to the tick counter by
 * temporarily disabling interrupts during the read operation.
 */
uint32_t get_tick_count(void);

/**
 * @brief SysTick interrupt handler
 *
 * Increments the global tick counter on each SysTick interrupt.
 *
 * Note: This function doesn't require interrupt protection as it's the only
 * function modifying tick_count besides main(), which handles its own protection.
 */
void SysTick_handler(void);

/**
 * @brief Configure GPIO for LED operation
 *
 * Configures PA5 as a general-purpose output for LED control:
 * 1. Enables GPIOA clock in AHB1 bus
 * 2. Sets PA5 to general purpose output mode (01)
 */
void LED_setup(void);

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
bool has_time_passed(uint32_t time, uint32_t start_tick_count);