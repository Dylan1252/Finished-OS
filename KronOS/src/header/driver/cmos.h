#ifndef _CMOS_H
#define _CMOS_H

#include <stdint.h>
#include <stdbool.h>

/* CMOS / RTC I/O ports */
#define CMOS_ADDRESS_PORT 0x70
#define CMOS_DATA_PORT    0x71

/* CMOS register indexes */
#define CMOS_REG_SECOND   0x00
#define CMOS_REG_MINUTE   0x02
#define CMOS_REG_HOUR     0x04
#define CMOS_REG_STATUS_A 0x0A
#define CMOS_REG_STATUS_B 0x0B

/**
 * Real time clock time, in binary (BCD already converted).
 */
struct RTCTime {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
} __attribute__((packed));

/**
 * Read the current time from the CMOS real time clock.
 *
 * @param time Output time
 */
void cmos_read_rtc(struct RTCTime *time);

#endif
