#include "serial.h"
#include "io.h"

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

void serial_write(const char *text) {
    while (*text) {
        while (!(inb(COM1 + 5) & 0x20)) {
        }
        outb(COM1, (uint8_t)*text++);
    }
}

void serial_write_hex(uint32_t value, int digits) {
    static const char alphabet[] = "0123456789ABCDEF";
    char text[9];
    if (digits < 1) digits = 1;
    if (digits > 8) digits = 8;
    for (int index = 0; index < digits; ++index) {
        int shift = (digits - index - 1) * 4;
        text[index] = alphabet[(value >> shift) & 0x0F];
    }
    text[digits] = 0;
    serial_write(text);
}

void serial_write_dec(uint32_t value) {
    char text[11];
    int length = 0;
    do {
        text[length++] = (char)('0' + value % 10);
        value /= 10;
    } while (value && length < 10);
    for (int left = 0, right = length - 1; left < right; ++left, --right) {
        char swap = text[left];
        text[left] = text[right];
        text[right] = swap;
    }
    text[length] = 0;
    serial_write(text);
}
