#include <stdint.h>

/* -- Minimal syscall interface for user programs -- */
static inline void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("int $0x30" : : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx) : "memory");
}

static void tty_puts(const char *s, uint32_t len) { syscall(6, (uint32_t) s, len, 0xF); }

static uint32_t slen(const char *s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

int main(void) {
    const char *msg = "Hello World! Greetings from a KronOS external program.\n";
    tty_puts(msg, slen(msg));
    return 0;
}
