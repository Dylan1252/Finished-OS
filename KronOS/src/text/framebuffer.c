#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/text/framebuffer.h"
#include "header/stdlib/string.h"
#include "header/cpu/portio.h"

static uint8_t framebuffer_row = 0;
static uint8_t framebuffer_col = 0;

void framebuffer_set_cursor(uint8_t r, uint8_t c) {
    framebuffer_row = r;
    framebuffer_col = c;

    uint16_t pos = r * 80 + c;

    uint8_t low_byte = pos & 0xFF;
    uint8_t high_byte = (pos >> 8) & 0xFF;

    out(CURSOR_PORT_CMD, 0x0F);
    out(CURSOR_PORT_DATA, low_byte);

    out(CURSOR_PORT_CMD, 0x0E);
    out(CURSOR_PORT_DATA, high_byte);
}

void framebuffer_write(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg) {
    uint16_t offset = (row * 80 + col) * 2;
    FRAMEBUFFER_MEMORY_OFFSET[offset]     = (uint8_t) c;
    FRAMEBUFFER_MEMORY_OFFSET[offset + 1] = (uint8_t)((bg & 0x0F) << 4) | (fg & 0x0F);
}

void framebuffer_clear(void) {
    for (uint8_t row = 0; row < 25; row++) {
        for (uint8_t col = 0; col < 80; col++) {
            framebuffer_write(row, col, 0x00, 0x07, 0x00);
        }
    }
    framebuffer_set_cursor(0, 0);
}

/* Scroll the framebuffer up by one row when the cursor leaves the bottom */
static void framebuffer_scroll(void) {
    if (framebuffer_row < 25) return;
    for (int i = 0; i < 80 * 24 * 2; i++)
        FRAMEBUFFER_MEMORY_OFFSET[i] = FRAMEBUFFER_MEMORY_OFFSET[i + 80 * 2];
    for (int c = 0; c < 80; c++)
        framebuffer_write(24, c, 0x00, 0x07, 0x00);
    framebuffer_row = 24;
}

void framebuffer_put(char c, uint8_t color) {
    if (c == '\n') {
        framebuffer_col = 0;
        framebuffer_row++;
    } else if (c == '\b') {
        if (framebuffer_col > 0) {
            framebuffer_col--;
        } else if (framebuffer_row > 0) {
            framebuffer_row--;
            framebuffer_col = 79;
        }
        framebuffer_write(framebuffer_row, framebuffer_col, 0x00, color & 0xF, color >> 4);
    } else if (c == '\r') {
        framebuffer_col = 0;
    } else {
        framebuffer_write(framebuffer_row, framebuffer_col, c, color & 0xF, color >> 4);
        framebuffer_col++;
        if (framebuffer_col >= 80) {
            framebuffer_col = 0;
            framebuffer_row++;
        }
    }
    framebuffer_scroll();
    framebuffer_set_cursor(framebuffer_row, framebuffer_col);
}

void framebuffer_puts(const char *s, uint32_t len, uint8_t color) {
    for (uint32_t i = 0; i < len; i++)
        framebuffer_put(s[i], color);
}
