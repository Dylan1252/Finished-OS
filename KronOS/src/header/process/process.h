#ifndef _PROCESS_H
#define _PROCESS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "header/cpu/interrupt.h"
#include "header/memory/paging.h"
#include "header/filesystem/ext2.h"

#define PROCESS_NAME_LENGTH_MAX          32
#define PROCESS_PAGE_FRAME_COUNT_MAX     384
#define PROCESS_COUNT_MAX                16

#define KERNEL_VIRTUAL_ADDRESS_BASE      0xC0000000

#define CPU_EFLAGS_BASE_FLAG             0x2
#define CPU_EFLAGS_FLAG_CARRY            0x1
#define CPU_EFLAGS_FLAG_PARITY           0x4
#define CPU_EFLAGS_FLAG_AUX_CARRY        0x10
#define CPU_EFLAGS_FLAG_ZERO             0x40
#define CPU_EFLAGS_FLAG_SIGN             0x80
#define CPU_EFLAGS_FLAG_TRAP             0x100
#define CPU_EFLAGS_FLAG_INTERRUPT_ENABLE 0x200
#define CPU_EFLAGS_FLAG_DIRECTION        0x400
#define CPU_EFLAGS_FLAG_OVERFLOW         0x800
#define CPU_EFLAGS_FLAG_IO_PRIVILEGE     0x3000
#define CPU_EFLAGS_FLAG_NESTED_TASK      0x4000
#define CPU_EFLAGS_FLAG_RESUME           0x10000
#define CPU_EFLAGS_FLAG_VIRTUAL_8086     0x20000
#define CPU_EFLAGS_FLAG_ALIGNMENT_CHECK  0x40000
#define CPU_EFLAGS_FLAG_VIRTUAL_INTERRUPT 0x80000
#define CPU_EFLAGS_FLAG_VIRTUAL_INTERRUPT_PENDING 0x100000
#define CPU_EFLAGS_FLAG_CPUID_AVAILABLE  0x200000

/* -- process_create_user_process() return codes -- */
#define PROCESS_CREATE_SUCCESS                   0
#define PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED 1
#define PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT   2
#define PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY    3
#define PROCESS_CREATE_FAIL_FS_READ_FAILURE      4

/**
 * PROCESS_STATE, the lifecycle state of a process.
 */
typedef enum PROCESS_STATE {
    PROCESS_STATE_UNUSED = 0,
    PROCESS_STATE_READY,
    PROCESS_STATE_RUNNING,
    PROCESS_STATE_BLOCKED,
} PROCESS_STATE;

/**
 * Context, CPU execution state of a process.
 * Saved on every timer interrupt and restored on context switch.
 *
 * @param cpu                       general purpose / index / segment registers
 * @param eip                       instruction pointer
 * @param eflags                    CPU flags
 * @param user_stack_addr           user (ring 3) stack pointer used for iret
 * @param page_directory_virtual_addr page directory of the process address space
 */
struct Context {
    struct CPURegister cpu;
    uint32_t            eip;
    uint32_t            eflags;
    uint32_t            user_stack_addr;
    void               *page_directory_virtual_addr;
} __attribute__((packed));

/**
 * ProcessMetadata, small identifying information about a process.
 */
struct ProcessMetadata {
    PROCESS_STATE state;
    uint32_t      pid;
    char          name[PROCESS_NAME_LENGTH_MAX];
} __attribute__((packed));

/**
 * ProcessControlBlock, everything the kernel knows about one process.
 */
struct ProcessControlBlock {
    struct ProcessMetadata metadata;
    struct Context         context;
    struct {
        void    *virtual_addr_used[PROCESS_PAGE_FRAME_COUNT_MAX];
        uint32_t page_frame_used_count;
    } memory;
} __attribute__((packed));

/**
 * ProcessInfo, process information exported to user space (for `ps`).
 */
struct ProcessInfo {
    PROCESS_STATE state;
    uint32_t      pid;
    char          name[PROCESS_NAME_LENGTH_MAX];
} __attribute__((packed));

/* -- Static process list, one entry per possible process -- */
extern struct ProcessControlBlock _process_list[PROCESS_COUNT_MAX];

/* -- Index into _process_list of the currently running process, PROCESS_COUNT_MAX if none -- */
extern uint32_t _current_running_process;

/**
 * Create a new user process from an executable on the filesystem.
 *
 * @param request Filesystem request pointing at the executable (buf = entrypoint)
 * @return PROCESS_CREATE_* return code
 */
int32_t process_create_user_process(struct EXT2DriverRequest request);

/**
 * Destroy a process by pid, freeing all of its memory.
 *
 * @param pid Process id
 * @return    True if a process was found and destroyed
 */
bool process_destroy(uint32_t pid);

/**
 * Get a pointer to the currently running process's PCB.
 *
 * @return PCB pointer, or NULL if no process is running
 */
struct ProcessControlBlock *process_get_current_running_pcb_pointer(void);

#endif
