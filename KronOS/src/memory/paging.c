#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/memory/paging.h"
#include "header/stdlib/string.h"

/**
 * _paging_kernel_page_directory and _paging_kernel_page_table is initialized on kernel-entrypoint.s
 */
__attribute__((aligned(0x1000))) struct PageDirectory _paging_kernel_page_directory;

__attribute__((aligned(0x1000))) struct PageTable _paging_kernel_page_table;

/**
 * Global page frame manager state tracking free/busy frames.
 */
struct PageManagerState page_manager_state = {
    .page_frame_map = {
        [0 ... KERNEL_RESERVED_PAGE_FRAME_COUNT - 1] = true,
    },
    .free_page_frame_count = PAGE_FRAME_MAX_COUNT - KERNEL_RESERVED_PAGE_FRAME_COUNT,
};

/* Get the currently loaded page directory (converting physical CR3 to kernel virtual address) */
struct PageDirectory *paging_get_current_page_directory_addr(void) {
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return (struct PageDirectory *) (cr3 + 0xC0000000);
}

struct PageTable *paging_get_page_table(struct PageDirectory *page_dir, uint32_t page_directory_index) {
    /* Recursive mapping only reflects the currently loaded page directory */
    if (page_directory_index == RECURSIVE_PAGE_DIRECTORY_INDEX ||
        page_dir != paging_get_current_page_directory_addr())
        return NULL;

    struct PageDirectoryEntry *dir_entry = &page_dir->table[page_directory_index];

    if (dir_entry->present_bit) {
        return (struct PageTable *) (RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS + page_directory_index * PAGE_FRAME_SIZE);
    }

    /* Allocate a fresh page table for this directory index */
    for (uint32_t page_frame_number = KERNEL_RESERVED_PAGE_FRAME_COUNT;
         page_frame_number < PAGE_FRAME_MAX_COUNT;
         page_frame_number++) {
        if (!page_manager_state.page_frame_map[page_frame_number]) {
            page_manager_state.page_frame_map[page_frame_number] = true;
            page_manager_state.free_page_frame_count--;

            dir_entry->present_bit = true;
            dir_entry->write_bit   = true;
            dir_entry->us_bit      = true;
            dir_entry->lower_address = page_frame_number;

            struct PageTable *page_table = (struct PageTable *)
                (RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS + page_directory_index * PAGE_FRAME_SIZE);
            flush_single_tlb(page_table);
            memset(page_table, 0, PAGE_FRAME_SIZE);
            return page_table;
        }
    }
    return NULL;
}

void update_page_directory_entry(
    struct PageDirectory *page_dir,
    void *physical_addr,
    void *virtual_addr,
    struct PageDirectoryEntryFlag flag
) {
    uint32_t page_directory_index = ((uint32_t) virtual_addr >> 22) & 0x3FF;
    uint32_t page_table_index     = ((uint32_t) virtual_addr >> 12) & 0x3FF;

    struct PageTable *page_table = paging_get_page_table(page_dir, page_directory_index);
    if (page_table == NULL) return;

    page_table->table[page_table_index].present_bit     = flag.present_bit;
    page_table->table[page_table_index].write_bit       = flag.write_bit;
    page_table->table[page_table_index].us_bit          = flag.us_bit;
    page_table->table[page_table_index].pwt_bit         = flag.pwt_bit;
    page_table->table[page_table_index].pcd_bit         = flag.pcd_bit;
    page_table->table[page_table_index].accessed_bit    = flag.accessed_bit;
    page_table->table[page_table_index].dirty_bit       = flag.dirty_bit;
    page_table->table[page_table_index].page_attr_table = flag.use_pagesize_4_mb;
    page_table->table[page_table_index].lower_address   = ((uint32_t) physical_addr >> 12);

    flush_single_tlb(virtual_addr);
}

void flush_single_tlb(void *virtual_addr) {
    __asm__ volatile("invlpg (%0)" : /* <Empty> */ : "b"(virtual_addr) : "memory");
}

/* --- Memory Management --- */

bool paging_allocate_check(uint32_t amount) {
    uint32_t page_frame_needed = (amount + PAGE_FRAME_SIZE - 1) / PAGE_FRAME_SIZE;
    return page_manager_state.free_page_frame_count >= page_frame_needed;
}

