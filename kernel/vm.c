#include "vm.h"
#include "heap.h"
#include "runtime.h"

#define BOOT_PML4        ((const uint64_t *)0x70000UL)
#define BOOT_DIRECTORIES ((const uint64_t *)0x72000UL)
#define PAGE_PRESENT 0x001UL
#define PAGE_WRITE   0x002UL
#define PAGE_USER    0x004UL
#define PAGE_HUGE    0x080UL
#define PAGE_NX      (1UL << 63)
#define PAGE_ADDRESS 0x000FFFFFFFFFF000UL

static uintptr_t align_down(uintptr_t value) {
    return value & ~(uintptr_t)(VM_PAGE_SIZE - 1);
}

static uintptr_t align_up(uintptr_t value) {
    return (value + VM_PAGE_SIZE - 1) & ~(uintptr_t)(VM_PAGE_SIZE - 1);
}

static bool canonical_user_address(uintptr_t address) {
    return address < VM_CANONICAL_LIMIT &&
           ((address >= VM_USER_BASE && address < VM_USER_MMAP_LIMIT) ||
            (address >= VM_USER_STACK_BASE && address < VM_USER_STACK_TOP));
}

static void *aligned_heap_alloc(size_t size, size_t alignment, void **allocation) {
    if (!alignment || (alignment & (alignment - 1))) return NULL;
    void *raw = heap_alloc(size + alignment - 1);
    if (!raw) return NULL;
    uintptr_t aligned = ((uintptr_t)raw + alignment - 1) & ~(uintptr_t)(alignment - 1);
    *allocation = raw;
    return (void *)aligned;
}

VmFrame *vm_frame_create(void) {
    VmFrame *frame = heap_alloc(sizeof(*frame));
    if (!frame) return NULL;
    memset(frame, 0, sizeof(*frame));
    frame->data = aligned_heap_alloc(VM_PAGE_SIZE, VM_PAGE_SIZE, &frame->allocation);
    if (!frame->data) {
        heap_free(frame);
        return NULL;
    }
    memset(frame->data, 0, VM_PAGE_SIZE);
    frame->references = 1;
    return frame;
}

void vm_frame_retain(VmFrame *frame) {
    if (frame) ++frame->references;
}

void vm_frame_release(VmFrame *frame) {
    if (!frame || !frame->references || --frame->references) return;
    heap_free(frame->allocation);
    heap_free(frame);
}

static void initialize_root(VmSpace *space) {
    memset(space->pml4, 0, 6 * VM_PAGE_SIZE);
    memcpy(space->pml4, BOOT_PML4, VM_PAGE_SIZE);
    memcpy(space->bootstrap_directories, BOOT_DIRECTORIES, 4 * VM_PAGE_SIZE);
    space->pml4[0] = (uintptr_t)space->bootstrap_pdpt |
                     PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    for (int index = 0; index < 4; ++index) {
        space->bootstrap_pdpt[index] =
            (uintptr_t)(space->bootstrap_directories + index * 512) |
            PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }
}

bool vm_space_create(VmSpace *space) {
    if (!space) return false;
    memset(space, 0, sizeof(*space));
    uint8_t *tables = aligned_heap_alloc(6 * VM_PAGE_SIZE, VM_PAGE_SIZE,
                                         &space->root_allocation);
    if (!tables) return false;
    space->pml4 = (uint64_t *)tables;
    space->bootstrap_pdpt = (uint64_t *)(tables + VM_PAGE_SIZE);
    space->bootstrap_directories = (uint64_t *)(tables + 2 * VM_PAGE_SIZE);
    initialize_root(space);
    return true;
}

static uint64_t *allocate_table(VmSpace *space) {
    if (space->table_count >= VM_MAX_TABLES) return NULL;
    void *allocation = NULL;
    uint64_t *table = aligned_heap_alloc(VM_PAGE_SIZE, VM_PAGE_SIZE, &allocation);
    if (!table) return NULL;
    memset(table, 0, VM_PAGE_SIZE);
    space->allocated_tables[space->table_count] = table;
    space->table_allocations[space->table_count] = allocation;
    ++space->table_count;
    return table;
}

