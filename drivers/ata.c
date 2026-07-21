#include "ata.h"
#include "io.h"

#define ATA_DATA       0x1F0
#define ATA_ERROR      0x1F1
#define ATA_SECTOR_CNT 0x1F2
#define ATA_LBA_LOW    0x1F3
#define ATA_LBA_MID    0x1F4
#define ATA_LBA_HIGH   0x1F5
#define ATA_DRIVE      0x1F6
#define ATA_STATUS     0x1F7
#define ATA_COMMAND    0x1F7
#define ATA_ALT_STATUS 0x3F6

#define STATUS_ERROR 0x01
#define STATUS_DRQ   0x08
#define STATUS_FAULT 0x20
#define STATUS_BUSY  0x80

static uint32_t total_sectors;
static bool ready;

static void delay_400ns(void) {
    inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS);
}

static bool wait_not_busy(void) {
    for (uint32_t timeout = 0; timeout < 1000000; ++timeout) {
        uint8_t status = inb(ATA_STATUS);
        if (!(status & STATUS_BUSY)) return status != 0xFF;
    }
    return false;
}

static bool wait_drq(void) {
    for (uint32_t timeout = 0; timeout < 1000000; ++timeout) {
        uint8_t status = inb(ATA_STATUS);
        if (status & (STATUS_ERROR | STATUS_FAULT)) return false;
        if (!(status & STATUS_BUSY) && (status & STATUS_DRQ)) return true;
    }
    return false;
}

bool ata_init(void) {
    ready = false;
    total_sectors = 0;
    outb(ATA_DRIVE, 0xA0);
    delay_400ns();
    outb(ATA_SECTOR_CNT, 0);
    outb(ATA_LBA_LOW, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HIGH, 0);
    outb(ATA_COMMAND, 0xEC);
    uint8_t status = inb(ATA_STATUS);
    if (!status || status == 0xFF || !wait_not_busy()) return false;
    if (inb(ATA_LBA_MID) || inb(ATA_LBA_HIGH) || !wait_drq()) return false;

    uint16_t identify[256];
    for (int index = 0; index < 256; ++index) identify[index] = inw(ATA_DATA);
    total_sectors = (uint32_t)identify[60] | ((uint32_t)identify[61] << 16);
    ready = total_sectors > 0;
    return ready;
}

static bool select_lba(uint32_t lba, uint8_t command) {
    if (!ready || lba >= total_sectors || lba >= 0x10000000 || !wait_not_busy()) return false;
    outb(ATA_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    delay_400ns();
    outb(ATA_ERROR, 0);
    outb(ATA_SECTOR_CNT, 1);
    outb(ATA_LBA_LOW, (uint8_t)lba);
    outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_COMMAND, command);
    return wait_drq();
}

bool ata_read_sector(uint32_t lba, void *buffer) {
    if (!buffer || !select_lba(lba, 0x20)) return false;
    uint16_t *words = (uint16_t *)buffer;
    for (int index = 0; index < 256; ++index) words[index] = inw(ATA_DATA);
    delay_400ns();
    return true;
}

bool ata_write_sector(uint32_t lba, const void *buffer) {
    if (!buffer || !select_lba(lba, 0x30)) return false;
    const uint16_t *words = (const uint16_t *)buffer;
    for (int index = 0; index < 256; ++index) outw(ATA_DATA, words[index]);
    if (!wait_not_busy()) return false;
    outb(ATA_COMMAND, 0xE7);
    return wait_not_busy() && !(inb(ATA_STATUS) & (STATUS_ERROR | STATUS_FAULT));
}

uint32_t ata_sector_count(void) {
    return total_sectors;
}
