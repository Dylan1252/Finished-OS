#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/cpu/idt.h"
#include "header/cpu/interrupt.h"
#include "header/driver/keyboard.h"
#include "header/text/framebuffer.h"
#include "header/kernel-entrypoint.h"
#include "header/driver/disk.h"
#include "header/filesystem/ext2.h"
#include "header/memory/paging.h"
#include "header/process/process.h"
#include "header/process/scheduler.h"
#include "header/stdlib/string.h"

static void framebuffer_put_u32(uint32_t value) {
    char digits[10];
    uint32_t count = 0;
    do {
        digits[count++] = (char) ('0' + value % 10u);
        value /= 10u;
    } while (value != 0 && count < sizeof(digits));
    while (count > 0)
        framebuffer_put(digits[--count], 0x0F);
}

void kernel_setup(void) {
    /* Descriptor tables & interrupt setup */
    load_gdt(&_gdt_gdtr);
    gdt_install_tss();
    set_tss_register();
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();

    framebuffer_clear();
    framebuffer_set_cursor(0, 0);
    framebuffer_puts("KronOS booting...\n", 18, 0xF);

    /* Initialize the EXT2 - IF2130 Edition filesystem */
    initialize_filesystem_ext2();

    /* Create the initial user process (the shell) from the filesystem */
    struct EXT2DriverRequest request = {
        .buf          = (void *) 0,
        .name         = "shell",
        .name_len     = 5,
        .parent_inode = 1,
        .buffer_size  = 0x100000,
        .is_directory = false,
    };

    uint32_t shell_size = 0;
    int8_t shell_lookup_result = get_file_size(request, &shell_size);
    set_tss_kernel_current_stack();
    int32_t shell_process_result = process_create_user_process(request);
    if (shell_process_result != PROCESS_CREATE_SUCCESS) {
        framebuffer_puts("Failed to load shell\nfilesystem lookup: ", 40, 0xF);
        framebuffer_put_u32((uint32_t) (int32_t) shell_lookup_result);
        framebuffer_puts(" size: ", 7, 0xF);
        framebuffer_put_u32(shell_size);
        framebuffer_puts("\nprocess error: ", 16, 0xF);
        framebuffer_put_u32((uint32_t) shell_process_result);
        while (true) __asm__("hlt");
    }

    /* Enable the timer and hand control to the first process */
    scheduler_init();
    scheduler_switch_to_next_process();
}
