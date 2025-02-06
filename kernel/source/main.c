#include <stdint.h>
#include <stdbool.h>
#include "STM32F401.h"
#include "core_cm4.h"
#include "system_core.h"
#include "neo_threads.h"
#include <stdlib.h>

#define PIN5 5

neo_thread_t thread_one;
neo_thread_t thread_two;

/* This kernel works currently only without the floating-point hardware being enabled */

uint32_t thread_two_stack[40];
void thread_two_fxn(void *arg)
{
    while (true)
    {
        GPIOA->ODR &= ~(1 << PIN5);
    }
}

uint32_t thread_one_stack[40];
void thread_one_fxn(void *arg)
{
    while (true)
    {
        GPIOA->ODR |= (1 << PIN5);
    }
}

#define PIN5 5

int main(void)
{
    LED_setup();
    neo_kernel_init();

    neo_thread_init(&thread_one, thread_one_fxn, NULL, (uint8_t *)thread_one_stack, 4 * 40);
    neo_thread_init(&thread_two, thread_two_fxn, NULL, (uint8_t *)thread_two_stack, 4 * 40);

    neo_start_threads();

    while (1)
        ;
}