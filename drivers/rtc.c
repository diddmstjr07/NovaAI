#include "rtc.h"
#include "io.h"

static uint8_t cmos_read(uint8_t index) {
    outb(0x70, index);
    return inb(0x71);
}

static uint8_t from_bcd(uint8_t value) {
    return (uint8_t)((value & 0x0F) + ((value >> 4) * 10));
}

RtcTime rtc_time(void) {
    RtcTime time;
    RtcTime verify;
    do {
        while (cmos_read(0x0A) & 0x80) {
        }
        time.second = cmos_read(0x00);
        time.minute = cmos_read(0x02);
        time.hour = cmos_read(0x04);
        while (cmos_read(0x0A) & 0x80) {
        }
        verify.second = cmos_read(0x00);
        verify.minute = cmos_read(0x02);
        verify.hour = cmos_read(0x04);
    } while (time.second != verify.second || time.minute != verify.minute ||
             time.hour != verify.hour);

    uint8_t status_b = cmos_read(0x0B);
    bool is_pm = (time.hour & 0x80) != 0;
    if (!(status_b & 0x04)) {
        time.second = from_bcd(time.second);
        time.minute = from_bcd(time.minute);
        time.hour = from_bcd(time.hour & 0x7F);
    }
    if (!(status_b & 0x02)) {
        time.hour %= 12;
        if (is_pm) time.hour += 12;
    }
    return time;
}
