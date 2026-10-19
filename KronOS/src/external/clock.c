#include <stdint.h>

/* -- Minimal syscall interface for user programs -- */
static inline void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("int $0x30" : : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx) : "memory");
}

struct RTCTime {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
} __attribute__((packed));

/* syscall 15: write a character directly at (row, col) without moving the cursor */
static void put_at(uint8_t row, uint8_t col, char c) {
    syscall(15, ((uint32_t) row << 8) | (uint32_t) col, (uint32_t) c, 0x0F);
}

static void put_digit2(uint8_t row, uint8_t col, uint8_t value) {
    put_at(row, col,     (char) ('0' + (value / 10)));
    put_at(row, (uint8_t) (col + 1), (char) ('0' + (value % 10)));
}

int main(void) {
    struct RTCTime time;
    uint8_t last_second = 255;

    const char *label = "RTC ";
    for (uint32_t i = 0; i < 4; i++)
        put_at(0, (uint8_t) (64 + i), label[i]);

    for (;;) {
        syscall(14, (uint32_t) &time, 0, 0);

        if (time.second != last_second) {
            last_second = time.second;
            put_digit2(0, 68, time.hour);
            put_at(0, 70, ':');
            put_digit2(0, 71, time.minute);
            put_at(0, 73, ':');
            put_digit2(0, 74, time.second);
        }
    }
}