static uint64_t *next_table(VmSpace *space, uint64_t *entry, bool create) {
    if (*entry & PAGE_PRESENT) {
        if (*entry & PAGE_HUGE) return NULL;
        return (uint64_t *)(uintptr_t)(*entry & PAGE_ADDRESS);
    }
    if (!create) return NULL;
    uint64_t *table = allocate_table(space);
    if (!table) return NULL;
    *entry = (uintptr_t)table | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    return table;
}

static uint64_t *walk_page_entry(VmSpace *space, uintptr_t address, bool create) {
    if (!space || !space->pml4 || !canonical_user_address(address)) return NULL;
    uint64_t *pdpt = next_table(space, &space->pml4[(address >> 39) & 0x1FF], create);
    if (!pdpt) return NULL;
    uint64_t *directory = next_table(space, &pdpt[(address >> 30) & 0x1FF], create);
    if (!directory) return NULL;
    uint64_t *directory_entry = &directory[(address >> 21) & 0x1FF];
    if ((*directory_entry & PAGE_HUGE) && create) {
        /* User mappings are deliberately placed outside live low kernel RAM,
           so replacing the inherited identity leaf is safe. */
        *directory_entry = 0;
    }
    uint64_t *table = next_table(space, directory_entry, create);
    if (!table) return NULL;
    return &table[(address >> 12) & 0x1FF];
}

static VmPage *find_page(VmSpace *space, uintptr_t address) {
    uintptr_t page_address = align_down(address);
    uint32_t slot = (uint32_t)(((page_address >> 12) * 2654435761UL) &
                               (VM_PAGE_HASH_SIZE - 1));
    for (int probe = 0; probe < VM_PAGE_HASH_SIZE; ++probe) {
        int32_t value = space->page_lookup[slot];
        if (!value) return NULL;
        if (value > 0) {
            VmPage *page = &space->pages[value - 1];
            if (page->used && page->virtual_address == page_address) return page;
        }
        slot = (slot + 1) & (VM_PAGE_HASH_SIZE - 1);
    }
    return NULL;
}

static const VmPage *find_page_const(const VmSpace *space, uintptr_t address) {
    return find_page((VmSpace *)space, address);
}

static VmPage *reserve_page_record(VmSpace *space) {
    for (int offset = 0; offset < VM_MAX_PAGES; ++offset) {
        int index = (space->free_page_hint + offset) % VM_MAX_PAGES;
        if (!space->pages[index].used) {
            space->free_page_hint = (index + 1) % VM_MAX_PAGES;
            return &space->pages[index];
        }
    }
    return NULL;
}

static bool insert_page_lookup(VmSpace *space, VmPage *page) {
    int index = (int)(page - space->pages);
    uint32_t slot = (uint32_t)(((page->virtual_address >> 12) * 2654435761UL) &
                               (VM_PAGE_HASH_SIZE - 1));
    int tombstone = -1;
    for (int probe = 0; probe < VM_PAGE_HASH_SIZE; ++probe) {
        int32_t value = space->page_lookup[slot];
        if (value == -1 && tombstone < 0) tombstone = (int)slot;
        if (!value) {
            if (tombstone >= 0) slot = (uint32_t)tombstone;
            space->page_lookup[slot] = index + 1;
            return true;
        }
        slot = (slot + 1) & (VM_PAGE_HASH_SIZE - 1);
    }
    return false;
}

static void remove_page_lookup(VmSpace *space, const VmPage *page) {
    uint32_t slot = (uint32_t)(((page->virtual_address >> 12) * 2654435761UL) &
                               (VM_PAGE_HASH_SIZE - 1));
    for (int probe = 0; probe < VM_PAGE_HASH_SIZE; ++probe) {
        int32_t value = space->page_lookup[slot];
        if (!value) return;
        if (value > 0 && &space->pages[value - 1] == page) {
            space->page_lookup[slot] = -1;
            int index = (int)(page - space->pages);
            if (index < space->free_page_hint) space->free_page_hint = index;
            return;
        }
        slot = (slot + 1) & (VM_PAGE_HASH_SIZE - 1);
    }
}

