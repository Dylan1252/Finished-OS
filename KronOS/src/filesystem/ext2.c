#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/stdlib/string.h"
#include "header/driver/disk.h"
#include "header/filesystem/ext2.h"

const uint8_t fs_signature[BLOCK_SIZE] = {
    'C', 'o', 'u', 'r', 's', 'e', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',  ' ',
    'D', 'e', 's', 'i', 'g', 'n', 'e', 'd', ' ', 'b', 'y', ' ', ' ', ' ', ' ',  ' ',
    'L', 'a', 'b', ' ', 'S', 'i', 's', 't', 'e', 'r', ' ', 'I', 'T', 'B', ' ',  ' ',
    'M', 'a', 'd', 'e', ' ', 'w', 'i', 't', 'h', ' ', '<', '3', ' ', ' ', ' ',  ' ',
    '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '2', '0', '2', '5', '\n',
    [BLOCK_SIZE-2] = 'O',
    [BLOCK_SIZE-1] = 'k',
};

/* Filesystem state kept in RAM, backed by dedicated 1-block buffers */
static uint8_t superblock_buffer[BLOCK_SIZE];
static uint8_t bgd_table_buffer[BLOCK_SIZE];

#define SUPERBLOCK ((struct EXT2Superblock *) superblock_buffer)
#define BGD_TABLE  ((struct EXT2BlockGroupDescriptorTable *) bgd_table_buffer)

/* =============================== HELPERS ========================================= */

char *get_entry_name(void *entry) {
    return (char *) entry + sizeof(struct EXT2DirectoryEntry);
}

struct EXT2DirectoryEntry *get_directory_entry(void *ptr, uint32_t offset) {
    return (struct EXT2DirectoryEntry *) ((uint8_t *) ptr + offset);
}

struct EXT2DirectoryEntry *get_next_directory_entry(struct EXT2DirectoryEntry *entry) {
    return (struct EXT2DirectoryEntry *) ((uint8_t *) entry + entry->rec_len);
}

uint16_t get_entry_record_len(uint8_t name_len) {
    return (name_len + sizeof(struct EXT2DirectoryEntry) + 3) & ~3;
}

uint32_t get_dir_first_child_offset(void *ptr) {
    struct EXT2DirectoryEntry *first  = get_directory_entry(ptr, 0);
    struct EXT2DirectoryEntry *second = get_next_directory_entry(first);
    return (uint32_t) ((uint8_t *) second - (uint8_t *) ptr) + second->rec_len;
}

uint32_t inode_to_bgd(uint32_t inode) {
    return (inode - 1) / INODES_PER_GROUP;
}

uint32_t inode_to_local(uint32_t inode) {
    return (inode - 1) % INODES_PER_GROUP;
}

/* =============================== LOW LEVEL I/O =================================== */

static void read_inode(struct EXT2Inode *node, uint32_t inode) {
    uint32_t bgd_index   = inode_to_bgd(inode);
    uint32_t local_index = inode_to_local(inode);
    uint32_t table_block = BLOCK_GROUP_INODE_TABLE(bgd_index);
    uint32_t byte_offset = local_index * INODE_SIZE;
    uint32_t block_index = table_block + byte_offset / BLOCK_SIZE;
    uint32_t offset      = byte_offset % BLOCK_SIZE;

    struct BlockBuffer first_block;
    read_blocks(&first_block, block_index, 1);

    if (offset + INODE_SIZE <= BLOCK_SIZE) {
        memcpy(node, first_block.buf + offset, INODE_SIZE);
    } else {
        struct BlockBuffer second_block;
        read_blocks(&second_block, block_index + 1, 1);
        uint32_t first_part = BLOCK_SIZE - offset;
        memcpy(node, first_block.buf + offset, first_part);
        memcpy((uint8_t *) node + first_part, second_block.buf, INODE_SIZE - first_part);
    }
}

static void write_inode(struct EXT2Inode *node, uint32_t inode) {
    uint32_t bgd_index   = inode_to_bgd(inode);
    uint32_t local_index = inode_to_local(inode);
    uint32_t table_block = BLOCK_GROUP_INODE_TABLE(bgd_index);
    uint32_t byte_offset = local_index * INODE_SIZE;
    uint32_t block_index = table_block + byte_offset / BLOCK_SIZE;
    uint32_t offset      = byte_offset % BLOCK_SIZE;

    struct BlockBuffer first_block;
    read_blocks(&first_block, block_index, 1);

    if (offset + INODE_SIZE <= BLOCK_SIZE) {
        memcpy(first_block.buf + offset, node, INODE_SIZE);
        write_blocks(&first_block, block_index, 1);
    } else {
        struct BlockBuffer second_block;
        read_blocks(&second_block, block_index + 1, 1);
        uint32_t first_part = BLOCK_SIZE - offset;
        memcpy(first_block.buf + offset, node, first_part);
        memcpy(second_block.buf, (uint8_t *) node + first_part, INODE_SIZE - first_part);
        write_blocks(&first_block, block_index, 1);
        write_blocks(&second_block, block_index + 1, 1);
    }
}

void sync_node(struct EXT2Inode *node, uint32_t inode) {
    write_inode(node, inode);
}

/* Write superblock & block group descriptor table back to storage */
static void sync_metadata(void) {
    write_blocks(superblock_buffer, 1, 1);
    write_blocks(bgd_table_buffer, 2, 1);
}

