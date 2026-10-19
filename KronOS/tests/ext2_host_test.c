#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "header/filesystem/ext2.h"
#include "header/stdlib/string.h"

static uint8_t disk_image[DISK_SPACE];
static unsigned checks;

static uint32_t test_strlen(const char *s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static bool read_inode_for_test(uint32_t inode_number, struct EXT2Inode *inode) {
    if (inode_number == 0 || inode_number > GROUPS_COUNT * INODES_PER_GROUP) return false;
    uint32_t group = (inode_number - 1u) / INODES_PER_GROUP;
    uint32_t local = (inode_number - 1u) % INODES_PER_GROUP;
    uint32_t byte_offset = local * INODE_SIZE;
    uint32_t block = BLOCK_GROUP_INODE_TABLE(group) + byte_offset / BLOCK_SIZE;
    uint32_t offset = byte_offset % BLOCK_SIZE;
    uint32_t first = BLOCK_SIZE - offset;
    if (first > INODE_SIZE) first = INODE_SIZE;
    memcpy(inode, disk_image + (uint64_t) block * BLOCK_SIZE + offset, first);
    if (first < INODE_SIZE)
        memcpy((uint8_t *) inode + first,
               disk_image + (uint64_t) (block + 1u) * BLOCK_SIZE,
               INODE_SIZE - first);
    return true;
}

static uint32_t test_inode_data_block(const struct EXT2Inode *inode, uint32_t index) {
    uint32_t pointers[BLOCK_SIZE / sizeof(uint32_t)];
    uint32_t per_indirect = BLOCK_SIZE / sizeof(uint32_t);
    if (index < 12u) return inode->i_block[index];
    index -= 12u;
    if (index < per_indirect) {
        if (inode->i_block[12] == 0) return 0;
        memcpy(pointers, disk_image + (uint64_t) inode->i_block[12] * BLOCK_SIZE, BLOCK_SIZE);
        return pointers[index];
    }
    index -= per_indirect;
    if (inode->i_block[13] == 0 || index >= per_indirect * per_indirect) return 0;
    memcpy(pointers, disk_image + (uint64_t) inode->i_block[13] * BLOCK_SIZE, BLOCK_SIZE);
    uint32_t inner_block = pointers[index / per_indirect];
    if (inner_block == 0) return 0;
    memcpy(pointers, disk_image + (uint64_t) inner_block * BLOCK_SIZE, BLOCK_SIZE);
    return pointers[index % per_indirect];
}

static bool on_disk_blocks_match(uint32_t inode_number, const uint8_t *expected,
                                 uint32_t size, bool *crosses_group) {
    struct EXT2Inode inode;
    if (!read_inode_for_test(inode_number, &inode) || inode.i_size != size) return false;
    uint32_t seen_groups = 0;
    uint32_t count = size / BLOCK_SIZE + (size % BLOCK_SIZE != 0);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t physical = test_inode_data_block(&inode, i);
        if (physical == 0 || physical >= DISK_SPACE / BLOCK_SIZE) return false;
        uint32_t group = physical / BLOCKS_PER_GROUP;
        if (group >= GROUPS_COUNT) return false;
        seen_groups |= 1u << group;
        uint32_t offset = i * BLOCK_SIZE;
        uint32_t chunk = size - offset;
        if (chunk > BLOCK_SIZE) chunk = BLOCK_SIZE;
        if (memcmp(disk_image + (uint64_t) physical * BLOCK_SIZE,
                   expected + offset, chunk) != 0)
            return false;
    }
    if (crosses_group != NULL) {
        uint32_t group_count = 0;
        while (seen_groups != 0) {
            group_count += seen_groups & 1u;
            seen_groups >>= 1;
        }
        *crosses_group = group_count > 1;
    }
    return true;
}

