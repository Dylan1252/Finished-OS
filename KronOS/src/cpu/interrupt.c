#include "header/cpu/interrupt.h"
#include "header/cpu/gdt.h"
#include "header/cpu/portio.h"
#include "header/driver/keyboard.h"
#include "header/text/framebuffer.h"
#include "header/filesystem/ext2.h"
#include "header/process/process.h"
#include "header/process/scheduler.h"
#include "header/driver/cmos.h"

struct TSSEntry _interrupt_tss_entry = {
    .ss0 = GDT_KERNEL_DATA_SEGMENT_SELECTOR,
};

void set_tss_kernel_current_stack(void) {
    uint32_t stack_ptr;
    // Reading base stack frame instead esp
    __asm__ volatile ("mov %%ebp, %0": "=r"(stack_ptr) : /* <Empty> */);
    // Add 8 because 4 for ret address and other 4 is for stack_ptr variable
    _interrupt_tss_entry.esp0 = stack_ptr + 8;
}

void io_wait(void) {
    out(0x80, 0);
}

static void syscall(struct InterruptFrame frame) {
    switch (frame.cpu.general.eax) {
        case 0: // FS read()
            *((int8_t *) (uint32_t) frame.cpu.general.ecx) =
                read(*(struct EXT2DriverRequest *) (uint32_t) frame.cpu.general.ebx);
            break;
        case 1: // FS read_directory()
            *((int8_t *) (uint32_t) frame.cpu.general.ecx) =
                read_directory((struct EXT2DriverRequest *) (uint32_t) frame.cpu.general.ebx);
            break;
        case 2: // FS write()
            *((int8_t *) (uint32_t) frame.cpu.general.ecx) =
                write((struct EXT2DriverRequest *) (uint32_t) frame.cpu.general.ebx);
            break;
        case 3: // FS delete()
            *((int8_t *) (uint32_t) frame.cpu.general.ecx) =
                delete(*(struct EXT2DriverRequest *) (uint32_t) frame.cpu.general.ebx);
            break;
        case 4: // Keyboard getchar()
            get_keyboard_buffer((char *) (uint32_t) frame.cpu.general.ebx);
            break;
        case 5: // Text putchar()
            framebuffer_put((char) frame.cpu.general.ebx, (uint8_t) frame.cpu.general.ecx);
            break;
        case 6: // Text puts()
            framebuffer_puts((const char *) (uint32_t) frame.cpu.general.ebx,
                             frame.cpu.general.ecx, (uint8_t) frame.cpu.general.edx);
            break;
        case 7: // Activate keyboard input
            keyboard_state_activate();
            break;
        case 8: // Text clear screen
            framebuffer_clear();
            break;
        case 9: // FS get_file_size()
            *((int8_t *) (uint32_t) frame.cpu.general.ecx) =
                get_file_size(*(struct EXT2DriverRequest *) (uint32_t) frame.cpu.general.ebx,
                              (uint32_t *) (uint32_t) frame.cpu.general.edx);
            break;
        case 10: // Process exit
        {
            struct ProcessControlBlock *pcb = process_get_current_running_pcb_pointer();
            if (pcb != NULL) process_destroy(pcb->metadata.pid);
            scheduler_switch_to_next_process();
        }
            break;
        case 11: // Process create (exec)
            *((int8_t *) (uint32_t) frame.cpu.general.ecx) =
                (int8_t) process_create_user_process(
                    *(struct EXT2DriverRequest *) (uint32_t) frame.cpu.general.ebx);
            break;
        case 12: // Process destroy (kill)
        {
            uint32_t target_pid = (uint32_t) frame.cpu.general.ebx;
            struct ProcessControlBlock *current = process_get_current_running_pcb_pointer();
            bool killed = process_destroy(target_pid);
            if (killed && current != NULL && current->metadata.pid == target_pid)
                scheduler_switch_to_next_process();
            *((int8_t *) (uint32_t) frame.cpu.general.ecx) = killed ? 0 : 1;
            break;
        }
        case 13: // Process info (ps)
        {
            uint32_t index = (uint32_t) frame.cpu.general.ebx;
            struct ProcessInfo *info = (struct ProcessInfo *) (uint32_t) frame.cpu.general.ecx;
            if (index < PROCESS_COUNT_MAX) {
                info->pid   = _process_list[index].metadata.pid;
                info->state = _process_list[index].metadata.state;
                for (uint32_t i = 0; i < PROCESS_NAME_LENGTH_MAX; i++)
                    info->name[i] = _process_list[index].metadata.name[i];
            }
            break;
        }
        case 14: // Clock / RTC read
            cmos_read_rtc((struct RTCTime *) (uint32_t) frame.cpu.general.ebx);
            break;
        case 15: // Text framebuffer_write(row, col, c, attr)
        {
            uint32_t packed = (uint32_t) frame.cpu.general.ebx;
            uint8_t row  = (uint8_t) ((packed >> 8) & 0xFF);
            uint8_t col  = (uint8_t) (packed & 0xFF);
            uint8_t attr = (uint8_t) frame.cpu.general.edx;
            framebuffer_write(row, col, (char) frame.cpu.general.ecx,
                              attr & 0x0F, (attr >> 4) & 0x0F);
            break;
        }
        case 16: // Filesystem directory size
            *((int8_t *) (uint32_t) frame.cpu.general.ecx) =
                get_directory_size((uint32_t) frame.cpu.general.ebx,
                                   (uint32_t *) (uint32_t) frame.cpu.general.edx);
            break;
        case 17: // Filesystem move / rename
            *((int8_t *) (uint32_t) frame.cpu.general.ecx) =
                move_entry((const struct EXT2MoveRequest *) (uint32_t) frame.cpu.general.ebx);
            break;
    }
}