/* =============================== BLOCK ALLOCATOR ================================= */

static uint32_t allocate_block(void) {
    for (uint32_t group = 0; group < GROUPS_COUNT; group++) {
        struct BlockBuffer bitmap;
        read_blocks(&bitmap, BLOCK_GROUP_BLOCK_BITMAP(group), 1);
        for (uint32_t i = 0; i < BLOCKS_PER_GROUP; i++) {
            if (!(bitmap.buf[i / 8] & (1 << (i % 8)))) {
                bitmap.buf[i / 8] |= (1 << (i % 8));
                write_blocks(&bitmap, BLOCK_GROUP_BLOCK_BITMAP(group), 1);
                BGD_TABLE->table[group].bg_free_blocks_count--;
                SUPERBLOCK->s_free_blocks_count--;
                return group * BLOCKS_PER_GROUP + i;
            }
        }
    }
    return 0; // out of blocks
}

static void deallocate_block_number(uint32_t block) {
    if (block == 0 || block >= DISK_SPACE / BLOCK_SIZE) return;
    uint32_t group = block / BLOCKS_PER_GROUP;
    uint32_t index = block % BLOCKS_PER_GROUP;

    struct BlockBuffer bitmap;
    read_blocks(&bitmap, BLOCK_GROUP_BLOCK_BITMAP(group), 1);
    if (bitmap.buf[index / 8] & (1 << (index % 8))) {
        bitmap.buf[index / 8] &= ~(1 << (index % 8));
        write_blocks(&bitmap, BLOCK_GROUP_BLOCK_BITMAP(group), 1);
        BGD_TABLE->table[group].bg_free_blocks_count++;
        SUPERBLOCK->s_free_blocks_count++;
    }
}

uint32_t deallocate_block(uint32_t *locations, uint32_t blocks, struct BlockBuffer *bitmap, uint32_t depth, uint32_t *last_bgd, bool bgd_loaded) {
    (void) bitmap; (void) depth; (void) last_bgd; (void) bgd_loaded;
    for (uint32_t i = 0; i < blocks; i++)
        deallocate_block_number(locations[i]);
    return 0;
}

void deallocate_blocks(void *loc, uint32_t blocks) {
    deallocate_block((uint32_t *) loc, blocks, NULL, 0, NULL, false);
}

/* =============================== INODE ALLOCATOR ================================= */

uint32_t allocate_node(void) {
    for (uint32_t group = 0; group < GROUPS_COUNT; group++) {
        struct BlockBuffer bitmap;
        read_blocks(&bitmap, BLOCK_GROUP_INODE_BITMAP(group), 1);
        for (uint32_t i = 0; i < INODES_PER_GROUP; i++) {
            if (!(bitmap.buf[i / 8] & (1 << (i % 8)))) {
                bitmap.buf[i / 8] |= (1 << (i % 8));
                write_blocks(&bitmap, BLOCK_GROUP_INODE_BITMAP(group), 1);
                BGD_TABLE->table[group].bg_free_inodes_count--;
                SUPERBLOCK->s_free_inodes_count--;
                return group * INODES_PER_GROUP + i + 1;
            }
        }
    }
    return 0;
}

/* Free a single inode index from its bitmap */
static void deallocate_node_number(uint32_t inode) {
    uint32_t group = inode_to_bgd(inode);
    uint32_t index = inode_to_local(inode);
    struct BlockBuffer bitmap;
    read_blocks(&bitmap, BLOCK_GROUP_INODE_BITMAP(group), 1);
    if (bitmap.buf[index / 8] & (1 << (index % 8))) {
        bitmap.buf[index / 8] &= ~(1 << (index % 8));
        write_blocks(&bitmap, BLOCK_GROUP_INODE_BITMAP(group), 1);
        BGD_TABLE->table[group].bg_free_inodes_count++;
        SUPERBLOCK->s_free_inodes_count++;
    }
}

/* =============================== BLOCK MAPPING =================================== */

/* Read-only resolution of a logical block index into a physical block number */
static uint32_t inode_block_at(struct EXT2Inode *node, uint32_t index) {
    if (index < 12) return node->i_block[index];

    index -= 12;
    if (index < BLOCK_SIZE / 4) { // single indirect
        if (node->i_block[12] == 0) return 0;
        uint32_t indirect[BLOCK_SIZE / 4];
        read_blocks(indirect, node->i_block[12], 1);
        return indirect[index];
    }

    index -= BLOCK_SIZE / 4;
    { // double indirect
        if (node->i_block[13] == 0) return 0;
        uint32_t indirect[BLOCK_SIZE / 4];
        read_blocks(indirect, node->i_block[13], 1);
        uint32_t first  = indirect[index / (BLOCK_SIZE / 4)];
        if (first == 0) return 0;
        read_blocks(indirect, first, 1);
        return indirect[index % (BLOCK_SIZE / 4)];
    }
}

static uint32_t filesystem_block_count(uint32_t size) {
    return size / BLOCK_SIZE + (size % BLOCK_SIZE != 0);
}

