#ifndef NOVA_HEAP_H
#define NOVA_HEAP_H

#include "types.h"

void heap_init(void);
void *heap_alloc(size_t size);
void *heap_calloc(size_t count, size_t size);
void heap_free(void *pointer);
size_t heap_free_bytes(void);

#endif
