#include "neo_alloc.h"
#include "core_cm4.h"

extern uint32_t _heap_start;

/**
 * Configuration constants for the heap allocator
 */
#define HEAP_SIZE 0x400  // 1KB Heap size
#define SPLIT_CUTOFF 16  // Minimum size for splitting chunks
#define DEFRAG_CUTOFF 10 // Number of frees before triggering defragmentation

/**
 * Chunk header structure (4 bytes)
 * byte 0: allocation status (0 = free, 1 = allocated)
 * bytes 1-2: chunk size (excluding header)
 * byte 3: padding/unused
 */
typedef struct
{
    uint8_t allocated;
    uint16_t size;
    uint8_t padding;
} ChunkHeader;

// Base pointer for the heap
static uint8_t *const heap_base = (uint8_t *)&_heap_start;
static volatile uint8_t free_calls = 0;

// Helper macro to get chunk header at a given index
#define GET_HEADER(index) ((ChunkHeader *)&heap_base[index])

/**
 * Coalesces adjacent free chunks to reduce fragmentation.
 * This function is called automatically after DEFRAG_CUTOFF number of frees.
 */
static void defragment(void)
{
    uint32_t curr_index = 0;

    while (curr_index < HEAP_SIZE)
    {
        ChunkHeader *curr = GET_HEADER(curr_index);

        if (!curr->allocated)
        {
            uint32_t next_index = curr_index + sizeof(ChunkHeader) + curr->size;
            if (next_index >= HEAP_SIZE)
                break;

            ChunkHeader *next = GET_HEADER(next_index);
            if (!next->allocated)
            {
                // Merge with next chunk
                curr->size += sizeof(ChunkHeader) + next->size;
                continue;
            }
        }

        curr_index += sizeof(ChunkHeader) + curr->size;
    }
}

/**
 * Initializes the heap by creating a single free chunk spanning the entire heap.
 * Must be called before any allocation operations.
 */
void neo_heap_init(void)
{
    __disable_irq();
    ChunkHeader *initial = GET_HEADER(0);
    initial->allocated = 0;
    initial->size = HEAP_SIZE - sizeof(ChunkHeader);
    __enable_irq();
}

/**
 * Allocates a block of memory from the heap.
 *
 * @param size Requested size in bytes
 * @return Pointer to allocated memory, or NULL if allocation fails
 *
 * Features:
 * - 4-byte alignment guaranteed
 * - Split chunks when possible to reduce fragmentation
 * - Thread-safe through interrupt disabling
 */
void *neo_alloc(uint16_t size)
{
    __disable_irq();

    // Round size up to nearest multiple of 4 for alignment
    uint16_t aligned_size = (size + 3) & ~3;
    if (aligned_size == 0)
        aligned_size = 4;

    uint32_t curr_index = 0;
    while (curr_index < HEAP_SIZE)
    {
        ChunkHeader *curr = GET_HEADER(curr_index);

        if (!curr->allocated && curr->size >= aligned_size)
        {
            // Check if chunk should be split
            if (curr->size >= aligned_size + sizeof(ChunkHeader) + SPLIT_CUTOFF)
            {
                // Split chunk
                ChunkHeader *new_chunk = GET_HEADER(curr_index + sizeof(ChunkHeader) + aligned_size);
                new_chunk->allocated = 0;
                new_chunk->size = curr->size - aligned_size - sizeof(ChunkHeader);

                curr->allocated = 1;
                curr->size = aligned_size;
            }
            else
            {
                // Use entire chunk
                curr->allocated = 1;
            }

            __enable_irq();
            return (void *)&heap_base[curr_index + sizeof(ChunkHeader)];
        }

        curr_index += sizeof(ChunkHeader) + curr->size;
    }

    __enable_irq();
    return NULL;
}

/**
 * Frees a previously allocated block of memory.
 *
 * @param ptr Pointer to memory block to free
 *
 * Features:
 * - Validates pointer before freeing
 * - Automatic defragmentation after DEFRAG_CUTOFF frees
 * - Thread-safe through interrupt disabling
 */
void neo_free(void *ptr)
{
    __disable_irq();

    if (!ptr || (uint8_t *)ptr < heap_base || (uint8_t *)ptr >= heap_base + HEAP_SIZE)
    {
        __enable_irq();
        return;
    }

    ChunkHeader *header = GET_HEADER(((uint8_t *)ptr - heap_base) - sizeof(ChunkHeader));

    if (!header->allocated)
    {
        __enable_irq();
        return;
    }

    header->allocated = 0;

    if (++free_calls >= DEFRAG_CUTOFF)
    {
        defragment();
        free_calls = 0;
    }

    __enable_irq();
}