/* Store a physical block number at a logical position, allocating indirect blocks */
static bool inode_set_block(struct EXT2Inode *node, uint32_t index, uint32_t block) {
    if (index < 12) {
        node->i_block[index] = block;
        return true;
    }

    index -= 12;
    if (index < BLOCK_SIZE / 4) { // single indirect
        uint32_t indirect[BLOCK_SIZE / 4];
        if (node->i_block[12] == 0) {
            node->i_block[12] = allocate_block();
            if (node->i_block[12] == 0) return false;
            memset(indirect, 0, BLOCK_SIZE);
        } else {
            read_blocks(indirect, node->i_block[12], 1);
        }
        indirect[index] = block;
        write_blocks(indirect, node->i_block[12], 1);
        return true;
    }

    index -= BLOCK_SIZE / 4;
    { // double indirect
        uint32_t indirect[BLOCK_SIZE / 4];
        if (node->i_block[13] == 0) {
            node->i_block[13] = allocate_block();
            if (node->i_block[13] == 0) return false;
            memset(indirect, 0, BLOCK_SIZE);
        } else {
            read_blocks(indirect, node->i_block[13], 1);
        }
        uint32_t first_index = index / (BLOCK_SIZE / 4);
        uint32_t first_block = indirect[first_index];
        if (first_block == 0) {
            first_block = allocate_block();
            if (first_block == 0) return false;
            indirect[first_index] = first_block;
            write_blocks(indirect, node->i_block[13], 1);
        }
        uint32_t second[BLOCK_SIZE / 4];
        read_blocks(second, first_block, 1);
        second[index % (BLOCK_SIZE / 4)] = block;
        write_blocks(second, first_block, 1);
        return true;
    }
}

static void free_inode_blocks(struct EXT2Inode *node) {
    uint32_t block_count = filesystem_block_count(node->i_size);
    uint32_t direct_count = block_count < 12 ? block_count : 12;
    for (uint32_t i = 0; i < direct_count; i++)
        deallocate_block_number(node->i_block[i]);

    uint32_t single_count = block_count > 12 ? block_count - 12 : 0;
    if (single_count > BLOCK_SIZE / 4) single_count = BLOCK_SIZE / 4;
    if (node->i_block[12] != 0) {
        uint32_t indirect[BLOCK_SIZE / 4];
        read_blocks(indirect, node->i_block[12], 1);
        for (uint32_t i = 0; i < single_count; i++)
            deallocate_block_number(indirect[i]);
        deallocate_block_number(node->i_block[12]);
    }

    uint32_t double_count = block_count > 12 + BLOCK_SIZE / 4
                                ? block_count - 12 - BLOCK_SIZE / 4 : 0;
    if (node->i_block[13] != 0) {
        uint32_t outer[BLOCK_SIZE / 4];
        read_blocks(outer, node->i_block[13], 1);
        uint32_t max_double = (BLOCK_SIZE / 4) * (BLOCK_SIZE / 4);
        if (double_count > max_double) double_count = max_double;
        for (uint32_t i = 0; i < BLOCK_SIZE / 4 && i * (BLOCK_SIZE / 4) < double_count; i++) {
            if (outer[i] == 0) continue;
            uint32_t inner[BLOCK_SIZE / 4];
            read_blocks(inner, outer[i], 1);
            uint32_t count = double_count - i * (BLOCK_SIZE / 4);
            if (count > BLOCK_SIZE / 4) count = BLOCK_SIZE / 4;
            for (uint32_t j = 0; j < count; j++)
                deallocate_block_number(inner[j]);
            deallocate_block_number(outer[i]);
        }
        deallocate_block_number(node->i_block[13]);
    }

    memset(node->i_block, 0, sizeof(node->i_block));
}

int8_t allocate_node_blocks(void *ptr, struct EXT2Inode *node, uint32_t prefered_bgd) {
    (void) prefered_bgd;
    uint32_t block_count = filesystem_block_count(node->i_size);
    for (uint32_t i = 0; i < block_count; i++) {
        uint32_t block = allocate_block();
        if (block == 0 || !inode_set_block(node, i, block)) {
            deallocate_block_number(block);
            free_inode_blocks(node);
            return -1;
        }

        struct BlockBuffer data;
        memset(&data, 0, BLOCK_SIZE);
        uint32_t remaining = node->i_size - i * BLOCK_SIZE;
        uint32_t chunk = remaining < BLOCK_SIZE ? remaining : BLOCK_SIZE;
        if (ptr != NULL)
            memcpy(data.buf, (uint8_t *) ptr + i * BLOCK_SIZE, chunk);
        write_blocks(&data, block, 1);
    }
    node->i_blocks = node->i_size / BLOCK_SIZE + ((node->i_size % BLOCK_SIZE) ? 1 : 0);
    return 0;
}

void deallocate_node(uint32_t inode) {
    struct EXT2Inode node;
    read_inode(&node, inode);
    free_inode_blocks(&node);
    deallocate_node_number(inode);
}

/* =============================== DIRECTORY HELPERS =============================== */

static bool name_matches(struct EXT2DirectoryEntry *entry, const char *name, uint8_t name_len) {
    return entry->name_len == name_len &&
           memcmp(get_entry_name(entry), name, name_len) == 0;
}

