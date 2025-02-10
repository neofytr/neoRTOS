#include "neo_alloc.h"
#include "core_cm4.h"

// Declare _heap_start as a pointer to the start of the heap region
extern uint8_t _heap_start[];

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
} __attribute__((packed)) ChunkHeader; // Added packed attribute

// Define heap bounds
static uint8_t *const heap_start = &_heap_start[0];
static uint8_t *const heap_end = &_heap_start[HEAP_SIZE];

static volatile uint8_t free_calls = 0;

/**
 * Helper function to validate a chunk header pointer
 */
static bool is_valid_header(const ChunkHeader *header)
{
    return ((uint8_t *)header >= heap_start &&
            (uint8_t *)header + sizeof(ChunkHeader) <= heap_end);
}

/**
 * Helper function to get chunk header at a given offset
 */
static ChunkHeader *get_header(size_t offset)
{
    if (offset >= HEAP_SIZE - sizeof(ChunkHeader))
        return NULL;
    return (ChunkHeader *)(heap_start + offset);
}

/**
 * Coalesces adjacent free chunks to reduce fragmentation.
 */
static void defragment(void)
{
    size_t curr_offset = 0;

    while (curr_offset < HEAP_SIZE)
    {
        ChunkHeader *curr = get_header(curr_offset);
        if (!curr)
            break;

        if (!curr->allocated)
        {
            size_t next_offset = curr_offset + sizeof(ChunkHeader) + curr->size;
            ChunkHeader *next = get_header(next_offset);

            if (next && !next->allocated)
            {
                // Merge with next chunk
                curr->size += sizeof(ChunkHeader) + next->size;
                continue;
            }
        }

        curr_offset += sizeof(ChunkHeader) + curr->size;
    }
}

/**
 * Initializes the heap
 */
void neo_heap_init(void)
{
    __disable_irq();
    ChunkHeader *initial = (ChunkHeader *)heap_start;
    initial->allocated = 0;
    initial->size = HEAP_SIZE - sizeof(ChunkHeader);
    initial->padding = 0;
    __enable_irq();
}

/**
 * Allocates memory from the heap
 */
void *neo_alloc(uint16_t size)
{
    if (size == 0)
        return NULL;

    __disable_irq();

    // Round size up to nearest multiple of 4 for alignment
    uint16_t aligned_size = (size + 3) & ~3;

    size_t curr_offset = 0;
    while (curr_offset < HEAP_SIZE)
    {
        ChunkHeader *curr = get_header(curr_offset);
        if (!curr)
            break;

        if (!curr->allocated && curr->size >= aligned_size)
        {
            // Check if chunk should be split
            if (curr->size >= aligned_size + sizeof(ChunkHeader) + SPLIT_CUTOFF)
            {
                size_t new_offset = curr_offset + sizeof(ChunkHeader) + aligned_size;
                ChunkHeader *new_chunk = get_header(new_offset);

                if (new_chunk)
                {
                    new_chunk->allocated = 0;
                    new_chunk->size = curr->size - aligned_size - sizeof(ChunkHeader);
                    new_chunk->padding = 0;

                    curr->allocated = 1;
                    curr->size = aligned_size;
                }
            }
            else
            {
                curr->allocated = 1;
            }

            __enable_irq();
            return heap_start + curr_offset + sizeof(ChunkHeader);
        }

        curr_offset += sizeof(ChunkHeader) + curr->size;
    }

    __enable_irq();
    return NULL;
}

/**
 * Frees allocated memory
 */
void neo_free(void *ptr)
{
    __disable_irq();

    if (!ptr || (uint8_t *)ptr < heap_start || (uint8_t *)ptr >= heap_end)
    {
        __enable_irq();
        return;
    }

    // Get header pointer by subtracting header size from data pointer
    ChunkHeader *header = (ChunkHeader *)((uint8_t *)ptr - sizeof(ChunkHeader));

    if (!is_valid_header(header) || !header->allocated)
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