bool paging_allocate_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr) {
    uint32_t page_directory_index = ((uint32_t) virtual_addr >> 22) & 0x3FF;
    uint32_t page_table_index     = ((uint32_t) virtual_addr >> 12) & 0x3FF;
    struct PageTable *page_table = paging_get_page_table(page_dir, page_directory_index);
    if (page_table == NULL) return false;

    for (uint32_t page_frame_number = KERNEL_RESERVED_PAGE_FRAME_COUNT;
         page_frame_number < PAGE_FRAME_MAX_COUNT;
         page_frame_number++) {
        if (!page_manager_state.page_frame_map[page_frame_number]) {
            page_manager_state.page_frame_map[page_frame_number] = true;
            page_manager_state.free_page_frame_count--;

            struct PageTableEntry *entry = &page_table->table[page_table_index];
            entry->present_bit   = true;
            entry->write_bit     = true;
            entry->us_bit        = true;
            entry->lower_address = page_frame_number;
            flush_single_tlb(virtual_addr);
            return true;
        }
    }
    return false;
}

bool paging_free_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr) {
    uint32_t page_directory_index = ((uint32_t) virtual_addr >> 22) & 0x3FF;
    uint32_t page_table_index     = ((uint32_t) virtual_addr >> 12) & 0x3FF;

    struct PageTable *page_table = paging_get_page_table(page_dir, page_directory_index);
    if (page_table == NULL) return false;
    if (!page_table->table[page_table_index].present_bit) return false;

    uint32_t page_frame_number = page_table->table[page_table_index].lower_address;
    page_manager_state.page_frame_map[page_frame_number] = false;
    page_manager_state.free_page_frame_count++;

    page_table->table[page_table_index].present_bit   = false;
    page_table->table[page_table_index].lower_address = 0;

    flush_single_tlb(virtual_addr);
    return true;
}

/* --- Multiple Virtual Address Space (process page directories) --- */

__attribute__((aligned(0x1000))) static struct PageDirectory page_directory_list[PAGING_DIRECTORY_TABLE_MAX_COUNT];
static bool page_directory_used[PAGING_DIRECTORY_TABLE_MAX_COUNT];

struct PageDirectory *paging_create_new_page_directory(void) {
    for (uint32_t i = 0; i < PAGING_DIRECTORY_TABLE_MAX_COUNT; i++) {
        if (!page_directory_used[i]) {
            page_directory_used[i] = true;
            struct PageDirectory *page_dir = &page_directory_list[i];
            memset(page_dir, 0, PAGE_FRAME_SIZE);

            /* Share the kernel higher-half mapping so kernel memory stays reachable */
            for (uint32_t j = 768; j < PAGE_ENTRY_COUNT - 1; j++)
                page_dir->table[j] = _paging_kernel_page_directory.table[j];

            /* Fractal/recursive mapping: last PDE points back to this directory */
            uint32_t phys_addr = (uint32_t) page_dir - 0xC0000000;
            page_dir->table[RECURSIVE_PAGE_DIRECTORY_INDEX].present_bit   = true;
            page_dir->table[RECURSIVE_PAGE_DIRECTORY_INDEX].write_bit     = true;
            page_dir->table[RECURSIVE_PAGE_DIRECTORY_INDEX].lower_address = phys_addr >> 12;
            return page_dir;
        }
    }
    return NULL;
}

bool paging_free_page_directory(struct PageDirectory *page_dir) {
    for (uint32_t i = 0; i < PAGING_DIRECTORY_TABLE_MAX_COUNT; i++) {
        if (&page_directory_list[i] == page_dir) {
            /* User page tables are private; higher-half kernel tables are shared. */
            for (uint32_t pde = 0; pde < 768; pde++) {
                struct PageDirectoryEntry *entry = &page_dir->table[pde];
                if (entry->present_bit) {
                    uint32_t frame = entry->lower_address;
                    if (frame >= KERNEL_RESERVED_PAGE_FRAME_COUNT && frame < PAGE_FRAME_MAX_COUNT &&
                        page_manager_state.page_frame_map[frame]) {
                        page_manager_state.page_frame_map[frame] = false;
                        page_manager_state.free_page_frame_count++;
                    }
                }
            }
            page_directory_used[i] = false;
            memset(page_dir, 0, PAGE_FRAME_SIZE);
            return true;
        }
    }
    return false;
}

void paging_use_page_directory(struct PageDirectory *page_dir_virtual_addr) {
    uint32_t physical_addr = (uint32_t) page_dir_virtual_addr;
    if (physical_addr >= 0xC0000000)
        physical_addr -= 0xC0000000;
    __asm__ volatile("mov %0, %%cr3" : /* <Empty> */ : "r"(physical_addr) : "memory");
}