static uint64_t page_flags(const VmPage *page) {
    uint64_t flags = PAGE_PRESENT | PAGE_USER;
    if ((page->protection & VM_PROT_WRITE) && !page->copy_on_write) flags |= PAGE_WRITE;
    if (!(page->protection & VM_PROT_EXEC)) flags |= PAGE_NX;
    return flags;
}

static bool install_page(VmSpace *space, uintptr_t address, VmFrame *frame,
                         uint8_t protection, bool copy_on_write, bool shared) {
    VmPage *record = reserve_page_record(space);
    uint64_t *entry = walk_page_entry(space, address, true);
    if (!record || !entry) return false;
    memset(record, 0, sizeof(*record));
    record->virtual_address = address;
    record->frame = frame;
    record->protection = protection;
    record->copy_on_write = copy_on_write;
    record->shared = shared;
    record->used = true;
    if (!insert_page_lookup(space, record)) {
        memset(record, 0, sizeof(*record));
        return false;
    }
    *entry = (uintptr_t)frame->data | page_flags(record);
    __asm__ volatile ("invlpg (%0)" : : "r"((void *)address) : "memory");
    return true;
}

void vm_space_reset_user(VmSpace *space) {
    if (!space || !space->pml4) return;
    for (int index = 0; index < VM_MAX_PAGES; ++index) {
        if (!space->pages[index].used) continue;
        vm_frame_release(space->pages[index].frame);
        memset(&space->pages[index], 0, sizeof(space->pages[index]));
    }
    memset(space->page_lookup, 0, sizeof(space->page_lookup));
    space->free_page_hint = 0;
    for (int index = 0; index < space->table_count; ++index) {
        heap_free(space->table_allocations[index]);
        space->allocated_tables[index] = NULL;
        space->table_allocations[index] = NULL;
    }
    space->table_count = 0;
    initialize_root(space);
    __asm__ volatile ("mov %%cr3, %%rax; mov %%rax, %%cr3" : : : "rax", "memory");
}

void vm_space_destroy(VmSpace *space) {
    if (!space) return;
    vm_space_reset_user(space);
    if (space->root_allocation) heap_free(space->root_allocation);
    memset(space, 0, sizeof(*space));
}

bool vm_map_page(VmSpace *space, uintptr_t virtual_address, uint8_t protection) {
    if (!space || !space->pml4 || (virtual_address & (VM_PAGE_SIZE - 1)) ||
        !canonical_user_address(virtual_address)) return false;
    VmPage *existing = find_page(space, virtual_address);
    if (existing) return vm_protect(space, virtual_address, VM_PAGE_SIZE, protection);
    VmFrame *frame = vm_frame_create();
    if (!frame) return false;
    if (!install_page(space, virtual_address, frame, protection, false, false)) {
        vm_frame_release(frame);
        return false;
    }
    return true;
}

bool vm_map_shared_frame(VmSpace *space, uintptr_t virtual_address,
                         VmFrame *frame, uint8_t protection) {
    if (!space || !space->pml4 || !frame ||
        (virtual_address & (VM_PAGE_SIZE - 1)) ||
        !canonical_user_address(virtual_address) ||
        find_page(space, virtual_address)) return false;
    vm_frame_retain(frame);
    if (!install_page(space, virtual_address, frame, protection, false, true)) {
        vm_frame_release(frame);
        return false;
    }
    return true;
}

bool vm_unmap_page(VmSpace *space, uintptr_t virtual_address) {
    if (!space) return false;
    virtual_address = align_down(virtual_address);
    VmPage *page = find_page(space, virtual_address);
    if (!page) return false;
    uint64_t *entry = walk_page_entry(space, virtual_address, false);
    if (entry) *entry = 0;
    remove_page_lookup(space, page);
    vm_frame_release(page->frame);
    memset(page, 0, sizeof(*page));
    __asm__ volatile ("invlpg (%0)" : : "r"((void *)virtual_address) : "memory");
    return true;
}

