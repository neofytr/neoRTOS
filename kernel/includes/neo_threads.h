#include <stdint.h>

#define STACK_SIZE (40U) // in words (32-bits)

typedef struct
{
    __attribute__((aligned(8))) uint32_t stack[40]; // aligned to 8 bytes so that stack_ptr is aligned to 8 bytes (following ACPSR)
    uint8_t *stack_ptr;
} neo_thread_t;

void init_thread(neo_thread_t *thread, void (*thread_function)(void *arg), void *thread_function_arg);
