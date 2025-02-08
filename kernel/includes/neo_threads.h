#ifndef NEO_THREADS_H
#define NEO_THREADS_H

#include <stdint.h>
#include <stdbool.h>
#include "system_core.h"
#include "core_cm4.h"

/* if the structure is ever changed, please update the requisite hardcoded values in the udpate sleeping threads function */
typedef struct __attribute__((packed))
{
    uint8_t *stack_ptr;
    uint8_t thread_id; // unique thread id; actually the thread's index in the thread queue
} neo_thread_t;

bool neo_thread_init(neo_thread_t *thread, void (*thread_function)(void *), void *thread_arg, uint8_t *stack, uint32_t stack_size);
void neo_kernel_init(void);
__attribute__((naked)) void neo_thread_sleep(uint32_t time);
__attribute__((naked)) void neo_thread_pause(void);
__attribute__((naked)) bool neo_thread_resume(neo_thread_t *thread);
__attribute__((naked)) bool neo_thread_start(neo_thread_t *thread);
void neo_thread_start_all_new(void);

#endif