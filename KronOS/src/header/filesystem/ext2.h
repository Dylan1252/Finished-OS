#ifndef _EXT2_H
#define _EXT2_H

#include "header/driver/disk.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* -- IF2130 File System constants -- */
#define BOOT_SECTOR 0 // legacy from FAT32 filesystem IF2130 OS
#define DISK_SPACE 4194304u // 4MB disk space (because our disk or storage.bin is 4MB)
#define EXT2_SUPER_MAGIC 0xEF53 // this indicating that the filesystem used by OS is ext2
#define INODE_SIZE sizeof(struct EXT2Inode) // size of inode
#define INODES_PER_TABLE (BLOCK_SIZE / INODE_SIZE) // number of inode per block
#define GROUPS_COUNT ((BLOCK_SIZE / sizeof(struct EXT2BlockGroupDescriptor)) / 2u) // number of groups in the filesystem
#define BLOCKS_PER_GROUP (DISK_SPACE / BLOCK_SIZE / GROUPS_COUNT) // number of blocks per group
#define INODES_TABLE_BLOCK_COUNT 16u
#define INODES_PER_GROUP (INODES_PER_TABLE * INODES_TABLE_BLOCK_COUNT) // number of inodes per group

/* -- EXT2 - IF2130 Edition layout macros -- */
// Block 0 : boot sector (fs_signature)
// Block 1 : superblock
// Block 2 : block group descriptor table
// Group g metadata : block bitmap, inode bitmap, inode table (INODES_TABLE_BLOCK_COUNT blocks)
#define BLOCK_GROUP_BLOCK_BITMAP(g) ((g) == 0 ? 3u : (uint32_t)(g) * BLOCKS_PER_GROUP)
#define BLOCK_GROUP_INODE_BITMAP(g) ((g) == 0 ? 4u : (uint32_t)(g) * BLOCKS_PER_GROUP + 1u)
#define BLOCK_GROUP_INODE_TABLE(g)  ((g) == 0 ? 5u : (uint32_t)(g) * BLOCKS_PER_GROUP + 2u)

/**
 * inodes constant
 * - reference: https://www.nongnu.org/ext2-doc/ext2.html#inode-table
 */
#define EXT2_S_IFREG 0x8000 // regular file
#define EXT2_S_IFDIR 0x4000 // directory

/* FILE TYPE CONSTANT*/
/**
 * reference:
 * - https://www.nongnu.org/ext2-doc/ext2.html#linked-directories
 * - Table 4.2. Defined Inode File Type Values
 */
#define EXT2_FT_UNKNOWN 0 // Unknown File Type
#define EXT2_FT_REG_FILE 1 // Regular File
#define EXT2_FT_DIR 2 // Directory

/**
 * EXT2DriverRequest
 * Derived and modified from FAT32DriverRequest legacy IF2130 OS
 */
struct EXT2DriverRequest
{
    void *buf;
    char *name;
    uint8_t name_len;
    uint32_t parent_inode;
    uint32_t buffer_size;

    bool is_directory;
} __attribute__((packed));

struct EXT2MoveRequest
{
    uint32_t source_parent_inode;
    char *source_name;
    uint8_t source_name_len;
    uint32_t destination_parent_inode;
    char *destination_name;
    uint8_t destination_name_len;
} __attribute__((packed));

/**
 * EXT2Superblock:
 * - https://www.nongnu.org/ext2-doc/ext2.html#superblock
 */
struct EXT2Superblock
{
    uint32_t s_inodes_count;      // total number of inodes
    uint32_t s_blocks_count;      // total number of blocks

    uint32_t s_r_blocks_count;    // reserved blocks for super user
    uint32_t s_free_blocks_count; // total free blocks
    uint32_t s_free_inodes_count; // total free inodes
    uint32_t s_first_data_block;  // id of the block containing the superblock
    uint32_t s_first_ino;         // first inode that can be used

    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;

    uint16_t s_magic;               // EXT2_SUPER_MAGIC
    uint8_t s_prealloc_blocks;      // blocks to preallocate for files
    uint8_t s_prealloc_dir_blocks;  // blocks to preallocate for directories
} __attribute__((packed));