static bool directory_block_is_valid(const uint8_t *buf) {
    uint32_t offset = 0;
    while (offset < BLOCK_SIZE) {
        const struct EXT2DirectoryEntry *entry =
            (const struct EXT2DirectoryEntry *) (buf + offset);
        if (entry->rec_len < sizeof(*entry) || (entry->rec_len & 3u) != 0 ||
            offset + entry->rec_len > BLOCK_SIZE ||
            entry->name_len > entry->rec_len - sizeof(*entry) ||
            entry->inode > GROUPS_COUNT * INODES_PER_GROUP)
            return false;
        offset += entry->rec_len;
    }
    return offset == BLOCK_SIZE;
}

/* Find a directory entry by name within a directory inode */
static bool find_entry(uint32_t dir_inode, const char *name, uint8_t name_len,
                       uint32_t *out_block, uint32_t *out_offset,
                       struct EXT2DirectoryEntry *out_entry) {
    struct EXT2Inode dir;
    read_inode(&dir, dir_inode);

    uint32_t block_count = dir.i_size / BLOCK_SIZE;
    for (uint32_t b = 0; b < block_count; b++) {
        uint32_t physical = inode_block_at(&dir, b);
        if (physical == 0) continue;

        struct BlockBuffer block;
        read_blocks(&block, physical, 1);
        if (!directory_block_is_valid(block.buf)) return false;

        uint32_t offset = 0;
        while (offset < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *entry = get_directory_entry(block.buf, offset);
            if (entry->rec_len == 0) break;
            if (entry->inode != 0 && name_matches(entry, name, name_len)) {
                if (out_block)  *out_block = physical;
                if (out_offset) *out_offset = offset;
                if (out_entry)  memcpy(out_entry, entry, sizeof(struct EXT2DirectoryEntry));
                return true;
            }
            offset += entry->rec_len;
        }
    }
    return false;
}

/* Add a new entry into a directory, growing it by one block when needed */
static bool add_entry(uint32_t dir_inode, uint32_t child_inode,
                      const char *name, uint8_t name_len, uint8_t file_type) {
    struct EXT2Inode dir;
    read_inode(&dir, dir_inode);

    uint16_t needed = get_entry_record_len(name_len);
    uint32_t block_count = dir.i_size / BLOCK_SIZE;

    for (uint32_t b = 0; b < block_count; b++) {
        uint32_t physical = inode_block_at(&dir, b);
        if (physical == 0) continue;

        struct BlockBuffer block;
        read_blocks(&block, physical, 1);
        if (!directory_block_is_valid(block.buf)) return false;

        uint32_t offset = 0;
        while (offset < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *entry = get_directory_entry(block.buf, offset);
            if (entry->rec_len == 0) break;

            if (entry->inode == 0) {
                if (entry->rec_len >= needed) {
                    entry->inode = child_inode;
                    entry->name_len = name_len;
                    entry->file_type = file_type;
                    memcpy(get_entry_name(entry), name, name_len);
                    write_blocks(&block, physical, 1);
                    return true;
                }
            } else {
                uint16_t used = get_entry_record_len(entry->name_len);
                if (entry->rec_len > used && entry->rec_len - used >= needed) {
                    uint16_t old_rec_len = entry->rec_len;
                    entry->rec_len = used;

                    struct EXT2DirectoryEntry *new_entry = get_directory_entry(block.buf, offset + used);
                    new_entry->inode = child_inode;
                    new_entry->rec_len = old_rec_len - used;
                    new_entry->name_len = name_len;
                    new_entry->file_type = file_type;
                    memcpy(get_entry_name(new_entry), name, name_len);
                    write_blocks(&block, physical, 1);
                    return true;
                }
            }
            offset += entry->rec_len;
        }
    }

    /* No slack found, allocate a new block for the directory */
    uint32_t new_block = allocate_block();
    if (new_block == 0) return false;

    struct BlockBuffer block;
    memset(&block, 0, BLOCK_SIZE);
    struct EXT2DirectoryEntry *entry = get_directory_entry(block.buf, 0);
    entry->inode = child_inode;
    entry->rec_len = BLOCK_SIZE;
    entry->name_len = name_len;
    entry->file_type = file_type;
    memcpy(get_entry_name(entry), name, name_len);
    write_blocks(&block, new_block, 1);

    if (!inode_set_block(&dir, block_count, new_block)) {
        deallocate_block_number(new_block);
        return false;
    }
    dir.i_size += BLOCK_SIZE;
    dir.i_blocks += 1;
    write_inode(&dir, dir_inode);
    return true;
}

/* Remove an entry from a directory by merging it into the previous entry */
static bool remove_entry(uint32_t dir_inode, uint32_t target_block, uint32_t target_offset) {
    struct EXT2Inode dir;
    read_inode(&dir, dir_inode);

    uint32_t block_count = dir.i_size / BLOCK_SIZE;
    for (uint32_t b = 0; b < block_count; b++) {
        uint32_t physical = inode_block_at(&dir, b);
        if (physical != target_block) continue;

        struct BlockBuffer block;
        read_blocks(&block, physical, 1);
        if (!directory_block_is_valid(block.buf)) return false;

        uint32_t offset = 0;
        struct EXT2DirectoryEntry *previous = NULL;
        bool removed = false;
        while (offset < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *entry = get_directory_entry(block.buf, offset);
            if (entry->rec_len == 0) break;
            if (offset == target_offset) {
                if (previous != NULL)
                    previous->rec_len += entry->rec_len;
                else
                    entry->inode = 0;
                removed = true;
                break;
            }
            previous = entry;
            offset += entry->rec_len;
        }
        if (!removed) return false;
        write_blocks(&block, physical, 1);
        return true;
    }
    return false;
}

