#include "header/process/scheduler.h"
#include "header/process/process.h"
#include "header/cpu/interrupt.h"
#include "header/cpu/portio.h"
#include "header/memory/paging.h"

/* -- PIT (Programmable Interval Timer) constants -- */
#define PIT_MAX_FREQUENCY           1193182
#define PIT_TIMER_FREQUENCY         1000
#define PIT_TIMER_COUNTER           (PIT_MAX_FREQUENCY / PIT_TIMER_FREQUENCY)

#define PIT_COMMAND_REGISTER_PIO    0x43
#define PIT_CHANNEL_0_DATA_PIO      0x40
#define PIT_COMMAND_VALUE           0x36  /* Channel 0, lobyte/hibyte, square wave */

void activate_timer_interrupt(void) {
    __asm__ volatile("cli");

    /* Program the PIT to fire at PIT_TIMER_FREQUENCY */
    out(PIT_COMMAND_REGISTER_PIO, PIT_COMMAND_VALUE);
    out(PIT_CHANNEL_0_DATA_PIO, (uint8_t) (PIT_TIMER_COUNTER & 0xFF));
    out(PIT_CHANNEL_0_DATA_PIO, (uint8_t) ((PIT_TIMER_COUNTER >> 8) & 0xFF));

    /* Unmask IRQ0 (timer) on the master PIC */
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_TIMER));

    __asm__ volatile("sti");
}

void scheduler_init(void) {
    activate_timer_interrupt();
}

void scheduler_save_context_to_current_running_pcb(struct Context ctx) {
    struct ProcessControlBlock *pcb = process_get_current_running_pcb_pointer();
    if (pcb != NULL)
        pcb->context = ctx;
}

__attribute__((noreturn)) void scheduler_switch_to_next_process(void) {
    /* Round-robin: pick the next READY/RUNNING process after the current one */
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        uint32_t idx = (_current_running_process >= PROCESS_COUNT_MAX)
                           ? i
                           : (_current_running_process + 1 + i) % PROCESS_COUNT_MAX;

        struct ProcessControlBlock *pcb = &_process_list[idx];
        if (pcb->metadata.state == PROCESS_STATE_READY ||
            pcb->metadata.state == PROCESS_STATE_RUNNING) {

            if (_current_running_process < PROCESS_COUNT_MAX &&
                _process_list[_current_running_process].metadata.state == PROCESS_STATE_RUNNING)
                _process_list[_current_running_process].metadata.state = PROCESS_STATE_READY;

            _current_running_process = idx;
            pcb->metadata.state = PROCESS_STATE_RUNNING;

            paging_use_page_directory(pcb->context.page_directory_virtual_addr);
            process_context_switch(pcb->context);
        }
    }

    /* No runnable process: idle forever */
    for (;;) __asm__ volatile("hlt");
}
