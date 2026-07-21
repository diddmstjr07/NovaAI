#include "runtime.h"

void *memset(void *destination, int value, size_t count) {
    uint8_t *bytes = (uint8_t *)destination;
    while (count--) {
        *bytes++ = (uint8_t)value;
    }
    return destination;
}

void *memcpy(void *destination, const void *source, size_t count) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    while (count--) {
        *out++ = *in++;
    }
    return destination;
}

void *memmove(void *destination, const void *source, size_t count) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    if (out < in) {
        return memcpy(destination, source, count);
    }
    while (count--) {
        out[count] = in[count];
    }
    return destination;
}

int memcmp(const void *left, const void *right, size_t count) {
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    while (count--) {
        if (*a != *b) return *a - *b;
        ++a;
        ++b;
    }
    return 0;
}

size_t strlen(const char *text) {
    size_t length = 0;
    while (text[length]) {
        ++length;
    }
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
    while ((*destination++ = *source++)) {
    }
    return result;
}

char *strncpy(char *destination, const char *source, size_t count) {
    char *result = destination;
    while (count && *source) {
        *destination++ = *source++;
        --count;
    }
    while (count--) {
        *destination++ = 0;
    }
    return result;
}

void int_to_string(int value, char *output) {
    char reversed[16];
    int index = 0;
    int negative = value < 0;
    uint32_t number = negative ? 0u - (uint32_t)value : (uint32_t)value;

    do {
        reversed[index++] = (char)('0' + number % 10);
        number /= 10;
    } while (number);

    if (negative) {
        reversed[index++] = '-';
    }
    while (index) {
        *output++ = reversed[--index];
    }
    *output = 0;
}
