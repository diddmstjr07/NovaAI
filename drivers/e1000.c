#include "e1000.h"
#include "heap.h"
#include "pci.h"
#include "runtime.h"

#define INTEL_VENDOR 0x8086
#define E1000_DEVICE 0x100E

#define REG_CTRL   0x0000
#define REG_EERD   0x0014
#define REG_ICR    0x00C0
#define REG_IMC    0x00D8
#define REG_RCTL   0x0100
#define REG_TCTL   0x0400
#define REG_TIPG   0x0410
#define REG_RDBAL  0x2800
#define REG_RDBAH  0x2804
#define REG_RDLEN  0x2808
#define REG_RDH    0x2810
#define REG_RDT    0x2818
#define REG_TDBAL  0x3800
#define REG_TDBAH  0x3804
#define REG_TDLEN  0x3808
#define REG_TDH    0x3810
#define REG_TDT    0x3818
#define REG_RAL    0x5400
#define REG_RAH    0x5404
#define REG_MTA    0x5200

#define RX_COUNT 32
#define TX_COUNT 16

typedef struct __attribute__((packed)) {
    uint64_t address;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} RxDescriptor;

typedef struct __attribute__((packed)) {
    uint64_t address;
    uint16_t length;
    uint8_t checksum_offset;
    uint8_t command;
    uint8_t status;
    uint8_t checksum_start;
    uint16_t special;
} TxDescriptor;

static volatile uint8_t *mmio;
static RxDescriptor *rx_ring;
static TxDescriptor *tx_ring;
static uint8_t *rx_buffers[RX_COUNT];
static uint8_t *tx_buffers[TX_COUNT];
static uint32_t rx_index;
static uint32_t tx_index;
static uint8_t mac_address[6];
static bool available;

static uint32_t read_register(uint32_t offset) {
    return *(volatile uint32_t *)(mmio + offset);
}

static void write_register(uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(mmio + offset) = value;
    __asm__ volatile ("mfence" ::: "memory");
}

static bool read_eeprom(uint8_t address, uint16_t *value) {
    write_register(REG_EERD, 1U | ((uint32_t)address << 8));
    for (uint32_t timeout = 0; timeout < 100000; ++timeout) {
        uint32_t data = read_register(REG_EERD);
        if (data & 0x10) {
            *value = (uint16_t)(data >> 16);
            return true;
        }
    }
    return false;
}

static bool load_mac(void) {
    uint32_t low = read_register(REG_RAL);
    uint32_t high = read_register(REG_RAH);
    if (low || (high & 0xFFFF)) {
        mac_address[0] = (uint8_t)low;
        mac_address[1] = (uint8_t)(low >> 8);
        mac_address[2] = (uint8_t)(low >> 16);
        mac_address[3] = (uint8_t)(low >> 24);
        mac_address[4] = (uint8_t)high;
        mac_address[5] = (uint8_t)(high >> 8);
        return true;
    }
    for (int word = 0; word < 3; ++word) {
        uint16_t value;
        if (!read_eeprom((uint8_t)word, &value)) return false;
        mac_address[word * 2] = (uint8_t)value;
        mac_address[word * 2 + 1] = (uint8_t)(value >> 8);
    }
    return true;
}

static bool setup_receive(void) {
    rx_ring = heap_calloc(RX_COUNT, sizeof(RxDescriptor));
    if (!rx_ring) return false;
    for (int index = 0; index < RX_COUNT; ++index) {
        rx_buffers[index] = heap_alloc(E1000_FRAME_MAX);
        if (!rx_buffers[index]) return false;
        rx_ring[index].address = (uintptr_t)rx_buffers[index];
    }
    uintptr_t address = (uintptr_t)rx_ring;
    write_register(REG_RDBAL, (uint32_t)address);
    write_register(REG_RDBAH, (uint32_t)(address >> 32));
    write_register(REG_RDLEN, RX_COUNT * sizeof(RxDescriptor));
    write_register(REG_RDH, 0);
    write_register(REG_RDT, RX_COUNT - 1);
    rx_index = 0;
    write_register(REG_RCTL, (1U << 1) | (1U << 15) | (1U << 26));
    return true;
}

