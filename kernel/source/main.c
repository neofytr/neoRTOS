#include <stdint.h>
#include <stdbool.h>
#include "STM32F401.h"
#include "core_cm4.h"
#include "system_core.h"
#include "neo_threads.h"
#include <stdlib.h>

#define PIN5 5

/* This kernel works currently only without the floating-point hardware being enabled */

void thread_two_fxn(void *arg)
{
    (int *)arg++;
    uint32_t start = get_tick_count();
    bool is_on = false;
    while (true)
    {
        if (has_time_passed(1000, start))
        {
            start = get_tick_count();
            if (is_on)
            {
                SET_BIT(GPIOA->BSRR, PIN5 + 16U);
                is_on = false;
            }
            else
            {
                SET_BIT(GPIOA->BSRR, PIN5);
                is_on = true;
            }
        }
    }
}

void thread_one_fxn(void *arg)
{
    (int *)arg++;
    uint32_t start = get_tick_count();
    bool is_on = false;
    while (true)
    {
        if (has_time_passed(1000, start))
        {
            start = get_tick_count();
            if (is_on)
            {
                SET_BIT(GPIOA->BSRR, PIN5 + 16U);
                is_on = false;
            }
            else
            {
                SET_BIT(GPIOA->BSRR, PIN5);
                is_on = true;
            }
        }
    }
}

#define PIN5 5

int main(void)
{
    LED_setup();
    neo_kernel_init();

    thread_one_fxn(NULL);
}