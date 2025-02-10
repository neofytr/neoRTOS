#include "neo_alloc.h"

extern uint32_t _heap_start;

// the heap is a simple array of bytes
// we can access it using the HEAP macro
// HEAP(index) will return the byte at position index in the heap
#define HEAP(index) (((uint8_t *)&_heap_start)[(index)])

void *neo_malloc();