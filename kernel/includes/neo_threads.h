#include <stdint.h>
#include <stdbool.h>

#define STACK_SIZE (100) // in words (32-bits)

/* Thread Control Block (TCB) */
typedef struct
{
    uint8_t *stack_ptr; // must be aligned to 8 bytes (following AAPCS)
} neo_thread_t;

bool neo_thread_start(neo_thread_t *thread, void (*thread_function)(void *arg), void *thread_function_arg, uint8_t *stack, uint32_t stack_size); // stack size in bytes
void neo_kernel_init(void);
void neo_thread_scheduler(void);
void thread_handler(void);
