#ifndef NOVA_E1000_H
#define NOVA_E1000_H

#include "types.h"

#define E1000_FRAME_MAX 2048

bool e1000_init(void);
bool e1000_send(const void *frame, uint16_t length);
int e1000_receive(void *frame, uint16_t capacity);
const uint8_t *e1000_mac(void);

#endif