bool vm_protect(VmSpace *space, uintptr_t address, size_t size, uint8_t protection) {
    if (!space || !size || address + size < address) return false;
    uintptr_t first = align_down(address);
    uintptr_t end = align_up(address + size);
    for (uintptr_t page_address = first; page_address < end; page_address += VM_PAGE_SIZE) {
        if (!find_page(space, page_address)) return false;
    }
    for (uintptr_t page_address = first; page_address < end; page_address += VM_PAGE_SIZE) {
        VmPage *page = find_page(space, page_address);
        page->protection = protection;
        page->copy_on_write = !page->shared && (protection & VM_PROT_WRITE) &&
                              page->frame->references > 1;
        uint64_t *entry = walk_page_entry(space, page_address, false);
        if (!entry) return false;
        *entry = (uintptr_t)page->frame->data | page_flags(page);
        __asm__ volatile ("invlpg (%0)" : : "r"((void *)page_address) : "memory");
    }
    return true;
}

bool vm_range_mapped(const VmSpace *space, uintptr_t address, size_t size,
                     bool require_write) {
    if (!space || !size || address + size < address || address + size > VM_CANONICAL_LIMIT) {
        return false;
    }
    uintptr_t first = align_down(address);
    uintptr_t end = align_up(address + size);
    for (uintptr_t page_address = first; page_address < end; page_address += VM_PAGE_SIZE) {
        const VmPage *page = find_page_const(space, page_address);
        if (!page || (require_write && !(page->protection & VM_PROT_WRITE))) return false;
    }
    return true;
}

static bool make_private(VmSpace *space, VmPage *page) {
    if (!page->copy_on_write) return true;
    if (!(page->protection & VM_PROT_WRITE)) return false;
    if (page->frame->references > 1) {
        VmFrame *replacement = vm_frame_create();
        if (!replacement) return false;
        memcpy(replacement->data, page->frame->data, VM_PAGE_SIZE);
        vm_frame_release(page->frame);
        page->frame = replacement;
    }
    page->copy_on_write = false;
    uint64_t *entry = walk_page_entry(space, page->virtual_address, false);
    if (!entry) return false;
    *entry = (uintptr_t)page->frame->data | page_flags(page);
    __asm__ volatile ("invlpg (%0)" : : "r"((void *)page->virtual_address) : "memory");
    return true;
}

static uint8_t *translated_frame(const VmSpace *space, uintptr_t address) {
    const VmPage *page = find_page_const(space, address);
    if (!page) return NULL;
    return page->frame->data + (address & (VM_PAGE_SIZE - 1));
}

bool vm_copy_to_user(VmSpace *space, uintptr_t destination, const void *source, size_t size) {
    const uint8_t *input = source;
    while (size) {
        VmPage *page = find_page(space, destination);
        if (!page || !(page->protection & VM_PROT_WRITE) || !make_private(space, page)) return false;
        uint8_t *output = page->frame->data + (destination & (VM_PAGE_SIZE - 1));
        size_t chunk = VM_PAGE_SIZE - (destination & (VM_PAGE_SIZE - 1));
        if (chunk > size) chunk = size;
        memcpy(output, input, chunk);
        input += chunk;
        destination += chunk;
        size -= chunk;
    }
    return true;
}

bool vm_copy_from_user(const VmSpace *space, void *destination, uintptr_t source, size_t size) {
    uint8_t *output = destination;
    while (size) {
        uint8_t *input = translated_frame(space, source);
        if (!input) return false;
        size_t chunk = VM_PAGE_SIZE - (source & (VM_PAGE_SIZE - 1));
        if (chunk > size) chunk = size;
        memcpy(output, input, chunk);
        output += chunk;
        source += chunk;
        size -= chunk;
    }
    return true;
}

