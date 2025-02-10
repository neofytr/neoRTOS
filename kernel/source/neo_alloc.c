#include "neo_alloc.h"
#include "core_cm4.h"

extern uint32_t _heap_start;

#define HEAP_SIZE 0x400 // 1KB Heap
#define SPLIT_CUTOFF 16 // if the size of the remaining chunk is less than this, don't split it

// we can assume that the heap is an array of bytes defined as uint8_t *neo_heap[HEAP_SIZE]
// the macro HEAP(index) is equivalent to neo_heap[index]
// index must be strictly less than HEAP_SIZE and greater than or equal to 0
#define HEAP(index) (((uint8_t *)&_heap_start)[(index)])
#define DEFRAG_CUTOFF (10) // after every 10 free calls, defragment the heap

static void defragment(void);
static volatile uint8_t free_calls = 0;

/* The allocations are in chunks of 4 bytes only; irrespective of the size of the allocation asked */

// the start of the heap is 8 bytes aligned
// all allocations are gauranteed to be at least 4 bytes aligned since allocations are in chunks of 4 bytes

// each allocated chunk is preceded by it's metadata of 4 bytes
// the first byte tells if the chunk is allocated or not (0 is for unallocated; 1 is for allocated)
// the second and third byte tells the size of the chunk (in bytes) (not including the metadata)
// the fourth byte is unused/padding

static void defragment(void)
{
    uint32_t curr_index = 0;
    while (true)
    {
        if (!HEAP(curr_index))
        {
            // the current chunk is unallocated
            uint32_t next_index = (curr_index) + 4 + *(uint16_t *)&HEAP(curr_index + 1);
            if (next_index >= HEAP_SIZE)
            {
                // we've reached the end of the heap
                break;
            }

            if (!HEAP(next_index))
            {
                // the next chunk is also unallocated
                // coallesce the two chunks
                *(uint16_t *)&HEAP(curr_index + 1) += 4 + *(uint16_t *)&HEAP(next_index + 1);
                curr_index += 4 + *(uint16_t *)&HEAP(curr_index + 1);
                if (curr_index >= HEAP_SIZE)
                {
                    // we've reached the end of the heap
                    break;
                }
                continue;
            }
            else
            {
                // the next chunk is allocated
                curr_index = next_index + 4 + *(uint16_t *)&HEAP(next_index + 1);
                if (curr_index >= HEAP_SIZE)
                {
                    // we've reached the end of the heap
                    break;
                }
            }
        }
    }
}

void neo_heap_init(void)
{
    __disable_irq();
    HEAP(0) = 0;
    HEAP(1) = HEAP_SIZE - 4;
    HEAP(2) = 0;
    __enable_irq();
}

void *neo_alloc(uint16_t size)
{
    // a context switch in between the neo_alloc function can cause the heap to be corrupted
    // so disable interrupts before entering the main function code
    __disable_irq();

    uint16_t actual_allocated_size = (size) & 0xFFFFFFFC; // round down to the nearest multiple of 4
    if (!actual_allocated_size)
    {
        actual_allocated_size = 4;
    }

    uint32_t curr_index = 0;
    while (true)
    {
        if (!HEAP(curr_index)) // the current chunk we're at is unallocated
        {
            if (*(uint16_t *)&HEAP(curr_index + 1) >= actual_allocated_size)
            {
                // the current chunk we're at is unallocated and has enough space to allocate
                if (*(uint16_t *)&HEAP(curr_index + 1) - actual_allocated_size >= SPLIT_CUTOFF)
                {
                    // the curent chunk also has enough space to split
                    HEAP(curr_index) = 1;
                    *(uint16_t *)&HEAP(curr_index + 1) = actual_allocated_size;

                    HEAP(curr_index + 4 + actual_allocated_size) = 0;
                    *(uint16_t *)&HEAP(curr_index + 4 + actual_allocated_size + 1) = *(uint16_t *)&HEAP(curr_index + 1) - actual_allocated_size;

                    __enable_irq();
                    return &HEAP(curr_index + 4);
                }
                else
                {
                    // the current chunk has enough space to allocate but not enough to split
                    HEAP(curr_index) = 1;
                    __enable_irq();
                    return &HEAP(curr_index + 4);
                }
            }
        }
        else
        {
            // the current chunk we're at is allocated
            curr_index += (4 + *(uint16_t *)&HEAP(curr_index + 1));
            if (curr_index >= HEAP_SIZE)
            {
                // we've reached the end of the heap
                break;
            }
        }
    }

    // we've reached the end of the heap and couldn't find a suitable chunk to allocate
    __enable_irq();
    return NULL;
    // it's now safe for a context switch to occur; enable interrupts
}

void free(void *ptr)
{
    // a context switch in between the neo_alloc function can cause the heap to be corrupted
    // so disable interrupts before entering the main function code
    __disable_irq();

    if (!ptr)
    {
        __enable_irq();
        return;
    }

    if ((uintptr_t)ptr < (uintptr_t)&_heap_start || (uintptr_t)ptr >= (uintptr_t)&_heap_start + HEAP_SIZE)
    {
        // the pointer is not within the heap
        __enable_irq();
        return;
    }

    uint32_t metadata_index = (uint32_t)ptr - (uint32_t)&_heap_start - 4;
    if (!HEAP(metadata_index))
    {
        // the chunk is already unallocated
        __enable_irq();
        return;
    }
    else
    {
        // the chunk is allocated; unallocate it
        HEAP(metadata_index) = 0;
    }

    free_calls++;
    if (free_calls == DEFRAG_CUTOFF)
    {
        defragment();
        free_calls = 0;
    }

    // it's now safe for a context switch to occur; enable interrupts
    __enable_irq();
    return;
}