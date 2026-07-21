#include "heap.h"
#include "runtime.h"

#define HEAP_START ((uintptr_t)0x02000000)
#define HEAP_END   ((uintptr_t)0x3E000000)
#define HEAP_ALIGNMENT 16

typedef struct HeapBlock {
    size_t size;
    struct HeapBlock *previous;
    struct HeapBlock *next;
    bool free;
} HeapBlock;

static HeapBlock *first_block;

static size_t align_size(size_t size) {
    return (size + HEAP_ALIGNMENT - 1) & ~(size_t)(HEAP_ALIGNMENT - 1);
}

void heap_init(void) {
    first_block = (HeapBlock *)HEAP_START;
    first_block->size = HEAP_END - HEAP_START - sizeof(HeapBlock);
    first_block->previous = NULL;
    first_block->next = NULL;
    first_block->free = true;
}

static void split_block(HeapBlock *block, size_t size) {
    if (block->size < size + sizeof(HeapBlock) + HEAP_ALIGNMENT) return;
    HeapBlock *remainder = (HeapBlock *)((uint8_t *)(block + 1) + size);
    remainder->size = block->size - size - sizeof(HeapBlock);
    remainder->previous = block;
    remainder->next = block->next;
    remainder->free = true;
    if (remainder->next) remainder->next->previous = remainder;
    block->next = remainder;
    block->size = size;
}

void *heap_alloc(size_t size) {
    if (!size || !first_block) return NULL;
    size = align_size(size);
    for (HeapBlock *block = first_block; block; block = block->next) {
        if (!block->free || block->size < size) continue;
        split_block(block, size);
        block->free = false;
        return block + 1;
    }
    return NULL;
}

void *heap_calloc(size_t count, size_t size) {
    if (size && count > (size_t)-1 / size) return NULL;
    size_t total = count * size;
    void *memory = heap_alloc(total);
    if (memory) memset(memory, 0, total);
    return memory;
}

static void merge_next(HeapBlock *block) {
    HeapBlock *next = block->next;
    if (!next || !next->free) return;
    block->size += sizeof(HeapBlock) + next->size;
    block->next = next->next;
    if (block->next) block->next->previous = block;
}

void heap_free(void *pointer) {
    if (!pointer || (uintptr_t)pointer <= HEAP_START || (uintptr_t)pointer >= HEAP_END) return;
    HeapBlock *block = (HeapBlock *)pointer - 1;
    if (block->free) return;
    block->free = true;
    merge_next(block);
    if (block->previous && block->previous->free) merge_next(block->previous);
}

size_t heap_free_bytes(void) {
    size_t total = 0;
    for (HeapBlock *block = first_block; block; block = block->next) {
        if (block->free) total += block->size;
    }
    return total;
}
