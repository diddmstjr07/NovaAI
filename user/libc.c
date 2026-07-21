#include "types.h"

size_t strlen(const char *text) {
    size_t length = 0;
    while (text[length]) ++length;
    return length;
}

void *memcpy(void *destination, const void *source, size_t count) {
    uint8_t *output = destination;
    const uint8_t *input = source;
    while (count--) *output++ = *input++;
    return destination;
}

void *memmove(void *destination, const void *source, size_t count) {
    uint8_t *output = destination;
    const uint8_t *input = source;
    if (output < input) return memcpy(destination, source, count);
    while (count--) output[count] = input[count];
    return destination;
}

void *memset(void *destination, int value, size_t count) {
    uint8_t *output = destination;
    while (count--) *output++ = (uint8_t)value;
    return destination;
}

int memcmp(const void *left, const void *right, size_t count) {
    const uint8_t *a = left;
    const uint8_t *b = right;
    while (count--) {
        if (*a != *b) return *a - *b;
        ++a;
        ++b;
    }
    return 0;
}

static long system_call3(long number, long argument1, long argument2, long argument3) {
    long result;
    __asm__ volatile ("syscall" : "=a"(result) : "a"(number), "D"(argument1),
                      "S"(argument2), "d"(argument3) : "rcx", "r11", "memory");
    return result;
}

static long system_call6(long number, long argument1, long argument2, long argument3,
                         long argument4, long argument5, long argument6) {
    register long fourth __asm__("r10") = argument4;
    register long fifth __asm__("r8") = argument5;
    register long sixth __asm__("r9") = argument6;
    long result;
    __asm__ volatile ("syscall" : "=a"(result) : "a"(number), "D"(argument1),
                      "S"(argument2), "d"(argument3), "r"(fourth), "r"(fifth),
                      "r"(sixth) : "rcx", "r11", "memory");
    return result;
}

static int nova_errno;

int *__errno_location(void) { return &nova_errno; }

static long libc_result(long result) {
    if (result < 0 && result >= -4095) {
        nova_errno = (int)-result;
        return -1;
    }
    return result;
}

long write(int descriptor, const void *buffer, size_t count) {
    return libc_result(system_call3(1, descriptor, (long)buffer, (long)count));
}

int puts(const char *text) {
    long first = write(1, text, strlen(text));
    if (first < 0) return -1;
    return write(1, "\n", 1) < 0 ? -1 : 0;
}

size_t strnlen(const char *text, size_t maximum) {
    size_t length = 0;
    while (length < maximum && text[length]) ++length;
    return length;
}

int strcmp(const char *left, const char *right) {
    while (*left && *left == *right) {
        ++left;
        ++right;
    }
    return (uint8_t)*left - (uint8_t)*right;
}

int strncmp(const char *left, const char *right, size_t count) {
    while (count && *left && *left == *right) {
        ++left;
        ++right;
        --count;
    }
    return count ? (uint8_t)*left - (uint8_t)*right : 0;
}

char *strcpy(char *destination, const char *source) {
    char *result = destination;
    while ((*destination++ = *source++)) {}
    return result;
}

char *strncpy(char *destination, const char *source, size_t count) {
    char *result = destination;
    while (count && *source) {
        *destination++ = *source++;
        --count;
    }
    while (count--) *destination++ = 0;
    return result;
}

char *strcat(char *destination, const char *source) {
    strcpy(destination + strlen(destination), source);
    return destination;
}

char *strncat(char *destination, const char *source, size_t count) {
    char *output = destination + strlen(destination);
    while (count-- && *source) *output++ = *source++;
    *output = 0;
    return destination;
}

char *strchr(const char *text, int character) {
    do {
        if (*text == (char)character) return (char *)text;
    } while (*text++);
    return NULL;
}

char *strrchr(const char *text, int character) {
    const char *result = NULL;
    do {
        if (*text == (char)character) result = text;
    } while (*text++);
    return (char *)result;
}

char *strstr(const char *text, const char *needle) {
    if (!*needle) return (char *)text;
    size_t length = strlen(needle);
    while (*text) {
        if (!strncmp(text, needle, length)) return (char *)text;
        ++text;
    }
    return NULL;
}

