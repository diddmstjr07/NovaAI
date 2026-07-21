#ifndef NOVA_PCI_H
#define NOVA_PCI_H

#include "types.h"

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint16_t vendor;
    uint16_t device;
} PciDevice;

uint32_t pci_read32(PciDevice device, uint8_t offset);
void pci_write32(PciDevice device, uint8_t offset, uint32_t value);
bool pci_find(uint16_t vendor, uint16_t device, PciDevice *result);
void pci_enable_memory_bus_master(PciDevice device);

#endif