uintptr_t vm_find_free_range(const VmSpace *space, uintptr_t hint, size_t size) {
    if (!space || !size) return 0;
    size_t page_count = align_up(size) / VM_PAGE_SIZE;
    uintptr_t start = align_up(hint < VM_USER_MMAP_BASE ? VM_USER_MMAP_BASE : hint);
    for (uintptr_t candidate = start;
         candidate + page_count * VM_PAGE_SIZE <= VM_USER_MMAP_LIMIT;
         candidate += VM_PAGE_SIZE) {
        bool free = true;
        for (size_t offset = 0; offset < page_count; ++offset) {
            if (find_page_const(space, candidate + offset * VM_PAGE_SIZE)) {
                free = false;
                break;
            }
        }
        if (free) return candidate;
    }
    return 0;
}

bool vm_space_clone_cow(VmSpace *destination, VmSpace *source) {
    if (!destination || !source || !vm_space_create(destination)) return false;
    for (int index = 0; index < VM_MAX_PAGES; ++index) {
        VmPage *page = &source->pages[index];
        if (!page->used) continue;
        bool copy_on_write = !page->shared && (page->protection & VM_PROT_WRITE) != 0;
        vm_frame_retain(page->frame);
        if (!install_page(destination, page->virtual_address, page->frame,
                          page->protection, copy_on_write, page->shared)) {
            vm_frame_release(page->frame);
            vm_space_destroy(destination);
            return false;
        }
        if (copy_on_write) {
            page->copy_on_write = true;
            uint64_t *source_entry = walk_page_entry(source, page->virtual_address, false);
            if (!source_entry) {
                vm_space_destroy(destination);
                return false;
            }
            *source_entry = (uintptr_t)page->frame->data | page_flags(page);
            __asm__ volatile ("invlpg (%0)" : : "r"((void *)page->virtual_address) : "memory");
        }
    }
    return true;
}

int vm_handle_page_fault(VmSpace *space, uintptr_t address, uint64_t error_code) {
    if (!space || !(error_code & 0x04)) return VM_FAULT_UNHANDLED;
    if ((error_code & 0x07) == 0x07) {
        VmPage *page = find_page(space, address);
        return page && page->copy_on_write && make_private(space, page) ?
               VM_FAULT_COW : VM_FAULT_UNHANDLED;
    }
    if (!(error_code & 0x01) && address >= VM_USER_STACK_BASE &&
        address < VM_USER_STACK_TOP) {
        return vm_map_page(space, align_down(address),
                           VM_PROT_READ | VM_PROT_WRITE) ?
               VM_FAULT_DEMAND : VM_FAULT_UNHANDLED;
    }
    return VM_FAULT_UNHANDLED;
}

uint32_t vm_frame_references(const VmSpace *space, uintptr_t address) {
    const VmPage *page = find_page_const(space, address);
    return page ? page->frame->references : 0;
}

bool vm_mark_shared(VmSpace *space, uintptr_t address, size_t size) {
    if (!space || !size || address + size < address) return false;
    uintptr_t first = align_down(address);
    uintptr_t end = align_up(address + size);
    for (uintptr_t page_address = first; page_address < end; page_address += VM_PAGE_SIZE) {
        if (!find_page(space, page_address)) return false;
    }
    for (uintptr_t page_address = first; page_address < end; page_address += VM_PAGE_SIZE) {
        VmPage *page = find_page(space, page_address);
        page->shared = true;
        page->copy_on_write = false;
        uint64_t *entry = walk_page_entry(space, page_address, false);
        if (!entry) return false;
        *entry = (uintptr_t)page->frame->data | page_flags(page);
        __asm__ volatile ("invlpg (%0)" : : "r"((void *)page_address) : "memory");
    }
    return true;
}

void vm_space_activate(const VmSpace *space) {
    if (!space || !space->pml4) return;
    __asm__ volatile ("mov %0, %%cr3" : : "r"((uintptr_t)space->pml4) : "memory");
}

void vm_kernel_activate(void) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"((uintptr_t)0x70000UL) : "memory");
}