/**
 * reference:
 * - https://www.nongnu.org/ext2-doc/ext2.html#block-group-descriptor-table
 */
struct EXT2BlockGroupDescriptor
{
    uint32_t bg_block_bitmap; // block id of the block bitmap
    uint32_t bg_inode_bitmap; // block id of the inode bitmap
    uint32_t bg_inode_table;  // block id of the inode table

    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;

    uint32_t bg_reserved[3]; // 12 bytes of reserved space for future revisions
} __attribute__((packed));

struct EXT2BlockGroupDescriptorTable
{
    struct EXT2BlockGroupDescriptor table[GROUPS_COUNT];
} __attribute__((packed));

/**
 * EXT2Inode
 * Inode stands for index node, a data structure describing a file-system object.
 */
struct EXT2Inode
{
    uint16_t i_mode;   // file type and access rights
    uint32_t i_size;   // size of the file in bytes
    uint32_t i_blocks; // number of 512-byte blocks used by the file

    /**
     * 15 x 32-bit block numbers pointing to the blocks containing the data.
     * - first 12 entries are direct blocks
     * - 13th is single indirect, 14th double indirect, 15th triple indirect
     */
    uint32_t i_block[15];
} __attribute__((packed));

struct EXT2InodeTable
{
    struct EXT2Inode table[INODES_PER_GROUP];
} __attribute__((packed));

/**
 * EXT2DirectoryEntry
 * Linked List Directory
 * reference:
 * - https://www.nongnu.org/ext2-doc/ext2.html#linked-directories
 */
struct EXT2DirectoryEntry
{
    uint32_t inode;   // inode number of the file entry, 0 means not used
    uint16_t rec_len; // displacement to the next directory entry
    uint16_t name_len; // length of the file name
    uint8_t file_type; // file type
} __attribute__((packed));

/**
 *  REGULAR function
 */

/**
 * get the name of the entry
 * @param entry the directory entry
 * @return the name of the entry
 */
char *get_entry_name(void *entry);

/**
 * get the directory entry from the buffer
 * @param ptr the buffer that contains the directory table
 * @param offset the offset of the entry
 * @return the directory entry
 */
struct EXT2DirectoryEntry *get_directory_entry(void *ptr, uint32_t offset);

/**
 * get the next directory entry from the current entry
 * @param entry the current entry
 * @return the next directory entry
 */
struct EXT2DirectoryEntry *get_next_directory_entry(struct EXT2DirectoryEntry *entry);

/**
 * get the record length of the entry
 * @param name_len the length of the name of the entry
 * @return the record length of the entry
 */
uint16_t get_entry_record_len(uint8_t name_len);

/**
 * get the offset of the first child of the directory
 * @param ptr the buffer that contains the directory table
 * @return the offset of the first child of the directory
 */
uint32_t get_dir_first_child_offset(void *ptr);

/* =================== MAIN FUNCTION OF EXT2 FILESYSTEM ============================*/

/**
 * @brief get bgd index from inode, inode will starts at index 1
 * @param inode 1 to INODES_PER_GROUP * GROUPS_COUNT
 * @return bgd index (0 to GROUPS_COUNT - 1)
 */
uint32_t inode_to_bgd(uint32_t inode);

/**
 * @brief get inode local index in the corresponding bgd
 * @param inode 1 to INODES_PER_GROUP * GROUPS_COUNT
 * @return local index
 */
uint32_t inode_to_local(uint32_t inode);

/**
 * @brief create a new directory using given node
 * first item of directory table is its node location (name will be .)
 * second item of directory is its parent location (name will be ..)
 * @param node pointer of inode
 * @param inode inode that already allocated
 * @param parent_inode inode of parent directory (if root directory, the parent is itself)
 */
bool init_directory_table(struct EXT2Inode *node, uint32_t inode, uint32_t parent_inode);

/**
 * @brief check whether filesystem signature is missing or not in boot sector
 *
 * @return true if memcmp(boot_sector, fs_signature) returning inequality
 */
bool is_empty_storage(void);

/**
 * @brief create a new EXT2 filesystem. Will write fs_signature into boot sector,
 * initialize super block, bgd table, block and inode bitmap, and create root directory
 */
void create_ext2(void);

