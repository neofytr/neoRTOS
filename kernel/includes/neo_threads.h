#include <stdint.h>
#include <stdbool.h>

typedef struct
{
    uint8_t *stack_ptr;
} neo_thread_t;

bool neo_thread_init(neo_thread_t *thread, void (*thread_function)(void *), void *thread_arg, uint8_t *stack, uint32_t stack_size);
void neo_kernel_init(void);
void neo_start_threads(void);