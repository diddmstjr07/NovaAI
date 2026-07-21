#ifndef NOVA_ATA_H
#define NOVA_ATA_H

#include "types.h"

#define ATA_SECTOR_SIZE 512

bool ata_init(void);
bool ata_read_sector(uint32_t lba, void *buffer);
bool ata_write_sector(uint32_t lba, const void *buffer);
uint32_t ata_sector_count(void);

#endif