bool is_directory_empty(uint32_t inode) {
    struct EXT2Inode dir;
    read_inode(&dir, inode);

    uint32_t block_count = dir.i_size / BLOCK_SIZE;
    for (uint32_t b = 0; b < block_count; b++) {
        uint32_t physical = inode_block_at(&dir, b);
        if (physical == 0) continue;

        struct BlockBuffer block;
        read_blocks(&block, physical, 1);
        if (!directory_block_is_valid(block.buf)) return false;

        uint32_t offset = 0;
        while (offset < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *entry = get_directory_entry(block.buf, offset);
            if (entry->rec_len == 0) break;
            if (entry->inode != 0) {
                uint16_t name_len = entry->name_len;
                char *name = get_entry_name(entry);
                bool is_dot  = (name_len == 1 && name[0] == '.');
                bool is_dot2 = (name_len == 2 && name[0] == '.' && name[1] == '.');
                if (!is_dot && !is_dot2)
                    return false;
            }
            offset += entry->rec_len;
        }
    }
    return true;
}

/* =============================== INITIALIZER ===================================== */

bool is_empty_storage(void) {
    struct BlockBuffer boot_sector;
    read_blocks(&boot_sector, BOOT_SECTOR, 1);
    return memcmp(boot_sector.buf, fs_signature, BLOCK_SIZE) != 0;
}

bool init_directory_table(struct EXT2Inode *node, uint32_t inode, uint32_t parent_inode) {
    node->i_mode = EXT2_S_IFDIR;
    node->i_size = BLOCK_SIZE;
    node->i_blocks = 1;

    uint32_t block = allocate_block();
    if (block == 0) return false;
    node->i_block[0] = block;

    struct BlockBuffer buffer;
    memset(&buffer, 0, BLOCK_SIZE);

    struct EXT2DirectoryEntry *self = get_directory_entry(buffer.buf, 0);
    self->inode = inode;
    self->name_len = 1;
    self->file_type = EXT2_FT_DIR;
    self->rec_len = get_entry_record_len(1);
    get_entry_name(self)[0] = '.';

    struct EXT2DirectoryEntry *parent = get_directory_entry(buffer.buf, self->rec_len);
    parent->inode = parent_inode;
    parent->name_len = 2;
    parent->file_type = EXT2_FT_DIR;
    parent->rec_len = BLOCK_SIZE - self->rec_len;
    get_entry_name(parent)[0] = '.';
    get_entry_name(parent)[1] = '.';

    write_blocks(&buffer, block, 1);
    return true;
}

void create_ext2(void) {
    /* Boot sector */
    struct BlockBuffer boot_sector;
    memcpy(boot_sector.buf, fs_signature, BLOCK_SIZE);
    write_blocks(&boot_sector, BOOT_SECTOR, 1);

    /* Superblock */
    memset(superblock_buffer, 0, BLOCK_SIZE);
    SUPERBLOCK->s_inodes_count      = GROUPS_COUNT * INODES_PER_GROUP;
    SUPERBLOCK->s_blocks_count      = DISK_SPACE / BLOCK_SIZE;
    SUPERBLOCK->s_r_blocks_count    = 0;
    SUPERBLOCK->s_free_inodes_count = GROUPS_COUNT * INODES_PER_GROUP;
    SUPERBLOCK->s_free_blocks_count = DISK_SPACE / BLOCK_SIZE;
    SUPERBLOCK->s_first_data_block  = 1;
    SUPERBLOCK->s_first_ino         = 1;
    SUPERBLOCK->s_blocks_per_group  = BLOCKS_PER_GROUP;
    SUPERBLOCK->s_frags_per_group   = BLOCKS_PER_GROUP;
    SUPERBLOCK->s_inodes_per_group  = INODES_PER_GROUP;
    SUPERBLOCK->s_magic             = EXT2_SUPER_MAGIC;
    SUPERBLOCK->s_prealloc_blocks   = 0;
    SUPERBLOCK->s_prealloc_dir_blocks = 0;

    /* Block group descriptor table + block/inode bitmaps */
    memset(bgd_table_buffer, 0, BLOCK_SIZE);
    for (uint32_t group = 0; group < GROUPS_COUNT; group++) {
        BGD_TABLE->table[group].bg_block_bitmap = BLOCK_GROUP_BLOCK_BITMAP(group);
        BGD_TABLE->table[group].bg_inode_bitmap = BLOCK_GROUP_INODE_BITMAP(group);
        BGD_TABLE->table[group].bg_inode_table  = BLOCK_GROUP_INODE_TABLE(group);
        BGD_TABLE->table[group].bg_used_dirs_count = 0;
        BGD_TABLE->table[group].bg_pad = 0;
        for (uint32_t i = 0; i < 3; i++)
            BGD_TABLE->table[group].bg_reserved[i] = 0;

        /* Mark reserved metadata blocks as used */
        uint32_t used_blocks = (group == 0) ? (3 + 2 + INODES_TABLE_BLOCK_COUNT)
                                            : (2 + INODES_TABLE_BLOCK_COUNT);
        struct BlockBuffer block_bitmap;
        memset(&block_bitmap, 0, BLOCK_SIZE);
        for (uint32_t i = 0; i < used_blocks; i++)
            block_bitmap.buf[i / 8] |= (1 << (i % 8));
        write_blocks(&block_bitmap, BLOCK_GROUP_BLOCK_BITMAP(group), 1);

        BGD_TABLE->table[group].bg_free_blocks_count = BLOCKS_PER_GROUP - used_blocks;
        BGD_TABLE->table[group].bg_free_inodes_count = INODES_PER_GROUP;

        /* Empty inode bitmap */
        struct BlockBuffer inode_bitmap;
        memset(&inode_bitmap, 0, BLOCK_SIZE);
        write_blocks(&inode_bitmap, BLOCK_GROUP_INODE_BITMAP(group), 1);

        /* Zero inode table */
        struct BlockBuffer empty;
        memset(&empty, 0, BLOCK_SIZE);
        for (uint32_t i = 0; i < INODES_TABLE_BLOCK_COUNT; i++)
            write_blocks(&empty, BLOCK_GROUP_INODE_TABLE(group) + i, 1);
    }

    /* Recompute free block count from descriptors */
    uint32_t free_blocks = 0;
    for (uint32_t group = 0; group < GROUPS_COUNT; group++)
        free_blocks += BGD_TABLE->table[group].bg_free_blocks_count;
    SUPERBLOCK->s_free_blocks_count = free_blocks;

    /* Root directory (inode 1, parent is itself) */
    uint32_t root_inode = allocate_node();
    struct EXT2Inode root;
    memset(&root, 0, sizeof(struct EXT2Inode));
    if (root_inode == 0 || !init_directory_table(&root, root_inode, root_inode)) return;
    write_inode(&root, root_inode);
    BGD_TABLE->table[inode_to_bgd(root_inode)].bg_used_dirs_count++;

    sync_metadata();
}

