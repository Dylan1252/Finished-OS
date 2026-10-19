#include "header/driver/disk.h"
#include "header/cpu/portio.h"

/* ATA primary bus I/O ports */
#define ATA_DATA_PORT      0x1F0
#define ATA_SECTOR_COUNT   0x1F2
#define ATA_LBA_LOW        0x1F3
#define ATA_LBA_MID        0x1F4
#define ATA_LBA_HIGH       0x1F5
#define ATA_DRIVE_SELECT   0x1F6
#define ATA_STATUS_CMD     0x1F7

#define ATA_CMD_READ_PIO   0x20
#define ATA_CMD_WRITE_PIO  0x30

// Wait until the controller is no longer busy
static void ATA_busy_wait(void) {
    while (in(ATA_STATUS_CMD) & ATA_STATUS_BSY);
}

// Wait until the controller is ready to transfer data (DRQ set)
static void ATA_DRQ_wait(void) {
    while (!(in(ATA_STATUS_CMD) & ATA_STATUS_DRQ));
}

// Send LBA, sector count & command to the primary master drive
static void ATA_send_command(uint32_t lba, uint8_t block_count, uint8_t cmd) {
    ATA_busy_wait();
    out(ATA_DRIVE_SELECT, 0xE0 | ((lba >> 24) & 0xF));
    out(ATA_SECTOR_COUNT, block_count);
    out(ATA_LBA_LOW,  (uint8_t) lba);
    out(ATA_LBA_MID,  (uint8_t) (lba >> 8));
    out(ATA_LBA_HIGH, (uint8_t) (lba >> 16));
    out(ATA_STATUS_CMD, cmd);
}

void read_blocks(void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    ATA_send_command(logical_block_address, block_count, ATA_CMD_READ_PIO);

    uint16_t *target = (uint16_t *) ptr;
    for (uint32_t i = 0; i < block_count; i++) {
        ATA_busy_wait();
        ATA_DRQ_wait();
        for (uint32_t j = 0; j < HALF_BLOCK_SIZE; j++)
            target[j] = in16(ATA_DATA_PORT);
        target += HALF_BLOCK_SIZE;
    }
}

void write_blocks(const void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    ATA_send_command(logical_block_address, block_count, ATA_CMD_WRITE_PIO);

    const uint16_t *source = (const uint16_t *) ptr;
    for (uint32_t i = 0; i < block_count; i++) {
        ATA_busy_wait();
        ATA_DRQ_wait();
        for (uint32_t j = 0; j < HALF_BLOCK_SIZE; j++)
            out16(ATA_DATA_PORT, source[j]);
        source += HALF_BLOCK_SIZE;
    }
}
