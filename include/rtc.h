#ifndef NOVA_RTC_H
#define NOVA_RTC_H

#include "types.h"

typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} RtcTime;

RtcTime rtc_time(void);

#endif