void *memchr(const void *memory, int character, size_t count) {
    const uint8_t *bytes = memory;
    while (count--) {
        if (*bytes == (uint8_t)character) return (void *)bytes;
        ++bytes;
    }
    return NULL;
}

typedef struct HeapBlock {
    size_t size;
    bool free;
    struct HeapBlock *next;
} HeapBlock;

static HeapBlock *heap_blocks;
static volatile uint32_t heap_lock;

static void lock_heap(void) {
    while (__atomic_exchange_n(&heap_lock, 1, __ATOMIC_ACQUIRE)) {
        system_call6(24, 0, 0, 0, 0, 0, 0);
    }
}

static void unlock_heap(void) {
    __atomic_store_n(&heap_lock, 0, __ATOMIC_RELEASE);
}

void *malloc(size_t size) {
    if (!size) size = 1;
    size = (size + 15) & ~(size_t)15;
    if (size > (size_t)-1 - sizeof(HeapBlock)) {
        nova_errno = 12;
        return NULL;
    }
    lock_heap();
    HeapBlock *last = NULL;
    for (HeapBlock *block = heap_blocks; block; block = block->next) {
        if (block->free && block->size >= size) {
            block->free = false;
            unlock_heap();
            return block + 1;
        }
        last = block;
    }
    uintptr_t current = (uintptr_t)system_call6(12, 0, 0, 0, 0, 0, 0);
    uintptr_t requested = current + sizeof(HeapBlock) + size;
    if (requested < current || (uintptr_t)system_call6(12, requested, 0, 0, 0, 0, 0) !=
                               requested) {
        unlock_heap();
        nova_errno = 12;
        return NULL;
    }
    HeapBlock *block = (HeapBlock *)current;
    block->size = size;
    block->free = false;
    block->next = NULL;
    if (last) last->next = block;
    else heap_blocks = block;
    unlock_heap();
    return block + 1;
}

void free(void *memory) {
    if (!memory) return;
    lock_heap();
    HeapBlock *block = (HeapBlock *)memory - 1;
    block->free = true;
    unlock_heap();
}

void *calloc(size_t count, size_t size) {
    if (size && count > (size_t)-1 / size) {
        nova_errno = 12;
        return NULL;
    }
    size_t total = count * size;
    void *memory = malloc(total);
    if (memory) memset(memory, 0, total);
    return memory;
}

void *realloc(void *memory, size_t size) {
    if (!memory) return malloc(size);
    if (!size) {
        free(memory);
        return NULL;
    }
    HeapBlock *block = (HeapBlock *)memory - 1;
    if (block->size >= size) return memory;
    void *replacement = malloc(size);
    if (!replacement) return NULL;
    memcpy(replacement, memory, block->size);
    free(memory);
    return replacement;
}

char *strdup(const char *text) {
    size_t size = strlen(text) + 1;
    char *copy = malloc(size);
    if (copy) memcpy(copy, text, size);
    return copy;
}

char *strndup(const char *text, size_t maximum) {
    size_t length = strnlen(text, maximum);
    char *copy = malloc(length + 1);
    if (!copy) return NULL;
    memcpy(copy, text, length);
    copy[length] = 0;
    return copy;
}

long read(int descriptor, void *buffer, size_t count) {
    return libc_result(system_call3(0, descriptor, (long)buffer, (long)count));
}
int close(int descriptor) { return (int)libc_result(system_call6(3, descriptor, 0, 0, 0, 0, 0)); }
long lseek(int descriptor, long offset, int origin) {
    return libc_result(system_call3(8, descriptor, offset, origin));
}
int openat(int directory, const char *path, int flags, ...) {
    return (int)libc_result(system_call6(257, directory, (long)path, flags, 0666, 0, 0));
}
int open(const char *path, int flags, ...) { return openat(-100, path, flags, 0666); }

void *mmap(void *address, size_t size, int protection, int flags,
           int descriptor, long offset) {
    long result = system_call6(9, (long)address, (long)size, protection, flags,
                               descriptor, offset);
    if (result < 0 && result >= -4095) {
        nova_errno = (int)-result;
        return (void *)-1;
    }
    return (void *)result;
}
int munmap(void *address, size_t size) {
    return (int)libc_result(system_call6(11, (long)address, (long)size, 0, 0, 0, 0));
}
int mprotect(void *address, size_t size, int protection) {
    return (int)libc_result(system_call3(10, (long)address, (long)size, protection));
}

