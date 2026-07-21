#ifndef NOVA_INPUT_H
#define NOVA_INPUT_H

#include "types.h"

typedef enum {
    INPUT_NONE,
    INPUT_MOUSE,
    INPUT_KEY
} InputEventType;

typedef enum {
    KEY_NONE,
    KEY_ESCAPE,
    KEY_BACKSPACE,
    KEY_TAB,
    KEY_ENTER,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_DELETE,
    KEY_HOME,
    KEY_END
} SpecialKey;

typedef struct {
    InputEventType type;
    int mouse_x;
    int mouse_y;
    uint8_t mouse_buttons;
    char character;
    SpecialKey key;
} InputEvent;

void input_init(int width, int height);
bool input_poll(InputEvent *event);

#endif
