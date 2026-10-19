#include "header/process/process.h"
#include "header/memory/paging.h"
#include "header/stdlib/string.h"
#include "header/cpu/gdt.h"

/* -- Process table -- */
struct ProcessControlBlock _process_list[PROCESS_COUNT_MAX];

/* -- Currently running process, PROCESS_COUNT_MAX means "none" -- */
uint32_t _current_running_process = PROCESS_COUNT_MAX;

/* -- Monotonic pid generator -- */
static uint32_t _next_pid = 1;

/* Number of extra page frames reserved for the user stack */
#define USER_STACK_FRAME_COUNT 16

static uint32_t process_ceil_div(uint32_t a, uint32_t b) {
    return (a + b - 1) / b;
}

static uint32_t process_active_count(void) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++)
        if (_process_list[i].metadata.state != PROCESS_STATE_UNUSED) count++;
    return count;
}

static int32_t process_list_get_inactive_index(void) {
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++)
        if (_process_list[i].metadata.state == PROCESS_STATE_UNUSED) return (int32_t) i;
    return -1;
}

static uint32_t process_generate_new_pid(void) {
    return _next_pid++;
}

struct ProcessControlBlock *process_get_current_running_pcb_pointer(void) {
    if (_current_running_process < PROCESS_COUNT_MAX)
        return &_process_list[_current_running_process];
    return NULL;
}

