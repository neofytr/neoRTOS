#ifndef NEO_THREADS_H
#define NEO_THREADS_H

#include <stdint.h>
#include <stdbool.h>
#include "system_core.h"
#include "core_cm4.h"

typedef enum
{
    READY = 0,
    RUNNING = 1,
    SLEEPING = 2,
    PAUSED = 3,
} neo_thread_state_t;

typedef struct __attribute__((packed))
{
    uint8_t *stack_ptr;
    uint32_t sleep_time;             // in multiple of 100 ms
    neo_thread_state_t thread_state; // 1 byte only
} neo_thread_t;

bool neo_thread_init(neo_thread_t *thread, void (*thread_function)(void *), void *thread_arg, uint8_t *stack, uint32_t stack_size);
void neo_kernel_init(void);
__attribute__((naked)) void neo_thread_sleep(uint32_t time);
__attribute__((naked)) void neo_thread_pause(void);
__attribute__((naked)) bool neo_thread_resume(neo_thread_t *thread);
void neo_start_threads(void);

#endif