/**
 * @brief Initialize file system driver state, if is_empty_storage() then create_ext2()
 * Else, read and cache super block (located at block 1) and bgd table (located at block 2) into state
 */
void initialize_filesystem_ext2(void);

/**
 * @brief check whether a directory table has children or not
 * @param inode of a directory table
 * @return true if first_child_entry->inode = 0
 */
bool is_directory_empty(uint32_t inode);

/* =============================== CRUD FUNC ======================================== */

/**
 * @brief EXT2 Folder / Directory read
 * @param request buf point to struct EXT2 Directory
 * @return Error code: 0 success - 1 not a folder - 2 not found - 3 parent folder invalid - -1 unknown
 */
int8_t read_directory(struct EXT2DriverRequest *prequest);

/**
 * @brief EXT2 read, read a file from file system
 * @param request All attribute will be used except is_dir for read, buffer_size will limit reading count
 * @return Error code: 0 success - 1 not a file - 2 not enough buffer - 3 not found - 4 parent folder invalid - -1 unknown
 */
int8_t read(struct EXT2DriverRequest request);

/**
 * Load a process image into virtual address zero. The caller must have mapped
 * a writable user range at address zero large enough for the file.
 */
int8_t read_process_image_at_zero(struct EXT2DriverRequest request);

/**
 * @brief EXT2 get file size, retrieve a regular file's size in bytes
 * @param request parent_inode, name and name_len will be used
 * @param size out parameter, will be filled with the file size
 * @return Error code: 0 success - 1 not a file - 3 not found - 4 parent folder invalid
 */
int8_t get_file_size(struct EXT2DriverRequest request, uint32_t *size);

/** Return the on-disk byte size of a directory inode. */
int8_t get_directory_size(uint32_t inode, uint32_t *size);

/**
 * @brief EXT2 write, write a file or a folder to file system
 *
 * @param is_directory selects directory creation; a regular file may have size zero.
 * @return Error code: 0 success - 1 file/folder already exist - 2 invalid parent folder - -1 unknown
 */
int8_t write(struct EXT2DriverRequest *request);

/**
 * @brief EXT2 delete, delete a file or empty directory in file system
 * @param request buf and buffer_size is unused, is_dir == true means delete folder
 * @return Error code: 0 success - 1 not found - 2 folder is not empty - 3 parent folder invalid -1 unknown
 */
int8_t delete(struct EXT2DriverRequest request);

/**
 * Atomically relink a file or directory entry under a new parent/name.
 * @return 0 success, 1 source not found, 2 destination exists,
 *         3 invalid parent, 4 root/special entry/cycle, -1 malformed request.
 */
int8_t move_entry(const struct EXT2MoveRequest *request);

/* =============================== MEMORY ==========================================*/

/**
 * @brief get a free inode from the disk, assuming it is always available
 * @return new inode
 */
uint32_t allocate_node(void);

/**
 * @brief deallocate node from the disk, will also deallocate its used blocks
 * @param inode that needs to be deallocated
 */
void deallocate_node(uint32_t inode);

/**
 * @brief deallocate node blocks
 * @param locations node->block
 * @param blocks number of blocks
 */
void deallocate_blocks(void *loc, uint32_t blocks);

/**
 * @brief deallocate block from the disk
 * @param locations block locations
 * @param blocks number of blocks
 * @param bitmap block bitmap
 * @param depth depth of the block
 * @param last_bgd last bgd that is used
 * @param bgd_loaded whether bgd is loaded or not
 * @return new last bgd
 */
uint32_t deallocate_block(uint32_t *locations, uint32_t blocks, struct BlockBuffer *bitmap, uint32_t depth, uint32_t *last_bgd, bool bgd_loaded);

/**
 * @brief write node->block in the given node, will allocate at least node->blocks number of blocks
 * @param ptr the buffer that needs to be written
 * @param node pointer of the node
 * @param prefered_bgd it is located at the node inode bgd
 */
int8_t allocate_node_blocks(void *ptr, struct EXT2Inode *node, uint32_t prefered_bgd);

/**
 * @brief update the node to the disk
 * @param node pointer of node
 * @param inode location of the node
 */
void sync_node(struct EXT2Inode *node, uint32_t inode);

#endif