void initialize_filesystem_ext2(void) {
    if (is_empty_storage()) {
        create_ext2();
    } else {
        read_blocks(superblock_buffer, 1, 1);
        read_blocks(bgd_table_buffer, 2, 1);
    }
}

/* =============================== CRUD ============================================ */

static int8_t validate_parent(uint32_t parent_inode, struct EXT2Inode *parent) {
    if (parent_inode == 0 || parent_inode > GROUPS_COUNT * INODES_PER_GROUP)
        return 2;
    read_inode(parent, parent_inode);
    if (parent->i_mode != EXT2_S_IFDIR)
        return 2;
    return 0;
}

static bool valid_move_name(const char *name, uint8_t name_len) {
    if (name == NULL || name_len == 0) return false;
    if ((name_len == 1 && name[0] == '.') ||
        (name_len == 2 && name[0] == '.' && name[1] == '.'))
        return false;
    for (uint32_t i = 0; i < name_len; i++)
        if (name[i] == '/' || name[i] == 0) return false;
    return true;
}

static int8_t directory_move_cycle_check(uint32_t source_inode, uint32_t destination_parent) {
    uint32_t current = destination_parent;
    uint32_t limit = GROUPS_COUNT * INODES_PER_GROUP;
    for (uint32_t depth = 0; depth <= limit; depth++) {
        if (current == source_inode) return 1;
        if (current == 1) return 0;

        struct EXT2DirectoryEntry parent_entry;
        if (!find_entry(current, "..", 2, NULL, NULL, &parent_entry) ||
            parent_entry.file_type != EXT2_FT_DIR || parent_entry.inode == 0 ||
            parent_entry.inode == current)
            return -1;
        current = parent_entry.inode;
    }
    return -1;
}

static bool update_directory_parent(uint32_t directory_inode, uint32_t old_parent,
                                    uint32_t new_parent) {
    uint32_t block_number, offset;
    struct EXT2DirectoryEntry parent_entry;
    if (!find_entry(directory_inode, "..", 2, &block_number, &offset, &parent_entry) ||
        parent_entry.inode != old_parent || parent_entry.file_type != EXT2_FT_DIR)
        return false;

    struct BlockBuffer block;
    read_blocks(&block, block_number, 1);
    if (!directory_block_is_valid(block.buf) ||
        offset + sizeof(struct EXT2DirectoryEntry) > BLOCK_SIZE)
        return false;
    struct EXT2DirectoryEntry *entry = get_directory_entry(block.buf, offset);
    if (!name_matches(entry, "..", 2) || entry->inode != old_parent) return false;
    entry->inode = new_parent;
    write_blocks(&block, block_number, 1);
    return true;
}

