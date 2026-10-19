#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "header/filesystem/ext2.h"

/*
 * Host-side inserter for the EXT2 - IF2130 Edition filesystem.
 * Usage: inserter [-f] <image> <parent_inode> <file>
 *   -f : replace file if it already exists
 */

uint8_t *image_storage;
uint8_t *file_buffer;
bool     is_replace = false;

void read_blocks(void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    for (int i = 0; i < block_count; i++) {
        memcpy(
            (uint8_t *) ptr + BLOCK_SIZE * i,
            image_storage + BLOCK_SIZE * (logical_block_address + i),
            BLOCK_SIZE
        );
    }
}

void write_blocks(const void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    for (int i = 0; i < block_count; i++) {
        memcpy(
            image_storage + BLOCK_SIZE * (logical_block_address + i),
            (uint8_t *) ptr + BLOCK_SIZE * i,
            BLOCK_SIZE
        );
    }
}

int main(int argc, char *argv[]) {
    int argi = 1;
    if (argc >= 2 && strcmp(argv[1], "-f") == 0) {
        is_replace = true;
        argi++;
    }

    if (argc - argi < 3) {
        fprintf(stderr, "Usage: %s [-f] <image> <parent_inode> <file>\n", argv[0]);
        return 1;
    }

    char *image_path  = argv[argi];
    char *parent_str  = argv[argi + 1];
    char *file_path   = argv[argi + 2];

    /* Load the disk image into memory */
    image_storage = (uint8_t *) malloc(DISK_SPACE);
    FILE *image = fopen(image_path, "rb");
    if (image == NULL) {
        fprintf(stderr, "%s: cannot open image %s\n", argv[0], image_path);
        return 1;
    }
    fread(image_storage, 1, DISK_SPACE, image);
    fclose(image);

    /* Read the file to insert */
    FILE *input = fopen(file_path, "rb");
    if (input == NULL) {
        fprintf(stderr, "%s: cannot open file %s\n", argv[0], file_path);
        return 1;
    }
    fseek(input, 0, SEEK_END);
    long file_size = ftell(input);
    fseek(input, 0, SEEK_SET);
    file_buffer = (uint8_t *) malloc((size_t) file_size);
    fread(file_buffer, 1, (size_t) file_size, input);
    fclose(input);

    /* Basename of the file becomes the EXT2 entry name */
    char *name = file_path;
    for (char *p = file_path; *p; p++)
        if (*p == '/') name = p + 1;

    initialize_filesystem_ext2();

    struct EXT2DriverRequest request = {
        .buf          = file_buffer,
        .name         = name,
        .name_len     = (uint8_t) strlen(name),
        .parent_inode = (uint32_t) atoi(parent_str),
        .buffer_size  = (uint32_t) file_size,
        .is_directory = false,
    };

    int8_t ret = write(&request);
    if (ret == 1 && is_replace) {
        struct EXT2DriverRequest del = request;
        del.buf = NULL;
        del.buffer_size = 0;
        delete(del);
        ret = write(&request);
    }

    if (ret != 0) {
        fprintf(stderr, "%s: failed to insert %s (error %d)\n", argv[0], file_path, ret);
        return 1;
    }

    /* Persist the modified image */
    FILE *out = fopen(image_path, "wb");
    if (out == NULL) {
        fprintf(stderr, "%s: cannot write image %s\n", argv[0], image_path);
        return 1;
    }
    fwrite(image_storage, 1, DISK_SPACE, out);
    fclose(out);

    printf("Inserted '%s' (%ld bytes) into parent inode %s\n", name, file_size, parent_str);
    return 0;
}
