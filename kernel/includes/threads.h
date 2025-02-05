#include <stdint.h>

#define STACK_SIZE (40) // in words (32-bits)

typedef void (*thread_function_t)(void);
typedef struct
{
    uint32_t stack[40];
    uint8_t *stack_ptr;
    thread_function_t thread_function;
} thread_t;

