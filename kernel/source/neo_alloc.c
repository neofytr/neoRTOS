#include "neo_alloc.h"
#include "core_cm4.h"

// Declare _heap_start as a pointer to the start of the heap region
extern uint8_t _heap_start[];

/**
 * Configuration constants for the heap allocator
 */
#define HEAP_SIZE 0x400  // 1KB total heap size
#define SPLIT_CUTOFF 16  // Minimum remaining size needed to split a chunk into two
#define DEFRAG_CUTOFF 10 // Number of free operations before automatic defragmentation

/**
 * Chunk header structure (4 bytes total)
 * The fields are arranged for optimal memory alignment:
 * - allocated: Indicates if chunk is in use (1 byte)
 * - padding: Reserved for future use (1 byte)
 * - size: Size of the chunk's data area in bytes (2 bytes)
 *
 * This arrangement ensures the size field is 2-byte aligned, which is
 * important for efficient access on ARM processors. The total header
 * remains 4 bytes to maintain alignment of the data portion.
 */
typedef struct
{
    uint8_t allocated; // 0 = free, 1 = allocated
    uint8_t padding;   // Unused, helps with alignment
    uint16_t size;     // Size of chunk data (excluding header)
} __attribute__((packed)) ChunkHeader;

// Define the heap region bounds using external symbol
static uint8_t *const heap_start = &_heap_start[0];
static uint8_t *const heap_end = &_heap_start[HEAP_SIZE];

// Counter for tracking free operations to trigger defragmentation
static volatile uint8_t free_calls = 0;

/**
 * Validates if a chunk header pointer is within the heap bounds
 * and properly aligned.
 *
 * @param header Pointer to the chunk header to validate
 * @return true if header is valid, false otherwise
 */
static bool is_valid_header(const ChunkHeader *header)
{
    return ((uint8_t *)header >= heap_start &&
            (uint8_t *)header + sizeof(ChunkHeader) <= heap_end);
}

/**
 * Retrieves a chunk header at a given offset in the heap.
 *
 * @param offset Byte offset from heap start
 * @return Pointer to chunk header, or NULL if offset is invalid
 */
static ChunkHeader *get_header(size_t offset)
{
    if (offset >= HEAP_SIZE - sizeof(ChunkHeader))
        return NULL;
    return (ChunkHeader *)(heap_start + offset);
}

/**
 * Coalesces adjacent free chunks to reduce memory fragmentation.
 * This function merges consecutive unallocated chunks into larger
 * free chunks, improving allocation efficiency for larger requests.
 *
 * The process:
 * 1. Scans the heap from start to end
 * 2. When finding a free chunk, checks if the next chunk is also free
 * 3. If both chunks are free, merges them by updating the first chunk's size
 * 4. Continues until no more merges are possible
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
                // Merge with next chunk by absorbing its space
                curr->size += sizeof(ChunkHeader) + next->size;
                continue; // Recheck the merged chunk for more possible merges
            }
        }

        curr_offset += sizeof(ChunkHeader) + curr->size;
    }
}

/**
 * Initializes the heap by creating a single large free chunk.
 * This must be called before any allocation operations.
 *
 * The function:
 * 1. Disables interrupts to ensure thread safety
 * 2. Creates an initial free chunk spanning the entire heap
 * 3. Re-enables interrupts
 */
void neo_heap_init(void)
{
    __disable_irq();
    ChunkHeader *initial = (ChunkHeader *)heap_start;
    initial->allocated = 0;
    initial->padding = 0;
    initial->size = HEAP_SIZE - sizeof(ChunkHeader);
    __enable_irq();
}

/**
 * Allocates memory from the heap with proper alignment.
 *
 * The allocation process:
 * 1. Rounds requested size up to maintain 4-byte alignment
 * 2. Searches for first free chunk large enough to hold request
 * 3. If chunk is significantly larger than needed, splits it
 * 4. Returns pointer to the allocated memory region
 *
 * @param size Requested allocation size in bytes
 * @return Pointer to allocated memory, or NULL if allocation fails
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
            // Check if chunk should be split to avoid wasting space
            if (curr->size >= aligned_size + sizeof(ChunkHeader) + SPLIT_CUTOFF)
            {
                size_t new_offset = curr_offset + sizeof(ChunkHeader) + aligned_size;
                ChunkHeader *new_chunk = get_header(new_offset);

                if (new_chunk)
                {
                    // Initialize the new chunk from the split
                    new_chunk->allocated = 0;
                    new_chunk->padding = 0;
                    new_chunk->size = curr->size - aligned_size - sizeof(ChunkHeader);

                    // Update current chunk
                    curr->allocated = 1;
                    curr->size = aligned_size;
                }
            }
            else
            {
                // Use entire chunk if splitting would create too small a remainder
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
 * Frees previously allocated memory and potentially defragments the heap.
 *
 * The free process:
 * 1. Validates the provided pointer
 * 2. Marks the chunk as unallocated
 * 3. Increments free counter and triggers defragmentation if threshold reached
 *
 * @param ptr Pointer to memory region to free
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

    // Trigger defragmentation periodically to prevent excessive fragmentation
    if (++free_calls >= DEFRAG_CUTOFF)
    {
        defragment();
        free_calls = 0;
    }

    __enable_irq();
}