#ifndef NEO_ALLOC_H
#define NEO_ALLOC_H

#include <stdint.h>

#define HEAP_SIZE 0x400 // 1KB Heap

void *neo_alloc(uint32_t size);
void neo_free(void *ptr);

#endif // NEO_ALLOC_H