#include <stdint.h>
#include <stdbool.h>
#include "STM32F401.h"
#include "core_cm4.h"
#include "system_core.h"
#include "neo_threads.h"
#include <stdlib.h>

#define PIN5 5

/* This kernel works currently only without the floating-point hardware being enabled */

neo_thread_t thread_one;
neo_thread_t thread_two;

bool has_run = false;

__attribute__((naked)) void thread_handler(void)
{
    __asm__ volatile(
        "ldr r2, =has_run \n"
        "ldr r1, [r2] \n"
        "cmp r1, 1 \n"
        "beq _exit_run \n"
        "ldr r0, =thread_one \n"
        "ldr sp, [r0] \n"
        "mov r3, 1 \n"
        "str r3, [r2] \n"
        "_exit_run: \n"
        "bx lr \n");
}

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
    setup_systick(1); // give an interrupt every millisecond
    init_thread(&thread_one, thread_one_fxn, NULL);
    init_thread(&thread_two, thread_two_fxn, NULL);

    while (true)
        ;
}