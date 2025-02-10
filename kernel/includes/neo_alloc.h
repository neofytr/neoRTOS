#ifndef NEO_ALLOC_H
#define NEO_ALLOC_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

void neo_heap_init(void);
void *neo_alloc(uint32_t size);
void neo_free(void *ptr);

#endif // NEO_ALLOC_H