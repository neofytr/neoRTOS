#include <stdint.h>
#include <stdbool.h>
#include "STM32F401.h"
#include "core_cm4.h"

#define SET_BIT(reg, bit) ((reg) |= ((1UL) << (bit)))
#define CLEAR_BIT(reg, bit) ((reg) &= ~((1UL) << (bit)))
#define TOGGLE_BIT(reg, bit) ((reg) ^= ((1UL) << (bit)))

void setup_systick(void);
static inline uint32_t get_tick_count(void);

#define SYS_CLOCK 16000000U // 16MHz is the default system clock without any configuration
#define SMALLEST_UNIT_OF_TIME_MEASURED (1000U)

volatile uint32_t tick_count = 0; // global tick counter; each tick measures 1 milisecond passed

void setup_systick(void)
{
#define COUNTER_ENABLE (0)
    SET_BIT(SysTick->CTRL, COUNTER_ENABLE); // enable counter
#define CLOCK_SOURCE (2)
    SET_BIT(SysTick->CTRL, CLOCK_SOURCE); // select the processor clock
#define SYSTICK_INTERRUPT (1)
    SET_BIT(SysTick->CTRL, SYSTICK_INTERRUPT); // enables the system interrupt request

    NVIC_EnableIRQ(SysTick_IRQn); // enable systick interrupts; i think they are also enabled by default?

    SysTick->LOAD = 0x00FFFFFFU & (unsigned int)(1 * SYS_CLOCK / SMALLEST_UNIT_OF_TIME_MEASURED - 1); // timer with a period of SYS_CLOCK processor clock cycles
    // so the systick interrupt goes off roughly every milisecond
    // systick counts from zero so we subtract a one
}

void SysTick_handler(void)
{
    tick_count++; // there is no other function (except the main function, which will disable interrupts
    // before accessing the tick_count) that accesses this global variable so we don't need to put disable and
    // enable irq before and after accessing this variable here
}

static inline uint32_t get_tick_count(void)
{
    uint32_t curr_count;
    __disable_irq();
    curr_count = tick_count;
    __enable_irq();
    return curr_count;
}

bool has_time_passed(uint32_t time, uint32_t start_tick_count)
{
    // time is in miliseconds
    return (get_tick_count() - start_tick_count) >= time;
}

int main(void)
{
    setup_systick();
    while (true)
        ;
}