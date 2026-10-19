#include "header/driver/cmos.h"
#include "header/cpu/portio.h"

static uint8_t cmos_read(uint8_t reg) {
    out(CMOS_ADDRESS_PORT, reg);
    return in(CMOS_DATA_PORT);
}

static uint8_t bcd_to_binary(uint8_t bcd) {
    return (uint8_t) ((bcd & 0x0F) + ((bcd >> 4) * 10));
}

void cmos_read_rtc(struct RTCTime *time) {
    /* Wait until the RTC is not updating its registers */
    while (cmos_read(CMOS_REG_STATUS_A) & 0x80);

    uint8_t second = cmos_read(CMOS_REG_SECOND);
    uint8_t minute = cmos_read(CMOS_REG_MINUTE);
    uint8_t hour   = cmos_read(CMOS_REG_HOUR);

    uint8_t status_b = cmos_read(CMOS_REG_STATUS_B);

    /* Bit 2 clear => values are BCD encoded */
    if (!(status_b & 0x04)) {
        second = bcd_to_binary(second);
        minute = bcd_to_binary(minute);
        hour   = bcd_to_binary((uint8_t) (hour & 0x7F));
    } else {
        hour &= 0x7F;
    }

    time->second = second;
    time->minute = minute;
    time->hour   = hour;
}
