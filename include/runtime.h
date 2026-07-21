#ifndef NOVA_RUNTIME_H
#define NOVA_RUNTIME_H

#include "types.h"

void *memset(void *destination, int value, size_t count);
void *memcpy(void *destination, const void *source, size_t count);
void *memmove(void *destination, const void *source, size_t count);
int memcmp(const void *left, const void *right, size_t count);
size_t strlen(const char *text);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t count);
char *strcpy(char *destination, const char *source);
char *strncpy(char *destination, const char *source, size_t count);
void int_to_string(int value, char *output);

#endif
