#ifndef NOVA_UI_H
#define NOVA_UI_H

#include "input.h"
#include "types.h"

void ui_init(void);
void ui_mouse(int x, int y, uint8_t buttons);
void ui_key(char character, SpecialKey key);
void ui_set_time(uint8_t hour, uint8_t minute, uint8_t second);
void ui_poll_services(void);
bool ui_needs_render(void);
void ui_render(void);

#endif