static bool setup_transmit(void) {
    tx_ring = heap_calloc(TX_COUNT, sizeof(TxDescriptor));
    if (!tx_ring) return false;
    for (int index = 0; index < TX_COUNT; ++index) {
        tx_buffers[index] = heap_alloc(E1000_FRAME_MAX);
        if (!tx_buffers[index]) return false;
        tx_ring[index].address = (uintptr_t)tx_buffers[index];
        tx_ring[index].status = 1;
    }
    uintptr_t address = (uintptr_t)tx_ring;
    write_register(REG_TDBAL, (uint32_t)address);
    write_register(REG_TDBAH, (uint32_t)(address >> 32));
    write_register(REG_TDLEN, TX_COUNT * sizeof(TxDescriptor));
    write_register(REG_TDH, 0);
    write_register(REG_TDT, 0);
    tx_index = 0;
    write_register(REG_TCTL, (1U << 1) | (1U << 3) | (0x0FU << 4) | (0x40U << 12));
    write_register(REG_TIPG, 10 | (8U << 10) | (6U << 20));
    return true;
}

bool e1000_init(void) {
    available = false;
    PciDevice device;
    if (!pci_find(INTEL_VENDOR, E1000_DEVICE, &device)) return false;
    uint32_t bar0 = pci_read32(device, 0x10);
    if ((bar0 & 1) || !(bar0 & ~0x0FU)) return false;
    mmio = (volatile uint8_t *)(uintptr_t)(bar0 & ~0x0FU);
    pci_enable_memory_bus_master(device);
    write_register(REG_IMC, 0xFFFFFFFF);
    (void)read_register(REG_ICR);
    write_register(REG_CTRL, read_register(REG_CTRL) | (1U << 26));
    for (uint32_t wait = 0; wait < 1000000; ++wait) {
        if (!(read_register(REG_CTRL) & (1U << 26))) break;
    }
    write_register(REG_IMC, 0xFFFFFFFF);
    (void)read_register(REG_ICR);
    if (!load_mac()) return false;
    uint32_t low = (uint32_t)mac_address[0] | ((uint32_t)mac_address[1] << 8) |
                   ((uint32_t)mac_address[2] << 16) | ((uint32_t)mac_address[3] << 24);
    uint32_t high = mac_address[4] | ((uint32_t)mac_address[5] << 8) | (1U << 31);
    write_register(REG_RAL, low);
    write_register(REG_RAH, high);
    for (int index = 0; index < 128; ++index) write_register(REG_MTA + index * 4, 0);
    if (!setup_receive() || !setup_transmit()) return false;
    available = true;
    return true;
}

bool e1000_send(const void *frame, uint16_t length) {
    if (!available || !frame || length < 14 || length > E1000_FRAME_MAX) return false;
    TxDescriptor *descriptor = &tx_ring[tx_index];
    for (uint32_t timeout = 0; !(descriptor->status & 1); ++timeout) {
        if (timeout > 1000000) return false;
    }
    memcpy(tx_buffers[tx_index], frame, length);
    descriptor->length = length;
    descriptor->checksum_offset = 0;
    descriptor->command = 0x0B;
    descriptor->status = 0;
    descriptor->checksum_start = 0;
    uint32_t next = (tx_index + 1) % TX_COUNT;
    write_register(REG_TDT, next);
    tx_index = next;
    return true;
}

int e1000_receive(void *frame, uint16_t capacity) {
    if (!available || !frame) return -1;
    RxDescriptor *descriptor = &rx_ring[rx_index];
    if (!(descriptor->status & 1)) return 0;
    uint16_t length = descriptor->length;
    if (length > capacity) length = capacity;
    memcpy(frame, rx_buffers[rx_index], length);
    descriptor->status = 0;
    uint32_t completed = rx_index;
    rx_index = (rx_index + 1) % RX_COUNT;
    write_register(REG_RDT, completed);
    return length;
}

const uint8_t *e1000_mac(void) {
    return mac_address;
}
