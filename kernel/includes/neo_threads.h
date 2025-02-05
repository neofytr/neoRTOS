#include <stdint.h>
#include <stdbool.h>

#define STACK_SIZE (100) // in words (32-bits)

typedef struct
{
    uint8_t *stack_ptr;
    __attribute__((aligned(8))) uint32_t stack[STACK_SIZE]; // aligned to 8 bytes so that stack_ptr is aligned to 8 bytes (following ACPSR)
} neo_thread_t;

bool init_thread(neo_thread_t *thread, void (*thread_function)(void *arg), void *thread_function_arg);
void init_neo();
void thread_handler();