void read_blocks(void *ptr, uint32_t lba, uint8_t count) {
    if ((uint64_t) (lba + count) * BLOCK_SIZE > DISK_SPACE) abort();
    memcpy(ptr, disk_image + (uint64_t) lba * BLOCK_SIZE, (size_t) count * BLOCK_SIZE);
}

void write_blocks(const void *ptr, uint32_t lba, uint8_t count) {
    if ((uint64_t) (lba + count) * BLOCK_SIZE > DISK_SPACE) abort();
    memcpy(disk_image + (uint64_t) lba * BLOCK_SIZE, ptr, (size_t) count * BLOCK_SIZE);
}

static void check(bool ok, const char *message) {
    checks++;
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static int8_t write_file(const char *name, const uint8_t *data, uint32_t size) {
    struct EXT2DriverRequest request = {
        .buf = (void *) data,
        .name = (char *) name,
        .name_len = (uint8_t) test_strlen(name),
        .parent_inode = 1,
        .buffer_size = size,
        .is_directory = false,
    };
    return write(&request);
}

static uint32_t find_child(uint32_t parent, const char *name);

static void check_round_trip(const char *name, uint32_t size) {
    uint8_t *expected = (uint8_t *) malloc(size);
    uint8_t *actual = (uint8_t *) malloc(size);
    if (expected == NULL || actual == NULL) abort();
    for (uint32_t i = 0; i < size; i++)
        expected[i] = (uint8_t) ((i * 37u + (i >> 3) + 11u) & 0xFFu);

    check(write_file(name, expected, size) == 0, "write file");
    struct EXT2DriverRequest request = {
        .buf = actual,
        .name = (char *) name,
        .name_len = (uint8_t) test_strlen(name),
        .parent_inode = 1,
        .buffer_size = size,
        .is_directory = false,
    };
    uint32_t actual_size = 0;
    check(get_file_size(request, &actual_size) == 0 && actual_size == size,
          "reported file size");
    check(read(request) == 0, "read file");
    check(memcmp(expected, actual, size) == 0, "byte-for-byte round trip");
    bool crosses_group = false;
    check(on_disk_blocks_match(find_child(1, name), expected, size, &crosses_group),
          "on-disk data blocks preserve logical byte order");
    if (size > BLOCKS_PER_GROUP * BLOCK_SIZE)
        check(crosses_group, "large file data spans more than one block group");
    free(actual);
    free(expected);
}

static uint32_t find_child(uint32_t parent, const char *name) {
    uint8_t directory[BLOCK_SIZE];
    struct EXT2DriverRequest request = {
        .buf = directory,
        .name = ".",
        .name_len = 1,
        .parent_inode = parent,
        .buffer_size = sizeof(directory),
        .is_directory = true,
    };
    if (read_directory(&request) != 0) return 0;
    for (uint32_t offset = 0; offset < sizeof(directory);) {
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *) (directory + offset);
        if (entry->rec_len < sizeof(*entry) || offset + entry->rec_len > sizeof(directory)) return 0;
        char *entry_name = get_entry_name(entry);
        if (entry->inode != 0 && entry->name_len == test_strlen(name) &&
            memcmp(entry_name, name, entry->name_len) == 0)
            return entry->inode;
        offset += entry->rec_len;
    }
    return 0;
}

static uint8_t find_child_type(uint32_t parent, const char *name) {
    uint8_t directory[BLOCK_SIZE];
    struct EXT2DriverRequest request = {
        .buf = directory,
        .name = ".",
        .name_len = 1,
        .parent_inode = parent,
        .buffer_size = sizeof(directory),
        .is_directory = true,
    };
    if (read_directory(&request) != 0) return EXT2_FT_UNKNOWN;
    for (uint32_t offset = 0; offset < sizeof(directory);) {
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *) (directory + offset);
        if (entry->rec_len < sizeof(*entry) || offset + entry->rec_len > sizeof(directory))
            return EXT2_FT_UNKNOWN;
        if (entry->inode != 0 && entry->name_len == test_strlen(name) &&
            memcmp(get_entry_name(entry), name, entry->name_len) == 0)
            return entry->file_type;
        offset += entry->rec_len;
    }
    return EXT2_FT_UNKNOWN;
}

