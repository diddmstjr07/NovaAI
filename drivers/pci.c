#include "pci.h"
#include "io.h"

static uint32_t config_address(PciDevice device, uint8_t offset) {
    return 0x80000000U | ((uint32_t)device.bus << 16) |
           ((uint32_t)device.slot << 11) | ((uint32_t)device.function << 8) |
           (offset & 0xFC);
}

uint32_t pci_read32(PciDevice device, uint8_t offset) {
    outl(0xCF8, config_address(device, offset));
    return inl(0xCFC);
}

void pci_write32(PciDevice device, uint8_t offset, uint32_t value) {
    outl(0xCF8, config_address(device, offset));
    outl(0xCFC, value);
}

bool pci_find(uint16_t vendor, uint16_t device_id, PciDevice *result) {
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            PciDevice device = {(uint8_t)bus, slot, 0, 0, 0};
            uint32_t identity = pci_read32(device, 0);
            if ((uint16_t)identity == 0xFFFF) continue;
            uint8_t functions = (pci_read32(device, 0x0C) & 0x00800000) ? 8 : 1;
            for (uint8_t function = 0; function < functions; ++function) {
                device.function = function;
                identity = pci_read32(device, 0);
                if ((uint16_t)identity == vendor && (uint16_t)(identity >> 16) == device_id) {
                    device.vendor = vendor;
                    device.device = device_id;
                    if (result) *result = device;
                    return true;
                }
            }
        }
    }
    return false;
}

void pci_enable_memory_bus_master(PciDevice device) {
    uint32_t command = pci_read32(device, 0x04);
    command |= 0x00000006;
    pci_write32(device, 0x04, command);
}