int getpid(void) { return (int)system_call6(39, 0, 0, 0, 0, 0, 0); }
int gettid(void) { return (int)system_call6(186, 0, 0, 0, 0, 0, 0); }
int sched_yield(void) { return (int)libc_result(system_call6(24, 0, 0, 0, 0, 0, 0)); }
int nanosleep(const void *request, void *remaining) {
    return (int)libc_result(system_call6(35, (long)request, (long)remaining, 0, 0, 0, 0));
}
int clock_gettime(int clock, void *time) {
    return (int)libc_result(system_call6(228, clock, (long)time, 0, 0, 0, 0));
}
int prctl(int operation, unsigned long a2, unsigned long a3,
          unsigned long a4, unsigned long a5) {
    return (int)libc_result(system_call6(157, operation, a2, a3, a4, a5, 0));
}

void _exit(int status) {
    system_call6(60, status, 0, 0, 0, 0, 0);
    for (;;) {}
}
void exit(int status) {
    system_call6(231, status, 0, 0, 0, 0, 0);
    for (;;) {}
}
void abort(void) {
    system_call6(62, getpid(), 6, 0, 0, 0, 0);
    _exit(134);
}

char *getenv(const char *name) {
    (void)name;
    return NULL;
}
char *secure_getenv(const char *name) { return getenv(name); }
int getpagesize(void) { return 4096; }
long sysconf(int name) { return name == 30 ? 4096 : -1; }
unsigned long getauxval(unsigned long type) { return type == 6 ? 4096 : 0; }

int atoi(const char *text) {
    int sign = 1;
    int value = 0;
    while (*text == ' ' || *text == '\t') ++text;
    if (*text == '-') { sign = -1; ++text; }
    else if (*text == '+') ++text;
    while (*text >= '0' && *text <= '9') value = value * 10 + (*text++ - '0');
    return value * sign;
}

long strtol(const char *text, char **end, int base) {
    while (*text == ' ' || *text == '\t') ++text;
    bool negative = false;
    if (*text == '-' || *text == '+') negative = *text++ == '-';
    if (!base) base = (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) ? 16 : 10;
    if (base == 16 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) text += 2;
    unsigned long value = 0;
    const char *cursor = text;
    for (;;) {
        int digit;
        if (*cursor >= '0' && *cursor <= '9') digit = *cursor - '0';
        else if (*cursor >= 'a' && *cursor <= 'z') digit = *cursor - 'a' + 10;
        else if (*cursor >= 'A' && *cursor <= 'Z') digit = *cursor - 'A' + 10;
        else break;
        if (digit >= base) break;
        value = value * (unsigned)base + (unsigned)digit;
        ++cursor;
    }
    if (end) *end = (char *)cursor;
    return negative ? -(long)value : (long)value;
}

void qsort(void *base, size_t count, size_t width,
           int (*compare)(const void *, const void *)) {
    uint8_t *bytes = base;
    for (size_t index = 1; index < count; ++index) {
        for (size_t current = index; current &&
             compare(bytes + (current - 1) * width, bytes + current * width) > 0;
             --current) {
            for (size_t byte = 0; byte < width; ++byte) {
                uint8_t swap = bytes[(current - 1) * width + byte];
                bytes[(current - 1) * width + byte] = bytes[current * width + byte];
                bytes[current * width + byte] = swap;
            }
        }
    }
}

void *bsearch(const void *key, const void *base, size_t count, size_t width,
              int (*compare)(const void *, const void *)) {
    const uint8_t *bytes = base;
    size_t first = 0;
    while (count) {
        size_t middle = first + count / 2;
        int order = compare(key, bytes + middle * width);
        if (!order) return (void *)(bytes + middle * width);
        if (order < 0) count /= 2;
        else {
            first = middle + 1;
            count -= count / 2 + 1;
        }
    }
    return NULL;
}

void __stack_chk_fail(void) { abort(); }
int atexit(void (*function)(void)) { (void)function; return 0; }
void __cxa_finalize(void *handle) { (void)handle; }
