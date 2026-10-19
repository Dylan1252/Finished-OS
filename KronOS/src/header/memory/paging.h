#ifndef _PAGING_H
#define _PAGING_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/** 
 * Note: MB often referring to MiB in context of memory management
 */
#define SYSTEM_MEMORY_MB     128

// Number of entries in a page directory / page table
#define PAGE_ENTRY_COUNT     1024

// Page Frame (PF) Size: 4 KiB
#define PAGE_FRAME_SIZE      0x1000u

// Kernel occupies the first 4 MiB of physical memory (higher half mapping)
#define KERNEL_RESERVED_PAGE_FRAME_COUNT (0x400000u / PAGE_FRAME_SIZE)
#define RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS 0xFFC00000u
#define RECURSIVE_PAGE_DIRECTORY_INDEX 1023u

// Maximum number of separate page directories (one per process)
#define PAGING_DIRECTORY_TABLE_MAX_COUNT 16u

// Maximum usable page frame. Default count: 128 MiB / 4 KiB = 32768 frames
#define PAGE_FRAME_MAX_COUNT ((SYSTEM_MEMORY_MB << 20) / PAGE_FRAME_SIZE)

// kernel's page directory, initialized on kernel-entrypoint.s
extern struct PageDirectory _paging_kernel_page_directory;
extern struct PageTable _paging_kernel_page_table;

/**
 * Page Directory Entry Flag, only first 8 bit
 * References: Intel Manual 3a - Ch 4 Paging - Figure 4-4 PDE: 4KB page
 */
struct PageDirectoryEntryFlag {
    uint8_t present_bit         : 1;
    uint8_t write_bit           : 1;
    uint8_t us_bit              : 1;
    uint8_t pwt_bit             : 1;
    uint8_t pcd_bit             : 1;
    uint8_t accessed_bit        : 1;
    uint8_t dirty_bit           : 1;
    uint8_t use_pagesize_4_mb   : 1;
} __attribute__((packed));

/**
 * Page Table Entry Flag, only first 8 bit
 */
struct PageTableEntryFlag {
    uint8_t present_bit         : 1;
    uint8_t write_bit           : 1;
    uint8_t us_bit              : 1;
    uint8_t pwt_bit             : 1;
    uint8_t pcd_bit             : 1;
    uint8_t accessed_bit        : 1;
    uint8_t dirty_bit           : 1;
    uint8_t page_attr_table     : 1;
} __attribute__((packed));

/**
 * Page Directory Entry, for page size 4 KB. Can also be used as Page Table Entry.
 * Reference: Intel Manual 3a - Figure 4-4 PDE: 4KB page
 */
struct PageDirectoryEntry {
    uint32_t present_bit       : 1;
    uint32_t write_bit         : 1;
    uint32_t us_bit            : 1;
    uint32_t pwt_bit           : 1;
    uint32_t pcd_bit           : 1;
    uint32_t accessed_bit      : 1;
    uint32_t dirty_bit         : 1;
    uint32_t use_pagesize_4_mb : 1;
    uint32_t global_page       : 1;
    uint32_t ignored           : 3;
    uint32_t lower_address     : 20; // Bits 31:12 of the address
} __attribute__((packed));

/**
 * Page Table Entry, for page size 4 KB.
 * Reference: Intel Manual 3a - Figure 4-5 PTE: 4KB page
 */
struct PageTableEntry {
    uint32_t present_bit       : 1;
    uint32_t write_bit         : 1;
    uint32_t us_bit            : 1;
    uint32_t pwt_bit           : 1;
    uint32_t pcd_bit           : 1;
    uint32_t accessed_bit      : 1;
    uint32_t dirty_bit         : 1;
    uint32_t page_attr_table   : 1;
    uint32_t global_page       : 1;
    uint32_t ignored           : 3;
    uint32_t lower_address     : 20; // Bits 31:12 of the address
} __attribute__((packed));

/**
 * Page Directory, contain array of PageDirectoryEntry.
 * Must be 4 KB aligned (@warning unaligned definition will cause triple fault).
 */
struct PageDirectory {
    struct PageDirectoryEntry table[PAGE_ENTRY_COUNT];
} __attribute__((packed));

/**
 * Page Table, contain array of PageTableEntry
 */
struct PageTable {
    struct PageTableEntry table[PAGE_ENTRY_COUNT];
} __attribute__((packed));

/**
 * Containing page manager states.
 *
 * @param page_frame_map     Keeping track empty space. True when the page frame is currently used
 * @param free_page_frame_count Number of unused page frames
 */
struct PageManagerState {
    bool     page_frame_map[PAGE_FRAME_MAX_COUNT];
    uint32_t free_page_frame_count;
} __attribute__((packed));

/**
 * Edit page directory with respective parameter
 *
 * @param page_dir      Page directory to update
 * @param physical_addr Physical address to map
 * @param virtual_addr  Virtual address to map
 * @param flag          Page entry flags
 */
void update_page_directory_entry(
    struct PageDirectory *page_dir,
    void *physical_addr,
    void *virtual_addr,
    struct PageDirectoryEntryFlag flag
);

/**
 * Get the requested page table from respective parameter.
 * Uses fractal (recursive) memory mapping, so only valid for the currently loaded page directory.
 *
 * @param page_dir              Page directory to read
 * @param page_directory_index  Page table's position/index on page directory
 */
struct PageTable *paging_get_page_table(struct PageDirectory *page_dir, uint32_t page_directory_index);

/**
 * Invalidate page that contain virtual address in parameter
 *
 * @param virtual_addr Virtual address to flush
 */
void flush_single_tlb(void *virtual_addr);

/* --- Memory Management --- */
/**
 * Check whether a certain amount of physical memory is available
 *
 * @param amount Requested amount of physical memory in bytes
 * @return       Return true when there's enough free memory available
 */
bool paging_allocate_check(uint32_t amount);

/**
 * Allocate single user page frame in page directory
 *
 * @param page_dir     Page directory to update
 * @param virtual_addr Virtual address to be allocated
 * @return             Will return true if success, false otherwise
 */
bool paging_allocate_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr);

/**
 * Deallocate single user page frame in page directory
 *
 * @param page_dir      Page directory to update
 * @param virtual_addr  Virtual address to be allocated
 * @return              Will return true if success, false otherwise
 */
bool paging_free_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr);

/**
 * Create a new, empty page directory (for a new process), copying the kernel
 * higher-half mapping so kernel memory stays accessible after switching.
 *
 * @return Page directory virtual address, or NULL if none is available
 */
struct PageDirectory *paging_create_new_page_directory(void);

/**
 * Free a page directory previously created by paging_create_new_page_directory()
 *
 * @param page_dir Page directory virtual address
 * @return         True on success
 */
bool paging_free_page_directory(struct PageDirectory *page_dir);

/**
 * Get currently active page directory virtual address from the CR3 register
 *
 * @return Page directory virtual address currently active
 */
struct PageDirectory *paging_get_current_page_directory_addr(void);

/**
 * Change active page directory (loads CR3, flushing non-global TLB entries)
 *
 * @param page_dir_virtual_addr Page directory virtual address to switch into
 */
void paging_use_page_directory(struct PageDirectory *page_dir_virtual_addr);

#endif
