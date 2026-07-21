#ifndef NOVA_COMPILER_H
#define NOVA_COMPILER_H

#include "types.h"

bool compiler_compile(const char *source, const char *output_name,
                      char *diagnostic, size_t diagnostic_size);

#endif
