#ifndef NOVA_VM_H
#define NOVA_VM_H

#include "types.h"

#define VM_PAGE_SIZE        4096UL
#define VM_USER_BASE        0x0000000040000000UL
#define VM_USER_LOAD_LIMIT  0x0000000E00000000UL
#define VM_USER_MMAP_BASE   0x0000001000000000UL
#define VM_USER_MMAP_LIMIT  0x0000700000000000UL
#define VM_USER_STACK_TOP   0x00007FFFFFFFE000UL
#define VM_USER_STACK_SIZE  0x00100000UL
#define VM_USER_STACK_BASE  (VM_USER_STACK_TOP - VM_USER_STACK_SIZE)
#define VM_CANONICAL_LIMIT  0x0000800000000000UL
#define VM_MAX_PAGES        131072
#define VM_MAX_TABLES       1024
#define VM_PAGE_HASH_SIZE   262144

#define VM_PROT_READ  0x01U
#define VM_PROT_WRITE 0x02U
#define VM_PROT_EXEC  0x04U

#define VM_FAULT_UNHANDLED 0
#define VM_FAULT_COW       1
#define VM_FAULT_DEMAND    2

typedef struct VmFrame {
    uint8_t *data;
    void *allocation;
    uint32_t references;
} VmFrame;

typedef struct {
    uintptr_t virtual_address;
    VmFrame *frame;
    uint8_t protection;
    bool copy_on_write;
    bool shared;
    bool used;
} VmPage;

typedef struct {
    uint64_t *pml4;
    uint64_t *bootstrap_pdpt;
    uint64_t *bootstrap_directories;
    void *root_allocation;
    uint64_t *allocated_tables[VM_MAX_TABLES];
    void *table_allocations[VM_MAX_TABLES];
    int table_count;
    int free_page_hint;
    int32_t page_lookup[VM_PAGE_HASH_SIZE];
    VmPage pages[VM_MAX_PAGES];
} VmSpace;

bool vm_space_create(VmSpace *space);
void vm_space_destroy(VmSpace *space);
bool vm_space_clone_cow(VmSpace *destination, VmSpace *source);
void vm_space_reset_user(VmSpace *space);
void vm_space_activate(const VmSpace *space);
void vm_kernel_activate(void);

bool vm_map_page(VmSpace *space, uintptr_t virtual_address, uint8_t protection);
VmFrame *vm_frame_create(void);
void vm_frame_retain(VmFrame *frame);
void vm_frame_release(VmFrame *frame);
bool vm_map_shared_frame(VmSpace *space, uintptr_t virtual_address,
                         VmFrame *frame, uint8_t protection);
bool vm_unmap_page(VmSpace *space, uintptr_t virtual_address);
bool vm_protect(VmSpace *space, uintptr_t address, size_t size, uint8_t protection);
bool vm_range_mapped(const VmSpace *space, uintptr_t address, size_t size,
                     bool require_write);
bool vm_copy_to_user(VmSpace *space, uintptr_t destination, const void *source, size_t size);
bool vm_copy_from_user(const VmSpace *space, void *destination, uintptr_t source, size_t size);
uintptr_t vm_find_free_range(const VmSpace *space, uintptr_t hint, size_t size);
int vm_handle_page_fault(VmSpace *space, uintptr_t address, uint64_t error_code);
uint32_t vm_frame_references(const VmSpace *space, uintptr_t address);
bool vm_mark_shared(VmSpace *space, uintptr_t address, size_t size);

#endif