void main_interrupt_handler(struct InterruptFrame frame) {
    if (frame.int_number == 0x30) {
        syscall(frame);
        return;
    }

    switch (frame.int_number) {
        case PIC1_OFFSET + IRQ_KEYBOARD:
            keyboard_isr();
            break;
        case PIC1_OFFSET + IRQ_TIMER:
            pic_ack(IRQ_TIMER);
            /* Only preempt processes that were interrupted in user mode */
            if (process_get_current_running_pcb_pointer() != NULL &&
                (frame.int_stack.cs & 0x3) == 0x3) {
                struct Context ctx;
                ctx.cpu    = frame.cpu;
                ctx.eip    = frame.int_stack.eip;
                ctx.eflags = frame.int_stack.eflags;
                ctx.user_stack_addr = ((uint32_t *) &frame.int_stack)[4];
                ctx.page_directory_virtual_addr = paging_get_current_page_directory_addr();

                scheduler_save_context_to_current_running_pcb(ctx);
                scheduler_switch_to_next_process();
            }
            break;
    }
}

void activate_keyboard_interrupt(void) {
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_KEYBOARD));
}

void pic_ack(uint8_t irq) {
    if (irq >= 8) out(PIC2_COMMAND, PIC_ACK);
    out(PIC1_COMMAND, PIC_ACK);
}

void pic_remap(void) {
    // Starts the initialization sequence in cascade mode
    out(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    out(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    out(PIC1_DATA, PIC1_OFFSET); // ICW2: Master PIC vector offset
    io_wait();
    out(PIC2_DATA, PIC2_OFFSET); // ICW2: Slave PIC vector offset
    io_wait();
    out(PIC1_DATA, 0b0100); // ICW3: tell Master PIC, slave PIC at IRQ2 (0000 0100)
    io_wait();
    out(PIC2_DATA, 0b0010); // ICW3: tell Slave PIC its cascade identity (0000 0010)
    io_wait();

    out(PIC1_DATA, ICW4_8086);
    io_wait();
    out(PIC2_DATA, ICW4_8086);
    io_wait();

    // Disable all interrupts
    out(PIC1_DATA, PIC_DISABLE_ALL_MASK);
    out(PIC2_DATA, PIC_DISABLE_ALL_MASK);
}