int main(void) {
    memset(disk_image, 0, sizeof(disk_image));
    initialize_filesystem_ext2();
    check(!is_empty_storage(), "filesystem initialized on blank image");
    uint32_t root_size = 0;
    check(get_directory_size(1, &root_size) == 0 && root_size == BLOCK_SIZE,
          "directory size is reported for complete directory reads");

    /* Includes the assignment's byte 0 and byte 2047 boundary checks. */
    check_round_trip("boundary", 4097);
    check_round_trip("direct-single-double", (12u + BLOCK_SIZE / 4u + 3u) * BLOCK_SIZE);
    check_round_trip("cross-block-group", (BLOCKS_PER_GROUP + 4u) * BLOCK_SIZE);
    check_round_trip("shell-sized", 972020u);

    uint8_t small[3] = {0x00, 0x7F, 0xFF};
    check(write_file("duplicate", small, sizeof(small)) == 0, "first create succeeds");
    check(write_file("duplicate", small, sizeof(small)) == 1, "duplicate create is rejected");
    check(write_file("rename-me", small, sizeof(small)) == 0,
          "rename fixture creation succeeds");
    struct EXT2MoveRequest rename_file = {
        .source_parent_inode = 1,
        .source_name = "rename-me",
        .source_name_len = 9,
        .destination_parent_inode = 1,
        .destination_name = "renamed",
        .destination_name_len = 7,
    };
    check(move_entry(&rename_file) == 0, "file can be renamed in the same directory");
    check(find_child(1, "rename-me") == 0 && find_child(1, "renamed") != 0,
          "rename leaves only the new name");
    struct EXT2MoveRequest move_to_existing = rename_file;
    move_to_existing.source_name = "renamed";
    move_to_existing.source_name_len = 7;
    move_to_existing.destination_name = "duplicate";
    move_to_existing.destination_name_len = 9;
    check(move_entry(&move_to_existing) == 2, "move refuses to overwrite an existing entry");
    struct EXT2MoveRequest move_missing = rename_file;
    move_missing.source_name = "absent";
    move_missing.source_name_len = 6;
    check(move_entry(&move_missing) == 1, "move reports a missing source");
    struct EXT2MoveRequest move_bad_parent = rename_file;
    move_bad_parent.destination_parent_inode = 0;
    check(move_entry(&move_bad_parent) == 3, "move rejects an invalid destination parent");
    struct EXT2DriverRequest delete_renamed = {
        .buf = NULL,
        .name = "renamed",
        .name_len = 7,
        .parent_inode = 1,
        .buffer_size = 0,
        .is_directory = false,
    };
    check(delete(delete_renamed) == 0, "renamed file can be removed");
    check(write_file("../escape", small, sizeof(small)) == -1,
          "path separators are rejected in an inode name");
    check(write_file(".", small, sizeof(small)) == -1,
          "special directory names cannot be created");

    check(write_file("empty", NULL, 0) == 0, "zero-byte regular file creation succeeds");
    check(find_child_type(1, "empty") == EXT2_FT_REG_FILE,
          "zero-byte file remains a regular file, not a directory");
    struct EXT2DriverRequest empty = {
        .buf = NULL,
        .name = "empty",
        .name_len = 5,
        .parent_inode = 1,
        .buffer_size = 0,
        .is_directory = false,
    };
    uint32_t empty_size = 99;
    check(get_file_size(empty, &empty_size) == 0 && empty_size == 0,
          "zero-byte file reports an empty size");
    check(read(empty) == 0, "zero-byte file can be read with an empty buffer");
    struct EXT2DriverRequest bad_buffer = empty;
    bad_buffer.name = "duplicate";
    bad_buffer.name_len = 9;
    bad_buffer.buffer_size = 1;
    check(write_file("null-buffer", NULL, 1) == -1,
          "non-empty file creation rejects a null source buffer");
    check(read(bad_buffer) == -1, "non-empty file rejects a null destination buffer");

    struct EXT2DriverRequest too_small = {
        .buf = small,
        .name = "boundary",
        .name_len = 8,
        .parent_inode = 1,
        .buffer_size = 1,
        .is_directory = false,
    };
    check(read(too_small) == 2, "undersized destination buffer is rejected");
    struct EXT2DriverRequest directory_as_file = too_small;
    directory_as_file.name = ".";
    directory_as_file.name_len = 1;
    check(read(directory_as_file) == 1, "read rejects a directory as a file");
    check(read_directory(&directory_as_file) == -1,
          "read_directory rejects an undersized destination buffer");
    uint8_t root_contents[BLOCK_SIZE];
    directory_as_file.buf = root_contents;
    directory_as_file.buffer_size = sizeof(root_contents);
    check(read_directory(&directory_as_file) == 0,
          "read_directory accepts a directory with a complete buffer");
    check(read_directory(&too_small) == 1, "read_directory rejects a regular file");
    struct EXT2DriverRequest missing = too_small;
    missing.name = "does-not-exist";
    missing.name_len = 14;
    check(read(missing) == 3, "read reports a missing file");
    check(read_directory(&missing) == 2, "read_directory reports a missing directory");
    struct EXT2DriverRequest invalid_parent = too_small;
    invalid_parent.parent_inode = 0;
    check(read(invalid_parent) == 4, "read rejects an invalid parent inode");
    check(read_directory(&invalid_parent) == 3,
          "read_directory rejects an invalid parent inode");

    struct EXT2DriverRequest deletion = {
        .buf = NULL,
        .name = "boundary",
        .name_len = 8,
        .parent_inode = 1,
        .buffer_size = 0,
        .is_directory = false,
    };
    check(delete(deletion) == 0, "delete succeeds");
    check(get_file_size(too_small, &(uint32_t){0}) == 3, "deleted file is absent");
    check(delete(deletion) == 1, "deleting missing file is rejected");
    struct EXT2DriverRequest delete_empty = deletion;
    delete_empty.name = "empty";
    delete_empty.name_len = 5;
    check(delete(delete_empty) == 0, "zero-byte file can be deleted");
    struct EXT2DriverRequest remove_root = {
        .buf = NULL,
        .name = ".",
        .name_len = 1,
        .parent_inode = 1,
        .buffer_size = 0,
        .is_directory = true,
    };
    check(delete(remove_root) == 2, "root directory cannot be deleted");

    struct EXT2DriverRequest mkdir_request = {
        .buf = NULL,
        .name = "folder",
        .name_len = 6,
        .parent_inode = 1,
        .buffer_size = 0,
        .is_directory = true,
    };
    check(write(&mkdir_request) == 0, "directory creation succeeds");
    uint32_t folder_inode = find_child(1, "folder");
    check(folder_inode != 0, "new directory is linked from parent");
    check(write_file("nested", small, sizeof(small)) == 0,
          "root remains writable after directory creation");
    struct EXT2DriverRequest nested = {
        .buf = (void *) small,
        .name = "child",
        .name_len = 5,
        .parent_inode = folder_inode,
        .buffer_size = sizeof(small),
        .is_directory = false,
    };
    check(write(&nested) == 0, "nested file creation succeeds");
    struct EXT2DriverRequest create_destination = mkdir_request;
    create_destination.name = "destination";
    create_destination.name_len = 11;
    check(write(&create_destination) == 0, "move destination directory creation succeeds");
    uint32_t destination_inode = find_child(1, "destination");
    check(destination_inode != 0, "move destination is linked from root");

    struct EXT2MoveRequest move_directory = {
        .source_parent_inode = 1,
        .source_name = "folder",
        .source_name_len = 6,
        .destination_parent_inode = destination_inode,
        .destination_name = "moved",
        .destination_name_len = 5,
    };
    check(move_entry(&move_directory) == 0, "directory can move across parents");
    check(find_child(destination_inode, "moved") == folder_inode,
          "moved directory keeps its inode and contents");
    check(find_child(folder_inode, "..") == destination_inode,
          "moved directory parent entry is updated");

    struct EXT2MoveRequest move_into_descendant = {
        .source_parent_inode = 1,
        .source_name = "destination",
        .source_name_len = 11,
        .destination_parent_inode = folder_inode,
        .destination_name = "cycle",
        .destination_name_len = 5,
    };
    check(move_entry(&move_into_descendant) == 4,
          "directory cannot be moved inside its own descendant");

    check(write_file("move-me", small, sizeof(small)) == 0,
          "file move fixture creation succeeds");
    struct EXT2MoveRequest move_file = {
        .source_parent_inode = 1,
        .source_name = "move-me",
        .source_name_len = 7,
        .destination_parent_inode = destination_inode,
        .destination_name = "moved-file",
        .destination_name_len = 10,
    };
    check(move_entry(&move_file) == 0, "file can move across parents");
    check(find_child(1, "move-me") == 0 && find_child(destination_inode, "moved-file") != 0,
          "file move relinks exactly one directory entry");

    struct EXT2DriverRequest remove_folder = {
        .buf = NULL,
        .name = "moved",
        .name_len = 5,
        .parent_inode = destination_inode,
        .buffer_size = 0,
        .is_directory = true,
    };
    check(delete(remove_folder) == 2, "non-empty directory cannot be deleted");
    struct EXT2DriverRequest remove_child = {
        .buf = NULL,
        .name = "child",
        .name_len = 5,
        .parent_inode = folder_inode,
        .buffer_size = 0,
        .is_directory = false,
    };
    check(delete(remove_child) == 0, "nested file deletion succeeds");
    check(delete(remove_folder) == 0, "empty directory deletion succeeds");
    struct EXT2DriverRequest delete_moved_file = deletion;
    delete_moved_file.name = "moved-file";
    delete_moved_file.name_len = 10;
    delete_moved_file.parent_inode = destination_inode;
    check(delete(delete_moved_file) == 0, "moved file remains deletable in its new parent");
    struct EXT2DriverRequest delete_destination = deletion;
    delete_destination.name = "destination";
    delete_destination.name_len = 11;
    check(delete(delete_destination) == 0, "empty move destination can be deleted");

    initialize_filesystem_ext2();
    uint8_t after_reinit[sizeof(small)];
    struct EXT2DriverRequest persisted = {
        .buf = after_reinit,
        .name = "duplicate",
        .name_len = 9,
        .parent_inode = 1,
        .buffer_size = sizeof(after_reinit),
        .is_directory = false,
    };
    check(read(persisted) == 0 && memcmp(after_reinit, small, sizeof(small)) == 0,
          "data persists after filesystem reinitialization");

    struct EXT2Superblock *super = (struct EXT2Superblock *) (disk_image + BLOCK_SIZE);
    uint32_t free_before = super->s_free_blocks_count;
    uint32_t inodes_before = super->s_free_inodes_count;
    uint8_t *oversized = (uint8_t *) calloc(1, DISK_SPACE);
    if (oversized == NULL) abort();
    check(write_file("out-of-space", oversized, DISK_SPACE) != 0,
          "out-of-space write fails cleanly");
    check(super->s_free_blocks_count == free_before,
          "failed write rolls back allocated blocks");
    check(super->s_free_inodes_count == inodes_before,
          "failed write rolls back allocated inode");
    check(write_file("after-failure", small, sizeof(small)) == 0,
          "filesystem remains writable after allocation failure");
    free(oversized);

    printf("EXT2 host tests passed (%u assertions)\n", checks);
    return 0;
}