int8_t move_entry(const struct EXT2MoveRequest *request) {
    if (request == NULL ||
        !valid_move_name(request->source_name, request->source_name_len) ||
        !valid_move_name(request->destination_name, request->destination_name_len))
        return -1;

    struct EXT2Inode source_parent, destination_parent;
    if (validate_parent(request->source_parent_inode, &source_parent) != 0 ||
        validate_parent(request->destination_parent_inode, &destination_parent) != 0)
        return 3;

    uint32_t source_block, source_offset;
    struct EXT2DirectoryEntry source_entry;
    if (!find_entry(request->source_parent_inode, request->source_name,
                    request->source_name_len, &source_block, &source_offset, &source_entry))
        return 1;
    if (source_entry.inode == 1 || source_entry.inode == request->source_parent_inode)
        return 4;
    if (source_entry.file_type != EXT2_FT_REG_FILE && source_entry.file_type != EXT2_FT_DIR)
        return -1;
    struct EXT2Inode source_node;
    read_inode(&source_node, source_entry.inode);
    if ((source_entry.file_type == EXT2_FT_REG_FILE && source_node.i_mode != EXT2_S_IFREG) ||
        (source_entry.file_type == EXT2_FT_DIR && source_node.i_mode != EXT2_S_IFDIR))
        return -1;

    bool same_parent = request->source_parent_inode == request->destination_parent_inode;
    bool same_name = same_parent &&
                     request->source_name_len == request->destination_name_len &&
                     memcmp(request->source_name, request->destination_name,
                            request->source_name_len) == 0;
    if (same_name) return 0;

    if (find_entry(request->destination_parent_inode, request->destination_name,
                   request->destination_name_len, NULL, NULL, NULL))
        return 2;

    if (source_entry.file_type == EXT2_FT_DIR) {
        int8_t cycle = directory_move_cycle_check(source_entry.inode,
                                                   request->destination_parent_inode);
        if (cycle > 0) return 4;
        if (cycle < 0) return -1;
        struct EXT2DirectoryEntry old_parent_entry;
        if (!find_entry(source_entry.inode, "..", 2, NULL, NULL, &old_parent_entry) ||
            old_parent_entry.inode != request->source_parent_inode)
            return -1;
    }

    if (!add_entry(request->destination_parent_inode, source_entry.inode,
                   request->destination_name, request->destination_name_len,
                   source_entry.file_type))
        return -1;

    if (!remove_entry(request->source_parent_inode, source_block, source_offset)) {
        uint32_t added_block, added_offset;
        if (find_entry(request->destination_parent_inode, request->destination_name,
                       request->destination_name_len, &added_block, &added_offset, NULL))
            (void) remove_entry(request->destination_parent_inode, added_block, added_offset);
        sync_metadata();
        return -1;
    }

    if (source_entry.file_type == EXT2_FT_DIR && !same_parent &&
        !update_directory_parent(source_entry.inode, request->source_parent_inode,
                                 request->destination_parent_inode)) {
        (void) add_entry(request->source_parent_inode, source_entry.inode,
                         request->source_name, request->source_name_len,
                         EXT2_FT_DIR);
        uint32_t added_block, added_offset;
        if (find_entry(request->destination_parent_inode, request->destination_name,
                       request->destination_name_len, &added_block, &added_offset, NULL))
            (void) remove_entry(request->destination_parent_inode, added_block, added_offset);
        sync_metadata();
        return -1;
    }

    sync_metadata();
    return 0;
}

static int8_t read_internal(struct EXT2DriverRequest request, bool allow_address_zero) {
    if (request.name == NULL ||
        (request.buf == NULL && request.buffer_size != 0 && !allow_address_zero))
        return -1;
    struct EXT2Inode parent;
    if (validate_parent(request.parent_inode, &parent) != 0)
        return 4;

    struct EXT2DirectoryEntry entry;
    if (!find_entry(request.parent_inode, request.name, request.name_len, NULL, NULL, &entry))
        return 3;
    if (entry.file_type != EXT2_FT_REG_FILE)
        return 1;

    struct EXT2Inode node;
    read_inode(&node, entry.inode);
    if (node.i_mode != EXT2_S_IFREG)
        return 1;
    if (request.buffer_size < node.i_size)
        return 2;

    uint32_t block_count = filesystem_block_count(node.i_size);
    for (uint32_t i = 0; i < block_count; i++) {
        uint32_t physical = inode_block_at(&node, i);
        if (physical == 0) return -1;
        struct BlockBuffer block;
        read_blocks(&block, physical, 1);
        uint32_t offset = i * BLOCK_SIZE;
        uint32_t chunk = node.i_size - offset;
        if (chunk > BLOCK_SIZE) chunk = BLOCK_SIZE;
        memcpy((uint8_t *) request.buf + offset, block.buf, chunk);
    }
    return 0;
}

int8_t read(struct EXT2DriverRequest request) {
    return read_internal(request, false);
}

int8_t read_process_image_at_zero(struct EXT2DriverRequest request) {
    if (request.buf != NULL || request.buffer_size == 0) return -1;
    return read_internal(request, true);
}

int8_t read_directory(struct EXT2DriverRequest *prequest) {
    if (prequest == NULL || prequest->name == NULL ||
        (prequest->buf == NULL && prequest->buffer_size != 0))
        return -1;
    struct EXT2Inode parent;
    if (validate_parent(prequest->parent_inode, &parent) != 0)
        return 3;

    struct EXT2DirectoryEntry entry;
    if (!find_entry(prequest->parent_inode, prequest->name, prequest->name_len, NULL, NULL, &entry))
        return 2;
    if (entry.file_type != EXT2_FT_DIR)
        return 1;

    struct EXT2Inode node;
    read_inode(&node, entry.inode);
    if (node.i_mode != EXT2_S_IFDIR)
        return 1;
    if (node.i_size == 0 || node.i_size % BLOCK_SIZE != 0)
        return -1;
    if (prequest->buffer_size < node.i_size)
        return -1;

    uint32_t size = node.i_size;
    uint32_t block_count = filesystem_block_count(size);
    for (uint32_t i = 0; i < block_count; i++) {
        uint32_t physical = inode_block_at(&node, i);
        if (physical == 0) return -1;
        struct BlockBuffer block;
        read_blocks(&block, physical, 1);
        if (!directory_block_is_valid(block.buf)) return -1;
        uint32_t offset = i * BLOCK_SIZE;
        uint32_t chunk = size - offset;
        if (chunk > BLOCK_SIZE) chunk = BLOCK_SIZE;
        memcpy((uint8_t *) prequest->buf + offset, block.buf, chunk);
    }
    return 0;
}

