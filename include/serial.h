#ifndef NOVA_SERIAL_H
#define NOVA_SERIAL_H

#include "types.h"

void serial_init(void);
void serial_write(const char *text);

/* Fixed-width hex, zero padded to `digits` (1..8). No "0x" prefix is added. */
void serial_write_hex(uint32_t value, int digits);
void serial_write_dec(uint32_t value);

#endif