int32_t process_create_user_process(struct EXT2DriverRequest request) {
    int32_t retcode = PROCESS_CREATE_SUCCESS;

    struct PageDirectory *current_page_directory = paging_get_current_page_directory_addr();
    struct PageDirectory *new_page_directory = NULL;
    int32_t p_index = -1;
    uint32_t page_frame_count_needed = 0;
    uint32_t allocated_frames = 0;

    /* The request (and its name pointer) lives in the caller's address space.
       Copy the name into kernel memory so it stays valid after we switch CR3. */
    char kernel_name[256];
    uint32_t copy_len = request.name_len;
    if (copy_len > 255) copy_len = 255;
    for (uint32_t i = 0; i < copy_len; i++) kernel_name[i] = request.name[i];
    kernel_name[copy_len] = '\0';
    request.name = kernel_name;

    /* Do not allow overflowing the process table */
    if (process_active_count() >= PROCESS_COUNT_MAX) {
        retcode = PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED;
        goto exit_cleanup;
    }

    /* Ensure entrypoint is not located in the kernel's higher half */
    if ((uint32_t) request.buf >= KERNEL_VIRTUAL_ADDRESS_BASE) {
        retcode = PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT;
        goto exit_cleanup;
    }

    /* Determine the real executable size, then round up to whole page frames
       and add extra frames for the user stack. */
    uint32_t file_size = 0;
    if (get_file_size(request, &file_size) != 0) {
        retcode = PROCESS_CREATE_FAIL_FS_READ_FAILURE;
        goto exit_cleanup;
    }
    request.buffer_size = file_size;

    page_frame_count_needed = process_ceil_div(file_size + PAGE_FRAME_SIZE, PAGE_FRAME_SIZE)
                              + USER_STACK_FRAME_COUNT;

    if (!paging_allocate_check(page_frame_count_needed * PAGE_FRAME_SIZE) ||
        page_frame_count_needed > PROCESS_PAGE_FRAME_COUNT_MAX) {
        retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
        goto exit_cleanup;
    }

    p_index = process_list_get_inactive_index();
    if (p_index < 0) {
        retcode = PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED;
        goto exit_cleanup;
    }

    new_page_directory = paging_create_new_page_directory();
    if (new_page_directory == NULL) {
        retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
        goto exit_cleanup;
    }

    /* Work inside the new address space so frames map into it */
    paging_use_page_directory(new_page_directory);

    for (uint32_t vaddr = 0; vaddr < page_frame_count_needed * PAGE_FRAME_SIZE; vaddr += PAGE_FRAME_SIZE) {
        if (!paging_allocate_user_page_frame(new_page_directory, (void *) vaddr)) {
            retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
            goto exit_cleanup;
        }
        _process_list[p_index].memory.virtual_addr_used[allocated_frames++] = (void *) vaddr;
    }
    _process_list[p_index].memory.page_frame_used_count = allocated_frames;

    /* Zero the user address space (fresh frames may contain garbage) */
    memset((void *) 0, 0, page_frame_count_needed * PAGE_FRAME_SIZE);

    /* Load the executable into virtual address 0 */
    if (read_process_image_at_zero(request) != 0) {
        retcode = PROCESS_CREATE_FAIL_FS_READ_FAILURE;
        goto exit_cleanup;
    }

    /* Fill in the process control block */
    struct ProcessControlBlock *pcb = &_process_list[p_index];
    pcb->metadata.state = PROCESS_STATE_READY;
    pcb->metadata.pid   = process_generate_new_pid();

    uint32_t name_len = request.name_len;
    if (name_len > PROCESS_NAME_LENGTH_MAX - 1) name_len = PROCESS_NAME_LENGTH_MAX - 1;
    for (uint32_t i = 0; i < name_len; i++) pcb->metadata.name[i] = request.name[i];
    pcb->metadata.name[name_len] = '\0';

    memset(&pcb->context, 0, sizeof(struct Context));
    pcb->context.cpu.segment.ds = GDT_USER_DATA_SEGMENT_SELECTOR | 0x3;
    pcb->context.cpu.segment.es = GDT_USER_DATA_SEGMENT_SELECTOR | 0x3;
    pcb->context.cpu.segment.fs = GDT_USER_DATA_SEGMENT_SELECTOR | 0x3;
    pcb->context.cpu.segment.gs = GDT_USER_DATA_SEGMENT_SELECTOR | 0x3;
    pcb->context.eip   = 0;
    pcb->context.eflags = CPU_EFLAGS_BASE_FLAG | CPU_EFLAGS_FLAG_INTERRUPT_ENABLE;
    pcb->context.user_stack_addr = page_frame_count_needed * PAGE_FRAME_SIZE - 4;
    pcb->context.page_directory_virtual_addr = new_page_directory;

exit_cleanup:
    if (retcode != PROCESS_CREATE_SUCCESS && new_page_directory != NULL) {
        /* Roll back: release user frames and the page directory */
        paging_use_page_directory(new_page_directory);
        for (uint32_t i = 0; i < allocated_frames; i++)
            paging_free_user_page_frame(new_page_directory,
                                        _process_list[p_index].memory.virtual_addr_used[i]);
        paging_use_page_directory(current_page_directory);
        paging_free_page_directory(new_page_directory);
        _process_list[p_index].memory.page_frame_used_count = 0;
    }

    /* Always restore the caller's address space */
    paging_use_page_directory(current_page_directory);
    return retcode;
}

bool process_destroy(uint32_t pid) {
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        if (_process_list[i].metadata.state == PROCESS_STATE_UNUSED) continue;
        if (_process_list[i].metadata.pid != pid) continue;

        struct PageDirectory *current = paging_get_current_page_directory_addr();
        struct PageDirectory *victim  = _process_list[i].context.page_directory_virtual_addr;

        paging_use_page_directory(victim);
        for (uint32_t j = 0; j < _process_list[i].memory.page_frame_used_count; j++)
            paging_free_user_page_frame(victim, _process_list[i].memory.virtual_addr_used[j]);

        /* If this is the current process, switch to the kernel directory before
           clearing the victim directory, which also contains the active mapping. */
        if (current == victim)
            paging_use_page_directory(&_paging_kernel_page_directory);
        else
            paging_use_page_directory(current);

        paging_free_page_directory(victim);

        _process_list[i].metadata.state             = PROCESS_STATE_UNUSED;
        _process_list[i].memory.page_frame_used_count = 0;
        return true;
    }
    return false;
}