int8_t get_file_size(struct EXT2DriverRequest request, uint32_t *size) {
    if (request.name == NULL || size == NULL) return -1;
    struct EXT2Inode parent;
    if (validate_parent(request.parent_inode, &parent) != 0)
        return 4;

    struct EXT2DirectoryEntry entry;
    if (!find_entry(request.parent_inode, request.name, request.name_len, NULL, NULL, &entry))
        return 3;
    if (entry.file_type != EXT2_FT_REG_FILE)
        return 1;

    struct EXT2Inode node;
    read_inode(&node, entry.inode);
    if (node.i_mode != EXT2_S_IFREG)
        return 1;

    *size = node.i_size;
    return 0;
}

int8_t get_directory_size(uint32_t inode, uint32_t *size) {
    if (size == NULL || inode == 0 || inode > GROUPS_COUNT * INODES_PER_GROUP)
        return -1;
    struct EXT2Inode node;
    read_inode(&node, inode);
    if (node.i_mode != EXT2_S_IFDIR) return 1;
    if (node.i_size == 0 || node.i_size % BLOCK_SIZE != 0) return -1;
    *size = node.i_size;
    return 0;
}

int8_t write(struct EXT2DriverRequest *request) {
    if (request == NULL || request->name == NULL || request->name_len == 0 ||
        request->name[0] == '/') return -1;
    if ((request->name_len == 1 && request->name[0] == '.') ||
        (request->name_len == 2 && request->name[0] == '.' && request->name[1] == '.'))
        return -1;
    for (uint32_t i = 0; i < request->name_len; i++)
        if (request->name[i] == '/' || request->name[i] == 0) return -1;

    struct EXT2Inode parent;
    if (validate_parent(request->parent_inode, &parent) != 0)
        return 2;

    if (find_entry(request->parent_inode, request->name, request->name_len, NULL, NULL, NULL))
        return 1;

    uint32_t inode = allocate_node();
    if (inode == 0)
        return -1;

    struct EXT2Inode node;
    memset(&node, 0, sizeof(struct EXT2Inode));

    if (request->is_directory) {
        if (!init_directory_table(&node, inode, request->parent_inode)) {
            deallocate_node_number(inode);
            sync_metadata();
            return -1;
        }
        write_inode(&node, inode);
        if (!add_entry(request->parent_inode, inode, request->name, request->name_len, EXT2_FT_DIR)) {
            deallocate_node(inode);
            sync_metadata();
            return -1;
        }
        BGD_TABLE->table[inode_to_bgd(inode)].bg_used_dirs_count++;
    } else {
        if (request->buffer_size != 0 && request->buf == NULL) {
            deallocate_node_number(inode);
            sync_metadata();
            return -1;
        }
        uint32_t maximum_blocks = 12 + BLOCK_SIZE / 4 + (BLOCK_SIZE / 4) * (BLOCK_SIZE / 4);
        uint32_t requested_blocks = filesystem_block_count(request->buffer_size);
        if (requested_blocks > maximum_blocks) {
            deallocate_node_number(inode);
            sync_metadata();
            return -1;
        }
        node.i_mode = EXT2_S_IFREG;
        node.i_size = request->buffer_size;
        if (allocate_node_blocks(request->buf, &node, inode_to_bgd(inode)) != 0) {
            deallocate_node_number(inode);
            sync_metadata();
            return -1;
        }
        write_inode(&node, inode);
        if (!add_entry(request->parent_inode, inode, request->name, request->name_len, EXT2_FT_REG_FILE)) {
            deallocate_node(inode);
            sync_metadata();
            return -1;
        }
    }

    sync_metadata();
    return 0;
}

int8_t delete(struct EXT2DriverRequest request) {
    if (request.name == NULL || request.name_len == 0) return -1;
    struct EXT2Inode parent;
    if (validate_parent(request.parent_inode, &parent) != 0)
        return 3;

    uint32_t entry_block, entry_offset;
    struct EXT2DirectoryEntry entry;
    if (!find_entry(request.parent_inode, request.name, request.name_len,
                    &entry_block, &entry_offset, &entry))
        return 1;

    if (entry.inode == 1 || entry.inode == request.parent_inode ||
        (request.name_len == 1 && request.name[0] == '.') ||
        (request.name_len == 2 && request.name[0] == '.' && request.name[1] == '.'))
        return 2;

    if (entry.file_type == EXT2_FT_DIR) {
        if (!is_directory_empty(entry.inode))
            return 2;
    }

    if (!remove_entry(request.parent_inode, entry_block, entry_offset)) return -1;
    if (entry.file_type == EXT2_FT_DIR)
        BGD_TABLE->table[inode_to_bgd(entry.inode)].bg_used_dirs_count--;
    deallocate_node(entry.inode);
    sync_metadata();
    return 0;
}
