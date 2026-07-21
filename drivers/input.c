#include "input.h"
#include "io.h"

static int pointer_x;
static int pointer_y;
static int pointer_limit_x;
static int pointer_limit_y;
static uint8_t pointer_buttons;
static uint8_t mouse_packet[3];
static uint8_t mouse_index;
static bool left_shift;
static bool right_shift;
static bool caps_lock;
static bool extended_key;

static const char normal_map[128] = {
    [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',[0x07]='6',
    [0x08]='7',[0x09]='8',[0x0A]='9',[0x0B]='0',[0x0C]='-',[0x0D]='=',
    [0x10]='q',[0x11]='w',[0x12]='e',[0x13]='r',[0x14]='t',[0x15]='y',
    [0x16]='u',[0x17]='i',[0x18]='o',[0x19]='p',[0x1A]='[',[0x1B]=']',
    [0x1E]='a',[0x1F]='s',[0x20]='d',[0x21]='f',[0x22]='g',[0x23]='h',
    [0x24]='j',[0x25]='k',[0x26]='l',[0x27]=';',[0x28]='\'',[0x29]='`',
    [0x2B]='\\',[0x2C]='z',[0x2D]='x',[0x2E]='c',[0x2F]='v',[0x30]='b',
    [0x31]='n',[0x32]='m',[0x33]=',',[0x34]='.',[0x35]='/',[0x39]=' '
};

static const char shifted_map[128] = {
    [0x02]='!',[0x03]='@',[0x04]='#',[0x05]='$',[0x06]='%',[0x07]='^',
    [0x08]='&',[0x09]='*',[0x0A]='(',[0x0B]=')',[0x0C]='_',[0x0D]='+',
    [0x10]='Q',[0x11]='W',[0x12]='E',[0x13]='R',[0x14]='T',[0x15]='Y',
    [0x16]='U',[0x17]='I',[0x18]='O',[0x19]='P',[0x1A]='{',[0x1B]='}',
    [0x1E]='A',[0x1F]='S',[0x20]='D',[0x21]='F',[0x22]='G',[0x23]='H',
    [0x24]='J',[0x25]='K',[0x26]='L',[0x27]=':',[0x28]='"',[0x29]='~',
    [0x2B]='|',[0x2C]='Z',[0x2D]='X',[0x2E]='C',[0x2F]='V',[0x30]='B',
    [0x31]='N',[0x32]='M',[0x33]='<',[0x34]='>',[0x35]='?',[0x39]=' '
};

static bool wait_to_write(void) {
    for (uint32_t timeout = 0; timeout < 100000; ++timeout) {
        if (!(inb(0x64) & 0x02)) return true;
    }
    return false;
}

static bool wait_to_read(void) {
    for (uint32_t timeout = 0; timeout < 100000; ++timeout) {
        if (inb(0x64) & 0x01) return true;
    }
    return false;
}

static void controller_command(uint8_t command) {
    if (wait_to_write()) outb(0x64, command);
}

static void controller_data(uint8_t value) {
    if (wait_to_write()) outb(0x60, value);
}

static bool mouse_command(uint8_t command) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        controller_command(0xD4);
        controller_data(command);
        if (!wait_to_read()) continue;
        uint8_t response = inb(0x60);
        if (response == 0xFA) return true;
        if (response != 0xFE) return false;
    }
    return false;
}

void input_init(int width, int height) {
    pointer_limit_x = width;
    pointer_limit_y = height;
    pointer_x = width / 2;
    pointer_y = height / 2;

    controller_command(0xAD);
    controller_command(0xA7);
    while (inb(0x64) & 0x01) (void)inb(0x60);

    controller_command(0x20);
    uint8_t configuration = wait_to_read() ? inb(0x60) : 0;
    configuration &= (uint8_t)~0x03;
    configuration |= 0x40;
    configuration &= (uint8_t)~0x20;
    controller_command(0x60);
    controller_data(configuration);

    controller_command(0xAE);
    controller_command(0xA8);
    (void)mouse_command(0xF6);
    (void)mouse_command(0xF4);

    while (inb(0x64) & 0x01) (void)inb(0x60);
}

static bool process_mouse(uint8_t data, InputEvent *event) {
    if (mouse_index == 0 && !(data & 0x08)) return false;
    mouse_packet[mouse_index++] = data;
    if (mouse_index != 3) return false;
    mouse_index = 0;

    if (mouse_packet[0] & 0xC0) return false;
    pointer_x += (int8_t)mouse_packet[1];
    pointer_y -= (int8_t)mouse_packet[2];
    if (pointer_x < 0) pointer_x = 0;
    if (pointer_y < 0) pointer_y = 0;
    if (pointer_x >= pointer_limit_x) pointer_x = pointer_limit_x - 1;
    if (pointer_y >= pointer_limit_y) pointer_y = pointer_limit_y - 1;
    pointer_buttons = mouse_packet[0] & 0x07;

    event->type = INPUT_MOUSE;
    event->mouse_x = pointer_x;
    event->mouse_y = pointer_y;
    event->mouse_buttons = pointer_buttons;
    return true;
}

static SpecialKey extended_special(uint8_t code) {
    switch (code) {
        case 0x47: return KEY_HOME;
        case 0x4B: return KEY_LEFT;
        case 0x4D: return KEY_RIGHT;
        case 0x4F: return KEY_END;
        case 0x48: return KEY_UP;
        case 0x50: return KEY_DOWN;
        case 0x53: return KEY_DELETE;
        default: return KEY_NONE;
    }
}

static bool process_keyboard(uint8_t data, InputEvent *event) {
    if (data == 0xE0) {
        extended_key = true;
        return false;
    }

    bool released = (data & 0x80) != 0;
    uint8_t code = data & 0x7F;
    if (code == 0x2A) { left_shift = !released; extended_key = false; return false; }
    if (code == 0x36) { right_shift = !released; extended_key = false; return false; }
    if (released) { extended_key = false; return false; }
    if (code == 0x3A) { caps_lock = !caps_lock; extended_key = false; return false; }

    event->type = INPUT_KEY;
    event->character = 0;
    event->key = KEY_NONE;

    if (extended_key) {
        event->key = extended_special(code);
        extended_key = false;
        return event->key != KEY_NONE;
    }

    switch (code) {
        case 0x01: event->key = KEY_ESCAPE; return true;
        case 0x0E: event->key = KEY_BACKSPACE; return true;
        case 0x0F: event->key = KEY_TAB; return true;
        case 0x1C: event->key = KEY_ENTER; return true;
        default: break;
    }

    bool shifted = left_shift || right_shift;
    char character = shifted ? shifted_map[code] : normal_map[code];
    if (character >= 'a' && character <= 'z' && caps_lock) character -= 'a' - 'A';
    else if (character >= 'A' && character <= 'Z' && caps_lock) character += 'a' - 'A';
    if (!character) return false;
    event->character = character;
    return true;
}

bool input_poll(InputEvent *event) {
    event->type = INPUT_NONE;
    /* Consume partial packets internally; return only complete UI events. */
    for (int bytes = 0; bytes < 32; ++bytes) {
        uint8_t status = inb(0x64);
        if (!(status & 0x01)) return false;
        uint8_t data = inb(0x60);
        bool complete = (status & 0x20) ? process_mouse(data, event)
                                        : process_keyboard(data, event);
        if (complete) return true;
    }
    return false;
}
