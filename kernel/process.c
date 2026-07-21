#include "process.h"
#include "filesystem.h"
#include "heap.h"
#include "io.h"
#include "runtime.h"
#include "serial.h"
#include "vm.h"

#define MAX_PROCESSES 64
#define MAX_FILE_DESCRIPTORS 256
#define ELF_PT_LOAD 1
#define ELF_PT_DYNAMIC 2
#define ELF_PT_INTERP 3
#define ELF_PT_TLS 7
#define ELF_DT_NULL 0
#define ELF_DT_NEEDED 1
#define ELF_DT_PLTRELSZ 2
#define ELF_DT_HASH 4
#define ELF_DT_STRTAB 5
#define ELF_DT_SYMTAB 6
#define ELF_DT_RELA 7
#define ELF_DT_RELASZ 8
#define ELF_DT_RELAENT 9
#define ELF_DT_STRSZ 10
#define ELF_DT_SYMENT 11
#define ELF_DT_INIT 12
#define ELF_DT_FINI 13
#define ELF_DT_PLTREL 20
#define ELF_DT_JMPREL 23
#define ELF_DT_INIT_ARRAY 25
#define ELF_DT_FINI_ARRAY 26
#define ELF_DT_INIT_ARRAYSZ 27
#define ELF_DT_FINI_ARRAYSZ 28
#define ELF_DT_PREINIT_ARRAY 32
#define ELF_DT_PREINIT_ARRAYSZ 33
#define ELF_DT_GNU_HASH 0x6FFFFEF5
#define ELF_DT_VERSYM 0x6FFFFFF0
#define ELF_DT_VERDEF 0x6FFFFFFC
#define ELF_DT_VERDEFNUM 0x6FFFFFFD
#define ELF_DT_VERNEED 0x6FFFFFFE
#define ELF_DT_VERNEEDNUM 0x6FFFFFFF
#define ELF_R_X86_64_64 1
#define ELF_R_X86_64_GLOB_DAT 6
#define ELF_R_X86_64_JUMP_SLOT 7
#define ELF_R_X86_64_RELATIVE 8
#define ELF_R_X86_64_DTPMOD64 16
#define ELF_R_X86_64_DTPOFF64 17
#define ELF_R_X86_64_TPOFF64 18
#define ELF_R_X86_64_DTPOFF32 21
#define ELF_R_X86_64_TPOFF32 23
#define ELF_R_X86_64_TLSDESC 36
#define ELF_MAX_OBJECTS 128
#define ELF_MAX_NEEDED 64
#define ELF_MAX_SEGMENTS 16
#define ELF_MAX_SYMBOLS 4194304U
#define ELF_SYMBOL_NAME_MAX 512
#define ELF_INTERPRETER_BASE 0x0000000200000000UL
#define ELF_LIBRARY_BASE     0x0000000400000000UL
#define ELF_LIBRARY_STRIDE   0x0000000010000000UL
#define ELF_TLS_TCB_SIZE     64UL
#define ELF_TLS_DTV_ENTRIES  (ELF_MAX_OBJECTS + 1UL)
#define ELF_TLS_DTV_SIZE     (ELF_TLS_DTV_ENTRIES * sizeof(uintptr_t))
#define ELF_TLS_MAX_ALIGN    VM_PAGE_SIZE
#define ELF_TLS_MAX_SIZE     (16UL * 1024UL * 1024UL)
#define ELF_STARTUP_MAGIC    0x4E4F564153544152UL
#define ELF_STARTUP_MAX_FUNCTIONS 4096UL
#define NOVA_AUTO_TLS        ((uintptr_t)-1)

#define NOVA_SYS_WRITE 1
#define NOVA_SYS_YIELD 2
#define NOVA_SYS_EXIT 3

#define LINUX_SYS_READ 0
#define LINUX_SYS_WRITE 1
#define LINUX_SYS_CLOSE 3
#define LINUX_SYS_FSTAT 5
#define LINUX_SYS_POLL 7
#define LINUX_SYS_LSEEK 8
#define LINUX_SYS_IOCTL 16
#define LINUX_SYS_PREAD64 17
#define LINUX_SYS_PWRITE64 18
#define LINUX_SYS_READV 19
#define LINUX_SYS_WRITEV 20
#define LINUX_SYS_ACCESS 21
#define LINUX_SYS_DUP 32
#define LINUX_SYS_DUP2 33
#define LINUX_SYS_MMAP 9
#define LINUX_SYS_MPROTECT 10
#define LINUX_SYS_MUNMAP 11
#define LINUX_SYS_BRK 12
#define LINUX_SYS_RT_SIGACTION 13
#define LINUX_SYS_RT_SIGPROCMASK 14
#define LINUX_SYS_RT_SIGRETURN 15
#define LINUX_SYS_SCHED_YIELD 24
#define LINUX_SYS_NANOSLEEP 35
#define LINUX_SYS_GETPID 39
#define LINUX_SYS_CLONE 56
#define LINUX_SYS_FORK 57
#define LINUX_SYS_VFORK 58
#define LINUX_SYS_EXECVE 59
#define LINUX_SYS_EXIT 60
#define LINUX_SYS_WAIT4 61
#define LINUX_SYS_KILL 62
#define LINUX_SYS_UNAME 63
#define LINUX_SYS_GETRLIMIT 97
#define LINUX_SYS_GETRUSAGE 98
#define LINUX_SYS_GETUID 102
#define LINUX_SYS_GETGID 104
#define LINUX_SYS_GETEUID 107
#define LINUX_SYS_GETEGID 108
#define LINUX_SYS_GETPPID 110
#define LINUX_SYS_GETPGID 121
#define LINUX_SYS_GETSID 124
#define LINUX_SYS_SIGALTSTACK 131
#define LINUX_SYS_MKDIR 83
#define LINUX_SYS_UNLINK 87
#define LINUX_SYS_READLINK 89
#define LINUX_SYS_FCNTL 72
#define LINUX_SYS_FTRUNCATE 77
#define LINUX_SYS_GETCWD 79
#define LINUX_SYS_PRCTL 157
#define LINUX_SYS_ARCH_PRCTL 158
#define LINUX_SYS_GETTID 186
#define LINUX_SYS_SCHED_GETAFFINITY 204
#define LINUX_SYS_MADVISE 28
#define LINUX_SYS_FUTEX 202
#define LINUX_SYS_SET_TID_ADDRESS 218
#define LINUX_SYS_GETDENTS64 217
#define LINUX_SYS_EPOLL_WAIT 232
#define LINUX_SYS_EPOLL_CTL 233
#define LINUX_SYS_CLOCK_GETTIME 228
#define LINUX_SYS_EXIT_GROUP 231
#define LINUX_SYS_OPENAT 257
#define LINUX_SYS_MKDIRAT 258
#define LINUX_SYS_NEWFSTATAT 262
#define LINUX_SYS_UNLINKAT 263
#define LINUX_SYS_SYMLINKAT 266
#define LINUX_SYS_READLINKAT 267
#define LINUX_SYS_FACCESSAT 269
#define LINUX_SYS_PPOLL 271
#define LINUX_SYS_SET_ROBUST_LIST 273
#define LINUX_SYS_TIMERFD_CREATE 283
#define LINUX_SYS_TIMERFD_SETTIME 286
#define LINUX_SYS_TIMERFD_GETTIME 287
#define LINUX_SYS_EVENTFD2 290
#define LINUX_SYS_EPOLL_CREATE1 291
#define LINUX_SYS_PIPE2 293
#define LINUX_SYS_DUP3 292
#define LINUX_SYS_PRLIMIT64 302
#define LINUX_SYS_SECCOMP 317
#define LINUX_SYS_GETRANDOM 318
#define LINUX_SYS_MEMFD_CREATE 319
#define LINUX_SYS_SOCKETPAIR 53

#define LINUX_EPERM 1
#define LINUX_ENOENT 2
#define LINUX_ESRCH 3
#define LINUX_EINTR 4
#define LINUX_EIO 5
#define LINUX_EBADF 9
#define LINUX_ECHILD 10
#define LINUX_EAGAIN 11
#define LINUX_ENOMEM 12
#define LINUX_EACCES 13
#define LINUX_EFAULT 14
#define LINUX_EBUSY 16
#define LINUX_EEXIST 17
#define LINUX_ENOTDIR 20
#define LINUX_EISDIR 21
#define LINUX_EINVAL 22
#define LINUX_ENFILE 23
#define LINUX_ENOTTY 25
#define LINUX_ENOSYS 38
#define LINUX_ENOTEMPTY 39

#define LINUX_O_WRONLY 1
#define LINUX_O_RDWR 2
#define LINUX_O_CREAT 0100
#define LINUX_O_TRUNC 01000
#define LINUX_O_APPEND 02000
#define LINUX_O_DIRECTORY 00200000
#define LINUX_AT_REMOVEDIR 0x200
#define LINUX_WNOHANG 1
#define LINUX_MAP_PRIVATE 0x02
#define LINUX_MAP_SHARED 0x01
#define LINUX_MAP_FIXED 0x10
#define LINUX_MAP_ANONYMOUS 0x20
#define LINUX_FUTEX_WAIT 0
#define LINUX_FUTEX_WAKE 1
#define LINUX_CLONE_VM 0x00000100UL
#define LINUX_CLONE_SETTLS 0x00080000UL
#define LINUX_CLONE_PARENT_SETTID 0x00100000UL
#define LINUX_CLONE_CHILD_SETTID 0x01000000UL
#define LINUX_ARCH_SET_GS 0x1001
#define LINUX_ARCH_SET_FS 0x1002
#define LINUX_ARCH_GET_FS 0x1003
#define LINUX_ARCH_GET_GS 0x1004
#define LINUX_PR_SET_NO_NEW_PRIVS 38
#define LINUX_PR_GET_NO_NEW_PRIVS 39
#define LINUX_SECCOMP_SET_MODE_STRICT 0

#define PROCESS_RUNNING 1
#define PROCESS_BLOCKED_FUTEX 2
#define PROCESS_BLOCKED_SLEEP 3
#define PROCESS_ZOMBIE 4

#define FD_FILE 1
#define FD_PIPE_READ 2
#define FD_PIPE_WRITE 3
#define FD_EPOLL 4
#define FD_UNIX_SOCKET 5
#define FD_DIRECTORY 6
#define FD_EVENTFD 7
#define FD_TIMERFD 8
#define FD_MEMFD 9
#define MAX_PIPE_OBJECTS 64
#define MAX_SOCKET_PAIRS 64
#define MAX_EPOLL_OBJECTS 64
#define MAX_EVENT_OBJECTS 64
#define MAX_TIMER_OBJECTS 64
#define MAX_MEMFD_OBJECTS 64
#define MAX_MEMFD_SIZE (64UL * 1024UL * 1024UL)
#define MAX_EPOLL_ENTRIES 256
#define IPC_BUFFER_SIZE 4096
#define LINUX_EPOLL_CTL_ADD 1
#define LINUX_EPOLL_CTL_DEL 2
#define LINUX_EPOLL_CTL_MOD 3
#define LINUX_EPOLLIN 0x001U
#define LINUX_EPOLLOUT 0x004U

#define CONTEXT_R15 0
#define CONTEXT_R11 4
#define CONTEXT_R10 5
#define CONTEXT_R9 6
#define CONTEXT_R8 7
#define CONTEXT_RDI 9
#define CONTEXT_RSI 10
#define CONTEXT_RDX 11
#define CONTEXT_RAX 14
#define CONTEXT_RIP 15
#define CONTEXT_RFLAGS 16
#define CONTEXT_RSP 17

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} DescriptorPointer;

typedef struct __attribute__((packed)) {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} IdtEntry;

typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
} Tss64;

typedef struct __attribute__((packed)) {
    uint8_t magic[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t program_header_offset;
    uint64_t section_header_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_header_size;
    uint16_t program_header_count;
    uint16_t section_header_size;
    uint16_t section_header_count;
    uint16_t section_name_index;
} Elf64Header;

typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
} Elf64ProgramHeader;

typedef struct __attribute__((packed)) {
    int64_t tag;
    uint64_t value;
} Elf64Dynamic;

typedef struct __attribute__((packed)) {
    uint64_t offset;
    uint64_t information;
    int64_t addend;
} Elf64Rela;

typedef struct __attribute__((packed)) {
    uint32_t name;
    uint8_t information;
    uint8_t other;
    uint16_t section_index;
    uint64_t value;
    uint64_t size;
} Elf64Symbol;

typedef struct __attribute__((packed)) {
    uint16_t version;
    uint16_t count;
    uint32_t file;
    uint32_t auxiliary;
    uint32_t next;
} Elf64VersionNeed;

typedef struct __attribute__((packed)) {
    uint32_t hash;
    uint16_t flags;
    uint16_t other;
    uint32_t name;
    uint32_t next;
} Elf64VersionNeedAux;

typedef struct __attribute__((packed)) {
    uint16_t version;
    uint16_t flags;
    uint16_t index;
    uint16_t count;
    uint32_t hash;
    uint32_t auxiliary;
    uint32_t next;
} Elf64VersionDefinition;

typedef struct __attribute__((packed)) {
    uint32_t name;
    uint32_t next;
} Elf64VersionDefinitionAux;

typedef struct {
    uintptr_t start;
    size_t size;
    uint8_t protection;
} ElfMappedSegment;

typedef struct {
    char name[FS_NAME_MAX + 1];
    uintptr_t bias;
    uintptr_t dynamic_address;
    size_t dynamic_size;
    uintptr_t string_table;
    size_t string_size;
    uintptr_t symbol_table;
    size_t symbol_entry_size;
    uint32_t symbol_count;
    uintptr_t gnu_hash_bloom;
    uintptr_t gnu_hash_buckets;
    uintptr_t gnu_hash_chains;
    uint32_t gnu_hash_bucket_count;
    uint32_t gnu_hash_symbol_offset;
    uint32_t gnu_hash_bloom_size;
    uint32_t gnu_hash_bloom_shift;
    uintptr_t version_symbols;
    uintptr_t version_definitions;
    uint32_t version_definition_count;
    uintptr_t version_requirements;
    uint32_t version_requirement_count;
    uintptr_t rela;
    size_t rela_size;
    size_t rela_entry_size;
    uintptr_t jump_rela;
    size_t jump_rela_size;
    uintptr_t init_function;
    uintptr_t fini_function;
    uintptr_t init_array;
    size_t init_array_size;
    uintptr_t fini_array;
    size_t fini_array_size;
    uintptr_t preinit_array;
    size_t preinit_array_size;
    uintptr_t tls_image;
    size_t tls_file_size;
    size_t tls_memory_size;
    size_t tls_alignment;
    size_t tls_offset;
    uint32_t tls_module_id;
    uint64_t needed[ELF_MAX_NEEDED];
    int needed_count;
    ElfMappedSegment segments[ELF_MAX_SEGMENTS];
    int segment_count;
} ElfLoadedObject;

typedef struct {
    VmSpace vm;
    int references;
    uint8_t *tls_template;
    size_t tls_template_size;
    size_t tls_static_size;
    uintptr_t tls_template_thread_pointer;
    uint32_t tls_module_count;
    uintptr_t loader_mapping_end;
} AddressSpace;

typedef struct {
    bool used;
    int type;
    int object;
    int endpoint;
    char name[FS_NAME_MAX + 1];
    uint64_t offset;
    int flags;
} FileDescriptor;

typedef struct {
    uint8_t data[IPC_BUFFER_SIZE];
    uint32_t read_offset;
    uint32_t write_offset;
    uint32_t count;
} IpcQueue;

typedef struct {
    bool used;
    IpcQueue queue;
} PipeObject;

typedef struct {
    bool used;
    IpcQueue incoming[2];
} UnixSocketPair;

typedef struct __attribute__((packed)) {
    uint32_t events;
    uint64_t data;
} LinuxEpollEvent;

typedef struct __attribute__((packed)) {
    int32_t descriptor;
    int16_t events;
    int16_t returned_events;
} LinuxPollFd;

typedef struct {
    bool used;
    int descriptor;
    LinuxEpollEvent event;
} EpollEntry;

typedef struct {
    bool used;
    EpollEntry entries[MAX_EPOLL_ENTRIES];
} EpollObject;

typedef struct {
    bool used;
    bool semaphore;
    uint64_t counter;
} EventObject;

typedef struct {
    bool used;
    uint64_t expires;
    uint64_t interval;
} TimerObject;

typedef struct {
    bool used;
    uint32_t references;
    uint64_t size;
    size_t frame_capacity;
    VmFrame **frames;
} MemfdObject;

typedef struct {
    uint64_t handler;
    uint64_t flags;
    uint64_t restorer;
    uint64_t mask;
} LinuxSignalAction;

typedef struct {
    uint64_t magic;
    uint64_t context[18];
} LinuxSignalFrame;

typedef struct __attribute__((aligned(16))) {
    uint8_t bytes[512];
} FxState;

typedef struct {
    int64_t seconds;
    int64_t nanoseconds;
} LinuxTimespec;

typedef struct {
    LinuxTimespec interval;
    LinuxTimespec value;
} LinuxItimerspec;

typedef struct {
    uintptr_t base;
    uint64_t length;
} LinuxIovec;

typedef struct {
    uint64_t current;
    uint64_t maximum;
} LinuxRlimit;

typedef struct {
    uint64_t device;
    uint64_t inode;
    uint64_t links;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t padding;
    uint64_t raw_device;
    int64_t size;
    int64_t block_size;
    int64_t blocks;
    LinuxTimespec access_time;
    LinuxTimespec modify_time;
    LinuxTimespec change_time;
    int64_t reserved[3];
} LinuxStat;

typedef struct {
    bool used;
    int state;
    int pid;
    int tgid;
    int parent_pid;
    AddressSpace *address_space;
    uint64_t context[18];
    FxState fx_state;
    char name[FS_NAME_MAX + 1];
    char output[512];
    int output_length;
    int exit_code;
    uintptr_t program_break;
    uintptr_t mmap_hint;
    uintptr_t clear_child_tid;
    uintptr_t futex_address;
    uint64_t wake_tick;
    uint64_t fs_base;
    uint64_t gs_base;
    uintptr_t tls_mapping;
    size_t tls_mapping_size;
    uint64_t pending_signals;
    bool no_new_privs;
    bool seccomp_strict;
    LinuxSignalAction signal_actions[32];
    FileDescriptor descriptors[MAX_FILE_DESCRIPTORS];
} Process;

extern void arch_load_gdt(const DescriptorPointer *pointer);
extern void arch_load_idt(const DescriptorPointer *pointer);
extern void process_enter_user(void);
extern void syscall_entry(void);
extern void linux_syscall_entry(void);
extern void timer_entry(void);
extern void page_fault_entry(void);
extern const uint8_t builtin_init_start[];
extern const uint8_t builtin_init_end[];
extern const uint8_t builtin_libnova_start[];
extern const uint8_t builtin_libnova_end[];
extern const uint8_t builtin_nova_ld_start[];
extern const uint8_t builtin_nova_ld_end[];
extern const uint8_t builtin_libc_start[];
extern const uint8_t builtin_libc_end[];
extern const uint8_t builtin_libpthread_start[];
extern const uint8_t builtin_libpthread_end[];
extern const uint8_t builtin_libdl_start[];
extern const uint8_t builtin_libdl_end[];
extern const uint8_t builtin_libm_start[];
extern const uint8_t builtin_libm_end[];
extern const uint8_t builtin_libgcc_start[];
extern const uint8_t builtin_libgcc_end[];
extern const uint8_t builtin_libtls_start[];
extern const uint8_t builtin_libtls_end[];
extern const uint8_t builtin_libtlsdesc_start[];
extern const uint8_t builtin_libtlsdesc_end[];
extern const uint8_t builtin_libctor_start[];
extern const uint8_t builtin_libctor_end[];

static uint64_t gdt[7] __attribute__((aligned(16)));
static IdtEntry idt[256] __attribute__((aligned(16)));
static Tss64 tss;
static uint8_t syscall_stack[16384] __attribute__((aligned(16)));
static Process processes[MAX_PROCESSES];
static PipeObject pipe_objects[MAX_PIPE_OBJECTS];
static UnixSocketPair socket_pairs[MAX_SOCKET_PAIRS];
static EpollObject epoll_objects[MAX_EPOLL_OBJECTS];
static EventObject event_objects[MAX_EVENT_OBJECTS];
static TimerObject timer_objects[MAX_TIMER_OBJECTS];
static MemfdObject memfd_objects[MAX_MEMFD_OBJECTS];
static FxState default_fx_state;

uint64_t process_kernel_rsp;
uint64_t process_syscall_rsp;
uint64_t process_user_rsp;
uint64_t process_context[18];
volatile uint64_t scheduler_ticks;

static int current_slot = -1;
static int last_slot = -1;
static int foreground_slot = -1;
static int next_pid = 1;
static uint64_t syscall_counter;
static uint64_t cow_fault_counter;
static uint64_t demand_fault_counter;

static void descriptor_retain_resources(const FileDescriptor *descriptor);
static void descriptor_release_resources(const FileDescriptor *descriptor);

static uintptr_t align_down(uintptr_t value) {
    return value & ~(uintptr_t)(VM_PAGE_SIZE - 1);
}

static uintptr_t align_up(uintptr_t value) {
    return (value + VM_PAGE_SIZE - 1) & ~(uintptr_t)(VM_PAGE_SIZE - 1);
}

static uint64_t linux_error(int error) {
    return (uint64_t)(int64_t)-error;
}

static uint64_t read_msr(uint32_t msr) {
    uint32_t low;
    uint32_t high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static void write_msr(uint32_t msr, uint64_t value) {
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"((uint32_t)value),
                      "d"((uint32_t)(value >> 32)));
}

static void set_tss_descriptor(int index, uintptr_t base, uint32_t limit) {
    uint64_t low = (limit & 0xFFFF) |
                   ((base & 0xFFFFFF) << 16) |
                   ((uint64_t)0x89 << 40) |
                   ((uint64_t)((limit >> 16) & 0x0F) << 48) |
                   ((uint64_t)((base >> 24) & 0xFF) << 56);
    gdt[index] = low;
    gdt[index + 1] = base >> 32;
}

static void set_idt_gate(int vector, void (*handler)(void), uint8_t attributes) {
    uintptr_t address = (uintptr_t)handler;
    idt[vector].offset_low = (uint16_t)address;
    idt[vector].selector = 0x08;
    idt[vector].ist = 0;
    idt[vector].attributes = attributes;
    idt[vector].offset_middle = (uint16_t)(address >> 16);
    idt[vector].offset_high = (uint32_t)(address >> 32);
    idt[vector].reserved = 0;
}

static AddressSpace *address_space_create(void) {
    AddressSpace *address_space = heap_alloc(sizeof(*address_space));
    if (!address_space) return NULL;
    memset(address_space, 0, sizeof(*address_space));
    if (!vm_space_create(&address_space->vm)) {
        heap_free(address_space);
        return NULL;
    }
    address_space->references = 1;
    return address_space;
}

static AddressSpace *address_space_clone(AddressSpace *source) {
    AddressSpace *address_space = heap_alloc(sizeof(*address_space));
    if (!address_space) return NULL;
    memset(address_space, 0, sizeof(*address_space));
    if (!vm_space_clone_cow(&address_space->vm, &source->vm)) {
        heap_free(address_space);
        return NULL;
    }
    if (source->tls_template_size) {
        address_space->tls_template = heap_alloc(source->tls_template_size);
        if (!address_space->tls_template) {
            vm_space_destroy(&address_space->vm);
            heap_free(address_space);
            return NULL;
        }
        memcpy(address_space->tls_template, source->tls_template,
               source->tls_template_size);
        address_space->tls_template_size = source->tls_template_size;
        address_space->tls_static_size = source->tls_static_size;
        address_space->tls_template_thread_pointer =
            source->tls_template_thread_pointer;
        address_space->tls_module_count = source->tls_module_count;
    }
    address_space->loader_mapping_end = source->loader_mapping_end;
    address_space->references = 1;
    return address_space;
}

static void address_space_release(AddressSpace *address_space) {
    if (!address_space || --address_space->references > 0) return;
    vm_space_destroy(&address_space->vm);
    if (address_space->tls_template) heap_free(address_space->tls_template);
    heap_free(address_space);
}

static bool file_info(const char *name, FsFileInfo *result) {
    return fs_path_info(name, result, true);
}

static bool valid_elf(const Elf64Header *header, size_t size) {
    return size >= sizeof(*header) && header->magic[0] == 0x7F &&
           header->magic[1] == 'E' && header->magic[2] == 'L' &&
           header->magic[3] == 'F' && header->magic[4] == 2 &&
           header->magic[5] == 1 && (header->type == 2 || header->type == 3) &&
           header->machine == 0x3E &&
           header->program_header_size == sizeof(Elf64ProgramHeader) &&
           header->program_header_count <= 64 &&
           header->program_header_offset +
           (uint64_t)header->program_header_count * sizeof(Elf64ProgramHeader) <= size;
}

static bool map_stack(AddressSpace *address_space) {
    return vm_map_page(&address_space->vm, VM_USER_STACK_TOP - VM_PAGE_SIZE,
                       VM_PROT_READ | VM_PROT_WRITE);
}

static uint8_t elf_segment_protection(uint32_t flags) {
    uint8_t protection = 0;
    if (flags & 4) protection |= VM_PROT_READ;
    if (flags & 2) protection |= VM_PROT_WRITE;
    if (flags & 1) protection |= VM_PROT_EXEC;
    return protection;
}

static bool copy_mapped_string(const AddressSpace *address_space, uintptr_t address,
                               char *output, size_t capacity) {
    if (!address || capacity < 2) return false;
    for (size_t index = 0; index < capacity - 1; ++index) {
        if (!vm_copy_from_user(&address_space->vm, &output[index], address + index, 1)) {
            return false;
        }
        if (!output[index]) return true;
    }
    output[capacity - 1] = 0;
    return false;
}

static bool elf_basename(const char *path, char output[FS_NAME_MAX + 1]) {
    const char *base = path;
    for (const char *cursor = path; *cursor; ++cursor) {
        if (*cursor == '/') base = cursor + 1;
    }
    size_t length = strlen(base);
    if (!length || length > FS_NAME_MAX) return false;
    for (size_t index = 0; index < length; ++index) {
        if ((base[index] == '.' && base[index + 1] == '.') || base[index] == '/') return false;
    }
    strcpy(output, base);
    return true;
}

static bool parse_gnu_hash(AddressSpace *address_space, ElfLoadedObject *object,
                           uintptr_t hash_address) {
    uint32_t header[4];
    if (!vm_copy_from_user(&address_space->vm, header, hash_address, sizeof(header)) ||
        !header[0] || !header[2] || header[0] > ELF_MAX_SYMBOLS ||
        header[1] > ELF_MAX_SYMBOLS || header[2] > ELF_MAX_SYMBOLS ||
        header[3] >= 32) return false;
    uintptr_t bloom_bytes = (uintptr_t)header[2] * sizeof(uint64_t);
    uintptr_t bucket_bytes = (uintptr_t)header[0] * sizeof(uint32_t);
    uintptr_t bloom = hash_address + sizeof(header);
    uintptr_t buckets = bloom + bloom_bytes;
    uintptr_t chains = buckets + bucket_bytes;
    if (bloom < hash_address || buckets < bloom || chains < buckets) return false;
    object->gnu_hash_bloom = bloom;
    object->gnu_hash_buckets = buckets;
    object->gnu_hash_chains = chains;
    object->gnu_hash_bucket_count = header[0];
    object->gnu_hash_symbol_offset = header[1];
    object->gnu_hash_bloom_size = header[2];
    object->gnu_hash_bloom_shift = header[3];

    uint32_t maximum_symbol = header[1];
    for (uint32_t bucket_index = 0; bucket_index < header[0]; ++bucket_index) {
        uint32_t symbol;
        if (!vm_copy_from_user(&address_space->vm, &symbol,
                buckets + (uintptr_t)bucket_index * sizeof(symbol), sizeof(symbol))) {
            return false;
        }
        if (!symbol) continue;
        if (symbol < header[1] || symbol >= ELF_MAX_SYMBOLS) return false;
        for (;;) {
            uint32_t chain;
            if (!vm_copy_from_user(&address_space->vm, &chain,
                    chains + (uintptr_t)(symbol - header[1]) * sizeof(chain),
                    sizeof(chain))) return false;
            if (symbol >= maximum_symbol) maximum_symbol = symbol + 1;
            if (chain & 1U) break;
            if (++symbol >= ELF_MAX_SYMBOLS) return false;
        }
    }
    if (maximum_symbol > object->symbol_count) object->symbol_count = maximum_symbol;
    return true;
}

static bool parse_dynamic_object(AddressSpace *address_space, ElfLoadedObject *object) {
    if (!object->dynamic_address || !object->dynamic_size) return true;
    uintptr_t hash_address = 0;
    uintptr_t gnu_hash_address = 0;
    object->rela_entry_size = sizeof(Elf64Rela);
    object->symbol_entry_size = sizeof(Elf64Symbol);
    for (size_t offset = 0; offset + sizeof(Elf64Dynamic) <= object->dynamic_size;
         offset += sizeof(Elf64Dynamic)) {
        Elf64Dynamic entry;
        if (!vm_copy_from_user(&address_space->vm, &entry,
                               object->dynamic_address + offset, sizeof(entry))) return false;
        if (entry.tag == ELF_DT_NULL) break;
        if (entry.tag == ELF_DT_NEEDED) {
            if (object->needed_count >= ELF_MAX_NEEDED) return false;
            object->needed[object->needed_count++] = entry.value;
        } else if (entry.tag == ELF_DT_HASH) {
            hash_address = object->bias + (uintptr_t)entry.value;
        } else if (entry.tag == ELF_DT_GNU_HASH) {
            gnu_hash_address = object->bias + (uintptr_t)entry.value;
        } else if (entry.tag == ELF_DT_VERSYM) {
            object->version_symbols = object->bias + (uintptr_t)entry.value;
        } else if (entry.tag == ELF_DT_VERDEF) {
            object->version_definitions = object->bias + (uintptr_t)entry.value;
        } else if (entry.tag == ELF_DT_VERDEFNUM) {
            if (entry.value > 65535) return false;
            object->version_definition_count = (uint32_t)entry.value;
        } else if (entry.tag == ELF_DT_VERNEED) {
            object->version_requirements = object->bias + (uintptr_t)entry.value;
        } else if (entry.tag == ELF_DT_VERNEEDNUM) {
            if (entry.value > 65535) return false;
            object->version_requirement_count = (uint32_t)entry.value;
        } else if (entry.tag == ELF_DT_STRTAB) {
            object->string_table = object->bias + (uintptr_t)entry.value;
        } else if (entry.tag == ELF_DT_SYMTAB) {
            object->symbol_table = object->bias + (uintptr_t)entry.value;
        } else if (entry.tag == ELF_DT_STRSZ) {
            object->string_size = (size_t)entry.value;
        } else if (entry.tag == ELF_DT_SYMENT) {
            object->symbol_entry_size = (size_t)entry.value;
        } else if (entry.tag == ELF_DT_INIT) {
            object->init_function = object->bias + (uintptr_t)entry.value;
        } else if (entry.tag == ELF_DT_FINI) {
            object->fini_function = object->bias + (uintptr_t)entry.value;
        } else if (entry.tag == ELF_DT_INIT_ARRAY) {
            object->init_array = object->bias + (uintptr_t)entry.value;
        } else if (entry.tag == ELF_DT_INIT_ARRAYSZ) {
            object->init_array_size = (size_t)entry.value;
        } else if (entry.tag == ELF_DT_FINI_ARRAY) {
            object->fini_array = object->bias + (uintptr_t)entry.value;
        } else if (entry.tag == ELF_DT_FINI_ARRAYSZ) {
            object->fini_array_size = (size_t)entry.value;
        } else if (entry.tag == ELF_DT_PREINIT_ARRAY) {
            object->preinit_array = object->bias + (uintptr_t)entry.value;
        } else if (entry.tag == ELF_DT_PREINIT_ARRAYSZ) {
            object->preinit_array_size = (size_t)entry.value;
        } else if (entry.tag == ELF_DT_RELA) {
            object->rela = object->bias + (uintptr_t)entry.value;
        } else if (entry.tag == ELF_DT_RELASZ) {
            object->rela_size = (size_t)entry.value;
        } else if (entry.tag == ELF_DT_RELAENT) {
            object->rela_entry_size = (size_t)entry.value;
        } else if (entry.tag == ELF_DT_JMPREL) {
            object->jump_rela = object->bias + (uintptr_t)entry.value;
        } else if (entry.tag == ELF_DT_PLTRELSZ) {
            object->jump_rela_size = (size_t)entry.value;
        } else if (entry.tag == ELF_DT_PLTREL && entry.value != ELF_DT_RELA) {
            return false;
        }
    }
    if (object->symbol_entry_size != sizeof(Elf64Symbol) ||
        object->rela_entry_size != sizeof(Elf64Rela) ||
        object->init_array_size % sizeof(uintptr_t) ||
        object->fini_array_size % sizeof(uintptr_t) ||
        object->preinit_array_size % sizeof(uintptr_t) ||
        (object->init_array_size && !object->init_array) ||
        (object->fini_array_size && !object->fini_array) ||
        (object->preinit_array_size && !object->preinit_array)) return false;
    if (hash_address) {
        uint32_t header[2];
        if (!vm_copy_from_user(&address_space->vm, header, hash_address, sizeof(header)) ||
            !header[0] || !header[1] || header[1] > ELF_MAX_SYMBOLS) return false;
        object->symbol_count = header[1];
    }
    if (gnu_hash_address && !parse_gnu_hash(address_space, object, gnu_hash_address)) {
        return false;
    }
    /* GNU hash deliberately omits leading/trailing undefined symbols.  Dynamic
       relocations still index those entries, so use the conventional dynsym to
       dynstr layout to retain the complete table bound for requester symbols. */
    if (object->symbol_table && object->string_table > object->symbol_table) {
        uintptr_t span = object->string_table - object->symbol_table;
        uint64_t count = span / sizeof(Elf64Symbol);
        if (count && count <= ELF_MAX_SYMBOLS && count > object->symbol_count) {
            object->symbol_count = (uint32_t)count;
        }
    }
    return true;
}

static bool map_elf_object(AddressSpace *address_space, const char *name,
                           uintptr_t requested_bias, ElfLoadedObject *object,
                           uintptr_t *entry_out, uintptr_t *maximum_end,
                           char interpreter[FS_NAME_MAX + 1]) {
    FsFileInfo info;
    if (!fs_is_ready() || !file_info(name, &info) || !info.size) {
        return false;
    }
    Elf64Header header;
    if (fs_read_at(name, 0, &header, sizeof(header)) != (int)sizeof(header) ||
        !valid_elf(&header, info.size)) return false;
    Elf64ProgramHeader program_headers[64];
    size_t program_header_bytes =
        (size_t)header.program_header_count * sizeof(Elf64ProgramHeader);
    if (fs_read_at(name, header.program_header_offset, program_headers,
                   program_header_bytes) != (int)program_header_bytes) return false;
    memset(object, 0, sizeof(*object));
    strncpy(object->name, name, sizeof(object->name) - 1);
    object->bias = header.type == 3 ? requested_bias : 0;
    if (interpreter) interpreter[0] = 0;
    for (uint16_t index = 0; index < header.program_header_count; ++index) {
        const Elf64ProgramHeader *segment = &program_headers[index];
        if (segment->type != ELF_PT_INTERP) continue;
        if (!interpreter || !segment->file_size || segment->file_size > FS_NAME_MAX + 1 ||
            segment->offset > info.size || segment->file_size > info.size - segment->offset) {
            return false;
        }
        char path[FS_NAME_MAX + 2];
        if (fs_read_at(name, segment->offset, path, (size_t)segment->file_size) !=
            (int)segment->file_size) return false;
        path[segment->file_size] = 0;
        if (!elf_basename(path, interpreter)) return false;
    }
    if (interpreter && interpreter[0]) {
        FsFileInfo interpreter_info;
        if (!file_info(interpreter, &interpreter_info)) {
            serial_write("NovaOS: missing ELF interpreter: ");
            serial_write(interpreter);
            serial_write("\r\n");
            return false;
        }
    }

    bool has_segment = false;
    for (uint16_t index = 0; index < header.program_header_count; ++index) {
        const Elf64ProgramHeader *segment = &program_headers[index];
        if (segment->type == ELF_PT_INTERP) continue;
        if (segment->type == ELF_PT_TLS) {
            size_t alignment = (size_t)(segment->alignment ? segment->alignment : 1);
            uintptr_t image = (uintptr_t)segment->virtual_address + object->bias;
            if (object->tls_memory_size || segment->memory_size < segment->file_size ||
                !alignment || (alignment & (alignment - 1)) ||
                alignment > ELF_TLS_MAX_ALIGN || image < VM_USER_BASE ||
                image + segment->memory_size < image ||
                image + segment->memory_size > VM_USER_LOAD_LIMIT) return false;
            object->tls_image = image;
            object->tls_file_size = (size_t)segment->file_size;
            object->tls_memory_size = (size_t)segment->memory_size;
            object->tls_alignment = alignment;
            continue;
        }
        if (segment->type == ELF_PT_DYNAMIC) {
            object->dynamic_address = (uintptr_t)segment->virtual_address + object->bias;
            object->dynamic_size = (size_t)segment->memory_size;
            continue;
        }
        if (segment->type != ELF_PT_LOAD) continue;
        if (object->segment_count >= ELF_MAX_SEGMENTS) {
            return false;
        }
        uintptr_t start = (uintptr_t)segment->virtual_address + object->bias;
        uintptr_t end = start + (uintptr_t)segment->memory_size;
        if (end < start || start < VM_USER_BASE || end > VM_USER_LOAD_LIMIT ||
            segment->memory_size < segment->file_size ||
            segment->offset > info.size || segment->file_size > info.size - segment->offset) {
            return false;
        }
        for (uintptr_t page = align_down(start); page < align_up(end); page += VM_PAGE_SIZE) {
            if (!vm_map_page(&address_space->vm, page,
                             VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXEC)) {
                return false;
            }
        }
        uint8_t contents[512];
        uint64_t copied = 0;
        while (copied < segment->file_size) {
            size_t chunk = (size_t)(segment->file_size - copied);
            if (chunk > sizeof(contents)) chunk = sizeof(contents);
            int loaded = fs_read_at(name, segment->offset + copied, contents, chunk);
            if (loaded <= 0 || !vm_copy_to_user(&address_space->vm, start + copied,
                                                 contents, (size_t)loaded)) return false;
            copied += (uint64_t)loaded;
        }
        ElfMappedSegment *mapped = &object->segments[object->segment_count++];
        mapped->start = align_down(start);
        mapped->size = align_up(end) - mapped->start;
        mapped->protection = elf_segment_protection(segment->flags);
        if (maximum_end && end > *maximum_end) *maximum_end = end;
        has_segment = true;
    }
    if (object->tls_memory_size &&
        !vm_range_mapped(&address_space->vm, object->tls_image,
                         object->tls_memory_size, false)) return false;
    if (!has_segment || !parse_dynamic_object(address_space, object)) {
        return false;
    }
    if (entry_out) {
        uintptr_t entry = (uintptr_t)header.entry + object->bias;
        if (entry < VM_USER_BASE || entry >= VM_USER_LOAD_LIMIT) {
            return false;
        }
        *entry_out = entry;
    }
    return true;
}

static bool elf_file_offset(const Elf64ProgramHeader *headers, uint16_t count,
                            uint64_t virtual_address, uint64_t *file_offset) {
    for (uint16_t index = 0; index < count; ++index) {
        const Elf64ProgramHeader *segment = &headers[index];
        if (segment->type != ELF_PT_LOAD || virtual_address < segment->virtual_address ||
            virtual_address >= segment->virtual_address + segment->file_size) continue;
        *file_offset = segment->offset + virtual_address - segment->virtual_address;
        return true;
    }
    return false;
}

static bool read_file_string(const char *file, uint64_t offset,
                             char *output, size_t capacity) {
    if (!capacity) return false;
    for (size_t index = 0; index < capacity - 1; ++index) {
        if (fs_read_at(file, offset + index, &output[index], 1) != 1) return false;
        if (!output[index]) return true;
    }
    output[capacity - 1] = 0;
    return false;
}

int process_audit_elf_dependencies(const char *name) {
    FsFileInfo info;
    Elf64Header header;
    if (!file_info(name, &info) || !info.size ||
        fs_read_at(name, 0, &header, sizeof(header)) != (int)sizeof(header) ||
        !valid_elf(&header, info.size)) return -1;
    Elf64ProgramHeader headers[64];
    size_t header_bytes = (size_t)header.program_header_count * sizeof(headers[0]);
    if (fs_read_at(name, header.program_header_offset, headers, header_bytes) !=
        (int)header_bytes) return -1;
    int missing = 0;
    uint64_t needed[ELF_MAX_NEEDED];
    int needed_count = 0;
    uint64_t string_virtual = 0;
    uint64_t dynamic_offset = 0;
    uint64_t dynamic_size = 0;
    for (uint16_t index = 0; index < header.program_header_count; ++index) {
        const Elf64ProgramHeader *segment = &headers[index];
        if (segment->type == ELF_PT_INTERP) {
            char path[FS_NAME_MAX + 2];
            char dependency[FS_NAME_MAX + 1];
            if (!segment->file_size || segment->file_size > FS_NAME_MAX + 1 ||
                fs_read_at(name, segment->offset, path, (size_t)segment->file_size) !=
                    (int)segment->file_size) return -1;
            path[segment->file_size] = 0;
            FsFileInfo dependency_info;
            if (!elf_basename(path, dependency) || !file_info(dependency, &dependency_info)) {
                serial_write("NovaOS Chrome audit: missing interpreter ");
                serial_write(path);
                serial_write("\r\n");
                ++missing;
            }
        } else if (segment->type == ELF_PT_DYNAMIC) {
            dynamic_offset = segment->offset;
            dynamic_size = segment->file_size;
        }
    }
    if (!dynamic_offset || !dynamic_size || dynamic_offset + dynamic_size > info.size) return -1;
    for (uint64_t offset = 0; offset + sizeof(Elf64Dynamic) <= dynamic_size;
         offset += sizeof(Elf64Dynamic)) {
        Elf64Dynamic entry;
        if (fs_read_at(name, dynamic_offset + offset, &entry, sizeof(entry)) !=
            (int)sizeof(entry)) return -1;
        if (entry.tag == ELF_DT_NULL) break;
        if (entry.tag == ELF_DT_NEEDED) {
            if (needed_count >= ELF_MAX_NEEDED) return -1;
            needed[needed_count++] = entry.value;
        } else if (entry.tag == ELF_DT_STRTAB) {
            string_virtual = entry.value;
        }
    }
    uint64_t string_offset;
    if (!string_virtual || !elf_file_offset(headers, header.program_header_count,
                                            string_virtual, &string_offset)) return -1;
    for (int index = 0; index < needed_count; ++index) {
        char path[FS_NAME_MAX + 2];
        char dependency[FS_NAME_MAX + 1];
        FsFileInfo dependency_info;
        if (!read_file_string(name, string_offset + needed[index], path, sizeof(path)) ||
            !elf_basename(path, dependency)) return -1;
        if (!file_info(dependency, &dependency_info)) {
            serial_write("NovaOS Chrome audit: missing DT_NEEDED ");
            serial_write(dependency);
            serial_write("\r\n");
            ++missing;
        }
    }
    return missing;
}

static int loaded_object_index(const ElfLoadedObject objects[ELF_MAX_OBJECTS],
                               int count, const char *name) {
    for (int index = 0; index < count; ++index) {
        if (!strcmp(objects[index].name, name)) return index;
    }
    return -1;
}

static bool load_needed_objects(AddressSpace *address_space,
                                ElfLoadedObject objects[ELF_MAX_OBJECTS], int *count) {
    int library_number = 0;
    for (int object_index = 0; object_index < *count; ++object_index) {
        ElfLoadedObject *object = &objects[object_index];
        for (int needed_index = 0; needed_index < object->needed_count; ++needed_index) {
            if (!object->string_table || object->needed[needed_index] >= object->string_size) {
                return false;
            }
            char path[FS_NAME_MAX + 2];
            char name[FS_NAME_MAX + 1];
            if (!copy_mapped_string(address_space,
                    object->string_table + object->needed[needed_index], path, sizeof(path)) ||
                !elf_basename(path, name)) return false;
            if (loaded_object_index(objects, *count, name) >= 0) continue;
            if (*count >= ELF_MAX_OBJECTS) return false;
            uintptr_t bias = ELF_LIBRARY_BASE + (uintptr_t)library_number * ELF_LIBRARY_STRIDE;
            char nested_interpreter[FS_NAME_MAX + 1];
            if (!map_elf_object(address_space, name, bias, &objects[*count], NULL,
                                NULL, nested_interpreter) || nested_interpreter[0]) return false;
            ++library_number;
            ++*count;
        }
    }
    return true;
}

static bool read_object_symbol(const AddressSpace *address_space,
                               const ElfLoadedObject *object, uint32_t index,
                               Elf64Symbol *symbol) {
    return object->symbol_table && index < object->symbol_count &&
           vm_copy_from_user(&address_space->vm, symbol,
               object->symbol_table + (uintptr_t)index * sizeof(*symbol), sizeof(*symbol));
}

static uint32_t gnu_symbol_hash(const char *name) {
    uint32_t hash = 5381;
    while (*name) hash = hash * 33U + (uint8_t)*name++;
    return hash;
}

static bool read_symbol_version(const AddressSpace *address_space,
                                const ElfLoadedObject *object, uint32_t symbol_index,
                                uint16_t *version) {
    if (!object->version_symbols) {
        *version = 1;
        return true;
    }
    return vm_copy_from_user(&address_space->vm, version,
        object->version_symbols + (uintptr_t)symbol_index * sizeof(*version),
        sizeof(*version));
}

static bool required_version_name(const AddressSpace *address_space,
                                  const ElfLoadedObject *object, uint16_t version_index,
                                  char output[ELF_SYMBOL_NAME_MAX]) {
    output[0] = 0;
    version_index &= 0x7FFFU;
    if (version_index <= 1) return true;
    if (!object->version_requirements || !object->version_requirement_count) return false;
    uintptr_t need_address = object->version_requirements;
    for (uint32_t need_index = 0;
         need_index < object->version_requirement_count; ++need_index) {
        Elf64VersionNeed need;
        if (!vm_copy_from_user(&address_space->vm, &need, need_address, sizeof(need)) ||
            !need.count) return false;
        uintptr_t auxiliary_address = need_address + need.auxiliary;
        if (auxiliary_address < need_address) return false;
        for (uint32_t auxiliary_index = 0; auxiliary_index < need.count;
             ++auxiliary_index) {
            Elf64VersionNeedAux auxiliary;
            if (!vm_copy_from_user(&address_space->vm, &auxiliary,
                                   auxiliary_address, sizeof(auxiliary))) return false;
            if ((auxiliary.other & 0x7FFFU) == version_index) {
                if (auxiliary.name >= object->string_size) return false;
                return copy_mapped_string(address_space,
                    object->string_table + auxiliary.name,
                    output, ELF_SYMBOL_NAME_MAX);
            }
            if (!auxiliary.next) break;
            uintptr_t next = auxiliary_address + auxiliary.next;
            if (next <= auxiliary_address) return false;
            auxiliary_address = next;
        }
        if (!need.next) break;
        uintptr_t next = need_address + need.next;
        if (next <= need_address) return false;
        need_address = next;
    }
    return false;
}

static bool defined_version_name(const AddressSpace *address_space,
                                 const ElfLoadedObject *object, uint16_t version_index,
                                 char output[ELF_SYMBOL_NAME_MAX]) {
    output[0] = 0;
    version_index &= 0x7FFFU;
    if (version_index <= 1) return true;
    if (!object->version_definitions || !object->version_definition_count) return false;
    uintptr_t definition_address = object->version_definitions;
    for (uint32_t definition_index = 0;
         definition_index < object->version_definition_count; ++definition_index) {
        Elf64VersionDefinition definition;
        if (!vm_copy_from_user(&address_space->vm, &definition, definition_address,
                               sizeof(definition))) return false;
        if ((definition.index & 0x7FFFU) == version_index) {
            uintptr_t auxiliary_address = definition_address + definition.auxiliary;
            Elf64VersionDefinitionAux auxiliary;
            if (auxiliary_address < definition_address ||
                !vm_copy_from_user(&address_space->vm, &auxiliary, auxiliary_address,
                                   sizeof(auxiliary)) ||
                auxiliary.name >= object->string_size) return false;
            return copy_mapped_string(address_space,
                object->string_table + auxiliary.name, output, ELF_SYMBOL_NAME_MAX);
        }
        if (!definition.next) break;
        uintptr_t next = definition_address + definition.next;
        if (next <= definition_address) return false;
        definition_address = next;
    }
    return false;
}

/* Returns one for a compatible version, zero for a mismatch, -1 for bad data. */
static int symbol_version_matches(const AddressSpace *address_space,
                                  const ElfLoadedObject *object,
                                  uint32_t symbol_index,
                                  const char *requested_version) {
    uint16_t raw_version;
    if (!read_symbol_version(address_space, object, symbol_index, &raw_version)) return -1;
    uint16_t version_index = raw_version & 0x7FFFU;
    if (!requested_version[0]) return (raw_version & 0x8000U) ? 0 : 1;
    if (version_index <= 1) return 0;
    char candidate_version[ELF_SYMBOL_NAME_MAX];
    if (!defined_version_name(address_space, object, version_index,
                              candidate_version)) return -1;
    return !strcmp(candidate_version, requested_version);
}

/* Returns one for a match, zero for no match, and -1 for malformed mapped data. */
static int find_symbol_in_object(AddressSpace *address_space,
                                 const ElfLoadedObject *object,
                                 const char *requested_name,
                                 const char *requested_version, uint64_t *value,
                                 uint32_t *matched_index) {
    if (object->gnu_hash_bucket_count) {
        uint32_t hash = gnu_symbol_hash(requested_name);
        uint64_t bloom_word;
        uint32_t bloom_index = (hash / 64U) % object->gnu_hash_bloom_size;
        if (!vm_copy_from_user(&address_space->vm, &bloom_word,
                object->gnu_hash_bloom + (uintptr_t)bloom_index * sizeof(bloom_word),
                sizeof(bloom_word))) return -1;
        uint64_t mask = (1UL << (hash & 63U)) |
                        (1UL << ((hash >> object->gnu_hash_bloom_shift) & 63U));
        if ((bloom_word & mask) != mask) return 0;
        uint32_t symbol_index;
        uint32_t bucket_index = hash % object->gnu_hash_bucket_count;
        if (!vm_copy_from_user(&address_space->vm, &symbol_index,
                object->gnu_hash_buckets + (uintptr_t)bucket_index * sizeof(symbol_index),
                sizeof(symbol_index))) return -1;
        if (symbol_index < object->gnu_hash_symbol_offset) return 0;
        while (symbol_index < object->symbol_count) {
            uint32_t chain;
            if (!vm_copy_from_user(&address_space->vm, &chain,
                    object->gnu_hash_chains +
                    (uintptr_t)(symbol_index - object->gnu_hash_symbol_offset) * sizeof(chain),
                    sizeof(chain))) return -1;
            if ((chain | 1U) == (hash | 1U)) {
                Elf64Symbol candidate;
                if (!read_object_symbol(address_space, object, symbol_index, &candidate)) {
                    return -1;
                }
                uint8_t binding = candidate.information >> 4;
                if (candidate.section_index && (binding == 1 || binding == 2) &&
                    candidate.name < object->string_size) {
                    char candidate_name[ELF_SYMBOL_NAME_MAX];
                    if (!copy_mapped_string(address_space,
                            object->string_table + candidate.name,
                            candidate_name, sizeof(candidate_name))) return -1;
                    if (!strcmp(candidate_name, requested_name)) {
                        int compatible = symbol_version_matches(address_space, object,
                            symbol_index, requested_version);
                        if (compatible < 0) return -1;
                        if (!compatible) {
                            if (chain & 1U) break;
                            ++symbol_index;
                            continue;
                        }
                        if (matched_index) *matched_index = symbol_index;
                        *value = object->bias + candidate.value;
                        return 1;
                    }
                }
            }
            if (chain & 1U) break;
            ++symbol_index;
        }
        return 0;
    }
    for (uint32_t index = 1; index < object->symbol_count; ++index) {
        Elf64Symbol candidate;
        if (!read_object_symbol(address_space, object, index, &candidate)) return -1;
        uint8_t binding = candidate.information >> 4;
        if (!candidate.section_index || (binding != 1 && binding != 2) ||
            candidate.name >= object->string_size) continue;
        char candidate_name[ELF_SYMBOL_NAME_MAX];
        if (!copy_mapped_string(address_space, object->string_table + candidate.name,
                                candidate_name, sizeof(candidate_name))) return -1;
        if (!strcmp(candidate_name, requested_name)) {
            int compatible = symbol_version_matches(address_space, object, index,
                                                     requested_version);
            if (compatible < 0) return -1;
            if (!compatible) continue;
            if (matched_index) *matched_index = index;
            *value = object->bias + candidate.value;
            return 1;
        }
    }
    return 0;
}

static bool resolve_symbol(AddressSpace *address_space,
                           const ElfLoadedObject objects[ELF_MAX_OBJECTS], int count,
                           const ElfLoadedObject *requester, uint32_t symbol_index,
                           uint64_t *value) {
    Elf64Symbol requested;
    if (!read_object_symbol(address_space, requester, symbol_index, &requested) ||
        !requester->string_table || requested.name >= requester->string_size) return false;
    if (requested.section_index) {
        *value = requester->bias + requested.value;
        return true;
    }
    char requested_name[ELF_SYMBOL_NAME_MAX];
    if (!copy_mapped_string(address_space, requester->string_table + requested.name,
                            requested_name, sizeof(requested_name))) return false;
    uint16_t requested_version_index;
    char requested_version[ELF_SYMBOL_NAME_MAX];
    if (!read_symbol_version(address_space, requester, symbol_index,
                             &requested_version_index) ||
        !required_version_name(address_space, requester, requested_version_index,
                               requested_version)) return false;
    for (int object_index = 0; object_index < count; ++object_index) {
        const ElfLoadedObject *object = &objects[object_index];
        int found = find_symbol_in_object(address_space, object, requested_name,
                                          requested_version, value, NULL);
        if (found < 0) return false;
        if (found > 0) return true;
    }
    if ((requested.information >> 4) == 2) {
        *value = 0;
        return true;
    }
    return false;
}

static bool resolve_named_symbol(AddressSpace *address_space,
                                 const ElfLoadedObject objects[ELF_MAX_OBJECTS],
                                 int count, const char *name, uint64_t *value) {
    for (int object_index = 0; object_index < count; ++object_index) {
        int found = find_symbol_in_object(address_space, &objects[object_index],
                                          name, "", value, NULL);
        if (found < 0) return false;
        if (found > 0) return true;
    }
    return false;
}

static bool resolve_tls_symbol(AddressSpace *address_space,
                               const ElfLoadedObject objects[ELF_MAX_OBJECTS], int count,
                               const ElfLoadedObject *requester, uint32_t symbol_index,
                               const ElfLoadedObject **definition_object,
                               Elf64Symbol *definition_symbol) {
    Elf64Symbol requested;
    if (!read_object_symbol(address_space, requester, symbol_index, &requested) ||
        !requester->string_table || requested.name >= requester->string_size) return false;
    if (requested.section_index) {
        *definition_object = requester;
        *definition_symbol = requested;
        return true;
    }
    char requested_name[ELF_SYMBOL_NAME_MAX];
    if (!copy_mapped_string(address_space, requester->string_table + requested.name,
                            requested_name, sizeof(requested_name))) return false;
    uint16_t requested_version_index;
    char requested_version[ELF_SYMBOL_NAME_MAX];
    if (!read_symbol_version(address_space, requester, symbol_index,
                             &requested_version_index) ||
        !required_version_name(address_space, requester, requested_version_index,
                               requested_version)) return false;
    for (int object_index = 0; object_index < count; ++object_index) {
        const ElfLoadedObject *candidate_object = &objects[object_index];
        uint64_t ignored_value;
        uint32_t candidate_index = 0;
        int found = find_symbol_in_object(address_space, candidate_object,
                                          requested_name, requested_version,
                                          &ignored_value, &candidate_index);
        if (found < 0) return false;
        if (!found) continue;
        if (!read_object_symbol(address_space, candidate_object, candidate_index,
                                definition_symbol)) return false;
        *definition_object = candidate_object;
        return true;
    }
    if ((requested.information >> 4) == 2) {
        memset(definition_symbol, 0, sizeof(*definition_symbol));
        *definition_object = NULL;
        return true;
    }
    return false;
}

static bool align_tls_size(size_t value, size_t alignment, size_t *result) {
    if (!alignment || (alignment & (alignment - 1)) ||
        value > (size_t)-1 - (alignment - 1)) return false;
    *result = (value + alignment - 1) & ~(alignment - 1);
    return true;
}

static bool layout_static_tls(ElfLoadedObject objects[ELF_MAX_OBJECTS], int count,
                              size_t *static_size) {
    size_t cursor = 0;
    size_t maximum_alignment = 16;
    uint32_t module_id = 1;
    for (int index = 0; index < count; ++index) {
        ElfLoadedObject *object = &objects[index];
        if (!object->tls_memory_size) continue;
        size_t alignment = object->tls_alignment ? object->tls_alignment : 1;
        if (object->tls_memory_size > ELF_TLS_MAX_SIZE ||
            cursor > ELF_TLS_MAX_SIZE - object->tls_memory_size ||
            !align_tls_size(cursor + object->tls_memory_size, alignment,
                            &object->tls_offset) ||
            object->tls_offset > ELF_TLS_MAX_SIZE) return false;
        cursor = object->tls_offset;
        if (alignment > maximum_alignment) maximum_alignment = alignment;
        object->tls_module_id = module_id++;
    }
    return align_tls_size(cursor, maximum_alignment, static_size) &&
           *static_size <= ELF_TLS_MAX_SIZE;
}

static bool map_user_rw_pages(AddressSpace *address_space, uintptr_t mapping,
                              size_t mapping_size) {
    uintptr_t page = mapping;
    for (; page < mapping + mapping_size; page += VM_PAGE_SIZE) {
        if (!vm_map_page(&address_space->vm, page, VM_PROT_READ | VM_PROT_WRITE)) break;
    }
    if (page == mapping + mapping_size) return true;
    for (uintptr_t rollback = mapping; rollback < page; rollback += VM_PAGE_SIZE) {
        vm_unmap_page(&address_space->vm, rollback);
    }
    return false;
}

static void unmap_user_pages(AddressSpace *address_space, uintptr_t mapping,
                             size_t mapping_size) {
    for (uintptr_t page = mapping; page < mapping + mapping_size; page += VM_PAGE_SIZE) {
        vm_unmap_page(&address_space->vm, page);
    }
}

static bool initialize_static_tls(AddressSpace *address_space,
                                  ElfLoadedObject objects[ELF_MAX_OBJECTS], int count,
                                  size_t static_size, uintptr_t *fs_base,
                                  uintptr_t *mapping_out, size_t *mapping_size_out) {
    *fs_base = 0;
    *mapping_out = 0;
    *mapping_size_out = 0;
    if (!static_size) return true;
    if (static_size > (size_t)-1 - ELF_TLS_TCB_SIZE - ELF_TLS_DTV_SIZE) return false;
    size_t template_size = static_size + ELF_TLS_TCB_SIZE + ELF_TLS_DTV_SIZE;
    size_t mapping_size = align_up(template_size);
    uintptr_t mapping = vm_find_free_range(&address_space->vm,
                                            VM_USER_MMAP_BASE, mapping_size);
    if (!mapping || !map_user_rw_pages(address_space, mapping, mapping_size)) return false;
    uintptr_t thread_pointer = mapping + static_size;
    bool initialized = true;
    uint8_t buffer[512];
    for (int index = 0; index < count && initialized; ++index) {
        ElfLoadedObject *object = &objects[index];
        if (!object->tls_memory_size) continue;
        uintptr_t destination = thread_pointer - object->tls_offset;
        size_t copied = 0;
        while (copied < object->tls_file_size) {
            size_t chunk = object->tls_file_size - copied;
            if (chunk > sizeof(buffer)) chunk = sizeof(buffer);
            if (!vm_copy_from_user(&address_space->vm, buffer,
                                   object->tls_image + copied, chunk) ||
                !vm_copy_to_user(&address_space->vm, destination + copied,
                                 buffer, chunk)) {
                initialized = false;
                break;
            }
            copied += chunk;
        }
    }
    uint64_t tcb[ELF_TLS_TCB_SIZE / sizeof(uint64_t)];
    memset(tcb, 0, sizeof(tcb));
    tcb[0] = thread_pointer;
    tcb[1] = thread_pointer + ELF_TLS_TCB_SIZE;
    tcb[2] = thread_pointer;
    if (initialized) {
        initialized = vm_copy_to_user(&address_space->vm, thread_pointer,
                                      tcb, sizeof(tcb));
    }
    uintptr_t dtv[ELF_TLS_DTV_ENTRIES];
    memset(dtv, 0, sizeof(dtv));
    uint32_t tls_module_count = 0;
    for (int index = 0; index < count; ++index) {
        ElfLoadedObject *object = &objects[index];
        if (!object->tls_module_id) continue;
        if (object->tls_module_id >= ELF_TLS_DTV_ENTRIES) {
            initialized = false;
            break;
        }
        dtv[object->tls_module_id] = thread_pointer - object->tls_offset;
        if (object->tls_module_id > tls_module_count) {
            tls_module_count = object->tls_module_id;
        }
    }
    dtv[0] = tls_module_count;
    if (initialized) {
        initialized = vm_copy_to_user(&address_space->vm,
            thread_pointer + ELF_TLS_TCB_SIZE, dtv, sizeof(dtv));
    }
    uint8_t *tls_template = initialized ? heap_alloc(template_size) : NULL;
    if (!tls_template || !vm_copy_from_user(&address_space->vm, tls_template,
                                             mapping, template_size)) {
        if (tls_template) heap_free(tls_template);
        unmap_user_pages(address_space, mapping, mapping_size);
        return false;
    }
    address_space->tls_template = tls_template;
    address_space->tls_template_size = template_size;
    address_space->tls_static_size = static_size;
    address_space->tls_template_thread_pointer = thread_pointer;
    address_space->tls_module_count = tls_module_count;
    *fs_base = thread_pointer;
    *mapping_out = mapping;
    *mapping_size_out = mapping_size;
    address_space->loader_mapping_end = mapping + mapping_size;
    return true;
}

static bool allocate_thread_tls(AddressSpace *address_space, uintptr_t hint,
                                uintptr_t *fs_base, uintptr_t *mapping_out,
                                size_t *mapping_size_out) {
    if (!address_space->tls_template_size) {
        *fs_base = 0;
        *mapping_out = 0;
        *mapping_size_out = 0;
        return true;
    }
    size_t mapping_size = align_up(address_space->tls_template_size);
    uintptr_t mapping = vm_find_free_range(&address_space->vm, hint, mapping_size);
    if (!mapping || !map_user_rw_pages(address_space, mapping, mapping_size) ||
        !vm_copy_to_user(&address_space->vm, mapping, address_space->tls_template,
                         address_space->tls_template_size)) {
        if (mapping) unmap_user_pages(address_space, mapping, mapping_size);
        return false;
    }
    uintptr_t thread_pointer = mapping + address_space->tls_static_size;
    if (!vm_copy_to_user(&address_space->vm, thread_pointer,
                         &thread_pointer, sizeof(thread_pointer)) ||
        !vm_copy_to_user(&address_space->vm, thread_pointer + sizeof(uintptr_t),
                         &(uintptr_t){thread_pointer + ELF_TLS_TCB_SIZE},
                         sizeof(thread_pointer)) ||
        !vm_copy_to_user(&address_space->vm, thread_pointer + 2 * sizeof(uintptr_t),
                         &thread_pointer, sizeof(thread_pointer))) {
        unmap_user_pages(address_space, mapping, mapping_size);
        return false;
    }
    for (uint32_t module = 1; module <= address_space->tls_module_count; ++module) {
        uintptr_t slot_address = thread_pointer + ELF_TLS_TCB_SIZE +
                                 (uintptr_t)module * sizeof(uintptr_t);
        uintptr_t original_block;
        if (!vm_copy_from_user(&address_space->vm, &original_block,
                               slot_address, sizeof(original_block))) {
            unmap_user_pages(address_space, mapping, mapping_size);
            return false;
        }
        if (!original_block) continue;
        if (original_block > address_space->tls_template_thread_pointer) {
            unmap_user_pages(address_space, mapping, mapping_size);
            return false;
        }
        uintptr_t block = thread_pointer -
            (address_space->tls_template_thread_pointer - original_block);
        if (!vm_copy_to_user(&address_space->vm, slot_address,
                             &block, sizeof(block))) {
            unmap_user_pages(address_space, mapping, mapping_size);
            return false;
        }
    }
    *fs_base = thread_pointer;
    *mapping_out = mapping;
    *mapping_size_out = mapping_size;
    return true;
}

static bool apply_relocation_table(AddressSpace *address_space,
                                   const ElfLoadedObject objects[ELF_MAX_OBJECTS], int count,
                                   const ElfLoadedObject *object, uintptr_t table, size_t size) {
    if (!table || !size) return true;
    if (size % sizeof(Elf64Rela)) return false;
    for (size_t offset = 0; offset < size; offset += sizeof(Elf64Rela)) {
        Elf64Rela relocation;
        if (!vm_copy_from_user(&address_space->vm, &relocation,
                               table + offset, sizeof(relocation))) return false;
        uint32_t type = (uint32_t)relocation.information;
        uint32_t symbol_index = (uint32_t)(relocation.information >> 32);
        uint64_t value = 0;
        size_t write_size = sizeof(value);
        if (type == 0) continue;
        if (type == ELF_R_X86_64_RELATIVE) {
            value = object->bias + (uint64_t)relocation.addend;
        } else if (type == ELF_R_X86_64_64 || type == ELF_R_X86_64_GLOB_DAT ||
                   type == ELF_R_X86_64_JUMP_SLOT) {
            if (!resolve_symbol(address_space, objects, count, object,
                                symbol_index, &value)) return false;
            value += (uint64_t)relocation.addend;
        } else if (type == ELF_R_X86_64_TLSDESC) {
            const ElfLoadedObject *definition_object;
            Elf64Symbol definition_symbol;
            if (!resolve_tls_symbol(address_space, objects, count, object,
                                    symbol_index, &definition_object,
                                    &definition_symbol) ||
                !definition_object || !definition_object->tls_module_id ||
                !definition_object->tls_memory_size) return false;
            uint64_t descriptor[2];
            if (!resolve_named_symbol(address_space, objects, count,
                                      "__nova_tlsdesc_static",
                                      &descriptor[0])) return false;
            descriptor[1] = definition_symbol.value - definition_object->tls_offset +
                            (uint64_t)relocation.addend;
            uintptr_t target = object->bias + (uintptr_t)relocation.offset;
            if (!vm_copy_to_user(&address_space->vm, target,
                                 descriptor, sizeof(descriptor))) return false;
            continue;
        } else if (type == ELF_R_X86_64_DTPMOD64 ||
                   type == ELF_R_X86_64_DTPOFF64 ||
                   type == ELF_R_X86_64_TPOFF64 ||
                   type == ELF_R_X86_64_DTPOFF32 ||
                   type == ELF_R_X86_64_TPOFF32) {
            const ElfLoadedObject *definition_object = object;
            Elf64Symbol definition_symbol;
            memset(&definition_symbol, 0, sizeof(definition_symbol));
            if (symbol_index &&
                !resolve_tls_symbol(address_space, objects, count, object,
                                    symbol_index, &definition_object,
                                    &definition_symbol)) return false;
            if (!definition_object) {
                value = 0;
            } else if (!definition_object->tls_module_id ||
                       !definition_object->tls_memory_size) {
                return false;
            } else if (type == ELF_R_X86_64_DTPMOD64) {
                value = definition_object->tls_module_id;
            } else if (type == ELF_R_X86_64_DTPOFF64 ||
                       type == ELF_R_X86_64_DTPOFF32) {
                value = definition_symbol.value + (uint64_t)relocation.addend;
            } else {
                value = definition_symbol.value - definition_object->tls_offset +
                        (uint64_t)relocation.addend;
            }
            if (type == ELF_R_X86_64_DTPOFF32 || type == ELF_R_X86_64_TPOFF32) {
                write_size = sizeof(uint32_t);
            }
        } else {
            return false;
        }
        uintptr_t target = object->bias + (uintptr_t)relocation.offset;
        if (!vm_copy_to_user(&address_space->vm, target, &value, write_size)) return false;
    }
    return true;
}

static bool relocate_and_protect_objects(AddressSpace *address_space,
                                         ElfLoadedObject objects[ELF_MAX_OBJECTS], int count) {
    for (int index = 0; index < count; ++index) {
        ElfLoadedObject *object = &objects[index];
        if (!apply_relocation_table(address_space, objects, count, object,
                                    object->rela, object->rela_size) ||
            !apply_relocation_table(address_space, objects, count, object,
                                    object->jump_rela, object->jump_rela_size)) return false;
    }
    for (int index = 0; index < count; ++index) {
        ElfLoadedObject *object = &objects[index];
        for (int segment = 0; segment < object->segment_count; ++segment) {
            ElfMappedSegment *mapped = &object->segments[segment];
            if (!vm_protect(&address_space->vm, mapped->start,
                            mapped->size, mapped->protection)) return false;
        }
    }
    return true;
}

static bool append_initializer_array(AddressSpace *address_space,
                                     uintptr_t array, size_t size,
                                     uintptr_t functions[ELF_STARTUP_MAX_FUNCTIONS],
                                     size_t *function_count) {
    for (size_t offset = 0; offset < size; offset += sizeof(uintptr_t)) {
        uintptr_t function;
        if (!vm_copy_from_user(&address_space->vm, &function,
                               array + offset, sizeof(function))) return false;
        if (!function || function == (uintptr_t)-1) continue;
        if (*function_count >= ELF_STARTUP_MAX_FUNCTIONS ||
            !vm_range_mapped(&address_space->vm, function, 1, false)) return false;
        functions[(*function_count)++] = function;
    }
    return true;
}

static bool append_object_initializers(AddressSpace *address_space,
                                       const ElfLoadedObject *object,
                                       uintptr_t functions[ELF_STARTUP_MAX_FUNCTIONS],
                                       size_t *function_count) {
    if (object->init_function) {
        if (*function_count >= ELF_STARTUP_MAX_FUNCTIONS ||
            !vm_range_mapped(&address_space->vm, object->init_function,
                             1, false)) return false;
        functions[(*function_count)++] = object->init_function;
    }
    return append_initializer_array(address_space, object->init_array,
                                    object->init_array_size,
                                    functions, function_count);
}

static bool prepare_interpreter_startup(AddressSpace *address_space,
                                        ElfLoadedObject objects[ELF_MAX_OBJECTS],
                                        int count, uintptr_t final_entry,
                                        uintptr_t *startup_entry,
                                        uintptr_t *startup_argument) {
    uint64_t bootstrap;
    if (!resolve_named_symbol(address_space, objects, count,
                              "nova_interpreter_start", &bootstrap)) return false;
    uintptr_t *functions = heap_calloc(ELF_STARTUP_MAX_FUNCTIONS,
                                       sizeof(*functions));
    if (!functions) return false;
    size_t function_count = 0;
    bool valid = append_initializer_array(address_space,
        objects[0].preinit_array, objects[0].preinit_array_size,
        functions, &function_count);
    for (int index = count - 1; index >= 2 && valid; --index) {
        valid = append_object_initializers(address_space, &objects[index],
                                           functions, &function_count);
    }
    if (valid) {
        valid = append_object_initializers(address_space, &objects[0],
                                           functions, &function_count);
    }
    size_t table_size = 3 * sizeof(uintptr_t) +
                        function_count * sizeof(uintptr_t);
    size_t mapping_size = align_up(table_size);
    uintptr_t hint = address_space->loader_mapping_end ?
                     address_space->loader_mapping_end : VM_USER_MMAP_BASE;
    uintptr_t mapping = valid ? vm_find_free_range(&address_space->vm,
                                                    hint, mapping_size) : 0;
    if (!mapping || !map_user_rw_pages(address_space, mapping, mapping_size)) {
        heap_free(functions);
        return false;
    }
    uintptr_t header[3] = {
        ELF_STARTUP_MAGIC,
        final_entry,
        function_count
    };
    valid = vm_copy_to_user(&address_space->vm, mapping, header, sizeof(header));
    if (valid && function_count) {
        valid = vm_copy_to_user(&address_space->vm, mapping + sizeof(header),
                                functions, function_count * sizeof(uintptr_t));
    }
    heap_free(functions);
    if (!valid || !vm_protect(&address_space->vm, mapping, mapping_size,
                              VM_PROT_READ)) {
        unmap_user_pages(address_space, mapping, mapping_size);
        return false;
    }
    address_space->loader_mapping_end = mapping + mapping_size;
    *startup_entry = (uintptr_t)bootstrap;
    *startup_argument = mapping;
    return true;
}

static bool load_elf_image(AddressSpace *address_space, const char *name,
                           uint64_t context[18], uintptr_t *program_break,
                           uintptr_t *fs_base, uintptr_t *tls_mapping,
                           size_t *tls_mapping_size) {
    ElfLoadedObject *objects = heap_calloc(ELF_MAX_OBJECTS, sizeof(*objects));
    if (!objects) return false;
    bool success = false;
    int object_count = 1;
    uintptr_t entry = 0;
    uintptr_t maximum_end = VM_USER_BASE;
    char interpreter[FS_NAME_MAX + 1];
    if (!map_elf_object(address_space, name, VM_USER_BASE, &objects[0],
                        &entry, &maximum_end, interpreter)) goto cleanup;
    uintptr_t interpreter_bias = 0;
    if (interpreter[0]) {
        if (object_count >= ELF_MAX_OBJECTS ||
            !map_elf_object(address_space, interpreter, ELF_INTERPRETER_BASE,
                            &objects[object_count], NULL, NULL, NULL)) goto cleanup;
        interpreter_bias = objects[object_count].bias;
        ++object_count;
    }
    size_t tls_static_size;
    uintptr_t user_entry = entry;
    uintptr_t startup_argument = 0;
    if (!load_needed_objects(address_space, objects, &object_count) ||
        !layout_static_tls(objects, object_count, &tls_static_size) ||
        !relocate_and_protect_objects(address_space, objects, object_count) ||
        !initialize_static_tls(address_space, objects, object_count,
                               tls_static_size, fs_base, tls_mapping,
                               tls_mapping_size)) goto cleanup;
    if (interpreter[0] &&
        !prepare_interpreter_startup(address_space, objects, object_count,
                                     entry, &user_entry,
                                     &startup_argument)) goto cleanup;
    if (!map_stack(address_space)) goto cleanup;

    uint64_t initial_stack[] = {
        0, 0, 0,
        6, VM_PAGE_SIZE,
        7, interpreter_bias,
        9, entry,
        0, 0
    };
    uintptr_t stack_pointer = (VM_USER_STACK_TOP - sizeof(initial_stack)) & ~(uintptr_t)0xFUL;
    if (!vm_copy_to_user(&address_space->vm, stack_pointer,
                         initial_stack, sizeof(initial_stack))) goto cleanup;
    memset(context, 0, 18 * sizeof(uint64_t));
    context[CONTEXT_RIP] = user_entry;
    context[CONTEXT_RFLAGS] = 0x202;
    context[CONTEXT_RSP] = stack_pointer;
    context[CONTEXT_RDI] = startup_argument;
    *program_break = align_up(maximum_end);
    if (interpreter[0]) {
        serial_write("NovaOS: PT_INTERP and DT_NEEDED shared linker ready\r\n");
    }
    if (objects[0].gnu_hash_bucket_count && objects[0].version_requirement_count) {
        serial_write("NovaOS: GNU_HASH and versioned ELF symbols verified\r\n");
    }
    if (tls_static_size) {
        serial_write("NovaOS: PT_TLS static module layout ready\r\n");
    }
    success = true;
cleanup:
    heap_free(objects);
    return success;
}

static void reset_process_slot(Process *process) {
    for (int descriptor = 3; descriptor < MAX_FILE_DESCRIPTORS; ++descriptor) {
        if (process->descriptors[descriptor].used) {
            descriptor_release_resources(&process->descriptors[descriptor]);
        }
    }
    if (process->address_space) address_space_release(process->address_space);
    memset(process, 0, sizeof(*process));
}

static int reserve_slot(void) {
    vm_kernel_activate();
    for (int index = 0; index < MAX_PROCESSES; ++index) {
        if (!processes[index].used) return index;
    }
    for (int index = 0; index < MAX_PROCESSES; ++index) {
        if (index != current_slot && processes[index].state == PROCESS_ZOMBIE) {
            reset_process_slot(&processes[index]);
            return index;
        }
    }
    return -1;
}

static int create_process(const char *name, int parent_pid) {
    int slot = reserve_slot();
    if (slot < 0) return -1;
    Process *process = &processes[slot];
    memset(process, 0, sizeof(*process));
    process->address_space = address_space_create();
    memcpy(&process->fx_state, &default_fx_state, sizeof(process->fx_state));
    if (!process->address_space || !load_elf_image(process->address_space, name,
            process->context, &process->program_break, &process->fs_base,
            &process->tls_mapping, &process->tls_mapping_size)) {
        reset_process_slot(process);
        return -1;
    }
    process->used = true;
    process->state = PROCESS_RUNNING;
    process->pid = next_pid++;
    process->tgid = process->pid;
    process->parent_pid = parent_pid;
    process->mmap_hint = align_up(process->program_break + VM_PAGE_SIZE);
    if (process->address_space->loader_mapping_end > process->mmap_hint) {
        process->mmap_hint = process->address_space->loader_mapping_end;
    }
    strncpy(process->name, name, sizeof(process->name) - 1);
    return slot;
}

static Process *current_process(void) {
    return current_slot >= 0 && current_slot < MAX_PROCESSES ? &processes[current_slot] : NULL;
}

static bool copy_user_string(Process *process, uintptr_t address, char *output, size_t capacity) {
    if (!process || !address || capacity < 2) return false;
    for (size_t index = 0; index < capacity - 1; ++index) {
        if (!vm_copy_from_user(&process->address_space->vm, &output[index], address + index, 1)) {
            return false;
        }
        if (!output[index]) return true;
    }
    output[capacity - 1] = 0;
    return false;
}

static bool normalize_path(const char *path, char output[FS_NAME_MAX + 1]) {
    size_t written = 0;
    while (*path == '/') ++path;
    while (*path) {
        const char *segment = path;
        size_t length = 0;
        while (path[length] && path[length] != '/') ++length;
        if (length == 2 && segment[0] == '.' && segment[1] == '.') return false;
        if (!(length == 1 && segment[0] == '.')) {
            if (!length || written + (written ? 1 : 0) + length > FS_NAME_MAX) return false;
            if (written) output[written++] = '/';
            memcpy(output + written, segment, length);
            written += length;
        }
        path += length;
        while (*path == '/') ++path;
    }
    output[written] = 0;
    return true;
}

static void append_output(Process *process, const char *text, size_t length) {
    if (!process) return;
    int available = (int)sizeof(process->output) - 1 - process->output_length;
    int copy = (int)length < available ? (int)length : available;
    if (copy > 0) {
        memcpy(process->output + process->output_length, text, (size_t)copy);
        process->output_length += copy;
        process->output[process->output_length] = 0;
    }
}

static uint64_t write_console(Process *process, uintptr_t address, size_t length) {
    if (length > 1024 * 1024) return linux_error(LINUX_EINVAL);
    char buffer[256];
    size_t completed = 0;
    while (completed < length) {
        size_t chunk = length - completed;
        if (chunk > sizeof(buffer) - 1) chunk = sizeof(buffer) - 1;
        if (!vm_copy_from_user(&process->address_space->vm, buffer,
                               address + completed, chunk)) return linux_error(LINUX_EFAULT);
        buffer[chunk] = 0;
        serial_write("user: ");
        serial_write(buffer);
        append_output(process, buffer, chunk);
        completed += chunk;
    }
    return length;
}

static int find_descriptor(Process *process) {
    for (int descriptor = 3; descriptor < MAX_FILE_DESCRIPTORS; ++descriptor) {
        if (!process->descriptors[descriptor].used) return descriptor;
    }
    return -1;
}

static int allocate_pipe_object(void) {
    for (int index = 0; index < MAX_PIPE_OBJECTS; ++index) {
        if (!pipe_objects[index].used) {
            memset(&pipe_objects[index], 0, sizeof(pipe_objects[index]));
            pipe_objects[index].used = true;
            return index;
        }
    }
    return -1;
}

static int allocate_socket_pair(void) {
    for (int index = 0; index < MAX_SOCKET_PAIRS; ++index) {
        if (!socket_pairs[index].used) {
            memset(&socket_pairs[index], 0, sizeof(socket_pairs[index]));
            socket_pairs[index].used = true;
            return index;
        }
    }
    return -1;
}

static int allocate_epoll_object(void) {
    for (int index = 0; index < MAX_EPOLL_OBJECTS; ++index) {
        if (!epoll_objects[index].used) {
            memset(&epoll_objects[index], 0, sizeof(epoll_objects[index]));
            epoll_objects[index].used = true;
            return index;
        }
    }
    return -1;
}

static int allocate_event_object(void) {
    for (int index = 0; index < MAX_EVENT_OBJECTS; ++index) {
        if (!event_objects[index].used) {
            memset(&event_objects[index], 0, sizeof(event_objects[index]));
            event_objects[index].used = true;
            return index;
        }
    }
    return -1;
}

static int allocate_timer_object(void) {
    for (int index = 0; index < MAX_TIMER_OBJECTS; ++index) {
        if (!timer_objects[index].used) {
            memset(&timer_objects[index], 0, sizeof(timer_objects[index]));
            timer_objects[index].used = true;
            return index;
        }
    }
    return -1;
}

static int allocate_memfd_object(void) {
    for (int index = 0; index < MAX_MEMFD_OBJECTS; ++index) {
        if (!memfd_objects[index].used) {
            memset(&memfd_objects[index], 0, sizeof(memfd_objects[index]));
            memfd_objects[index].used = true;
            memfd_objects[index].references = 1;
            return index;
        }
    }
    return -1;
}

static void memfd_release_object(int index) {
    if (index < 0 || index >= MAX_MEMFD_OBJECTS ||
        !memfd_objects[index].used) return;
    MemfdObject *object = &memfd_objects[index];
    if (!object->references || --object->references) return;
    for (size_t page = 0; page < object->frame_capacity; ++page) {
        if (object->frames[page]) vm_frame_release(object->frames[page]);
    }
    if (object->frames) heap_free(object->frames);
    memset(object, 0, sizeof(*object));
}

static void descriptor_retain_resources(const FileDescriptor *descriptor) {
    if (!descriptor || descriptor->type != FD_MEMFD || descriptor->object < 0 ||
        descriptor->object >= MAX_MEMFD_OBJECTS ||
        !memfd_objects[descriptor->object].used) return;
    ++memfd_objects[descriptor->object].references;
}

static void descriptor_release_resources(const FileDescriptor *descriptor) {
    if (descriptor && descriptor->type == FD_MEMFD) {
        memfd_release_object(descriptor->object);
    }
}

static bool memfd_ensure_capacity(MemfdObject *object, size_t page_count) {
    if (page_count <= object->frame_capacity) return true;
    if (page_count > MAX_MEMFD_SIZE / VM_PAGE_SIZE) return false;
    size_t capacity = object->frame_capacity ? object->frame_capacity : 4;
    while (capacity < page_count) {
        if (capacity > MAX_MEMFD_SIZE / VM_PAGE_SIZE / 2) {
            capacity = MAX_MEMFD_SIZE / VM_PAGE_SIZE;
            break;
        }
        capacity *= 2;
    }
    VmFrame **frames = heap_calloc(capacity, sizeof(*frames));
    if (!frames) return false;
    if (object->frames) {
        memcpy(frames, object->frames,
               object->frame_capacity * sizeof(*frames));
        heap_free(object->frames);
    }
    object->frames = frames;
    object->frame_capacity = capacity;
    return true;
}

static bool memfd_set_size(MemfdObject *object, uint64_t size) {
    if (!object || size > MAX_MEMFD_SIZE) return false;
    size_t page_count = (size_t)((size + VM_PAGE_SIZE - 1) / VM_PAGE_SIZE);
    if (!memfd_ensure_capacity(object, page_count)) return false;
    size_t old_page_count = (size_t)((object->size + VM_PAGE_SIZE - 1) / VM_PAGE_SIZE);
    if (page_count < old_page_count) {
        for (size_t page = page_count; page < old_page_count; ++page) {
            if (!object->frames[page]) continue;
            vm_frame_release(object->frames[page]);
            object->frames[page] = NULL;
        }
    }
    if (size && (size & (VM_PAGE_SIZE - 1)) && page_count &&
        object->frames[page_count - 1]) {
        size_t tail = (size_t)(size & (VM_PAGE_SIZE - 1));
        memset(object->frames[page_count - 1]->data + tail, 0,
               VM_PAGE_SIZE - tail);
    }
    object->size = size;
    return true;
}

static VmFrame *memfd_frame(MemfdObject *object, size_t page, bool create) {
    if (!object || page >= object->frame_capacity) return NULL;
    if (!object->frames[page] && create) {
        object->frames[page] = vm_frame_create();
    }
    return object->frames[page];
}

static uint64_t linux_memfd_create(Process *process, uintptr_t name_address, int flags) {
    char name[FS_NAME_MAX + 1];
    if ((flags & ~3) || !copy_user_string(process, name_address,
                                          name, sizeof(name)) || !name[0]) {
        return linux_error(LINUX_EINVAL);
    }
    int object = allocate_memfd_object();
    int descriptor = find_descriptor(process);
    if (object < 0 || descriptor < 0) {
        if (object >= 0) memfd_release_object(object);
        return linux_error(LINUX_ENFILE);
    }
    FileDescriptor *file = &process->descriptors[descriptor];
    memset(file, 0, sizeof(*file));
    file->used = true;
    file->type = FD_MEMFD;
    file->object = object;
    file->flags = flags;
    strncpy(file->name, name, sizeof(file->name) - 1);
    return descriptor;
}

static uint64_t linux_ftruncate(Process *process, int descriptor, uint64_t size) {
    if (descriptor < 3 || descriptor >= MAX_FILE_DESCRIPTORS ||
        !process->descriptors[descriptor].used) return linux_error(LINUX_EBADF);
    FileDescriptor *file = &process->descriptors[descriptor];
    if (file->type != FD_MEMFD || file->object < 0 ||
        file->object >= MAX_MEMFD_OBJECTS || !memfd_objects[file->object].used) {
        return linux_error(LINUX_EINVAL);
    }
    return memfd_set_size(&memfd_objects[file->object], size) ?
           0 : linux_error(LINUX_EINVAL);
}

static uint64_t memfd_read_user(Process *process, FileDescriptor *file,
                                uintptr_t buffer, size_t count) {
    MemfdObject *object = &memfd_objects[file->object];
    if (file->offset >= object->size) return 0;
    uint64_t available = object->size - file->offset;
    if ((uint64_t)count > available) count = (size_t)available;
    uint8_t zeros[512];
    memset(zeros, 0, sizeof(zeros));
    size_t completed = 0;
    while (completed < count) {
        uint64_t position = file->offset + completed;
        size_t page = (size_t)(position / VM_PAGE_SIZE);
        size_t in_page = (size_t)(position & (VM_PAGE_SIZE - 1));
        size_t chunk = count - completed;
        if (chunk > VM_PAGE_SIZE - in_page) chunk = VM_PAGE_SIZE - in_page;
        if (chunk > sizeof(zeros)) chunk = sizeof(zeros);
        VmFrame *frame = memfd_frame(object, page, false);
        const void *source = frame ? frame->data + in_page : zeros;
        if (!vm_copy_to_user(&process->address_space->vm, buffer + completed,
                             source, chunk)) {
            return completed ? completed : linux_error(LINUX_EFAULT);
        }
        completed += chunk;
    }
    file->offset += completed;
    return completed;
}

static uint64_t memfd_write_user(Process *process, FileDescriptor *file,
                                 uintptr_t buffer, size_t count) {
    if ((uint64_t)count > MAX_MEMFD_SIZE || file->offset > MAX_MEMFD_SIZE - count) {
        return linux_error(LINUX_EINVAL);
    }
    MemfdObject *object = &memfd_objects[file->object];
    uint64_t end = file->offset + count;
    if (end > object->size && !memfd_set_size(object, end)) {
        return linux_error(LINUX_ENOMEM);
    }
    uint8_t contents[512];
    size_t completed = 0;
    while (completed < count) {
        uint64_t position = file->offset + completed;
        size_t page = (size_t)(position / VM_PAGE_SIZE);
        size_t in_page = (size_t)(position & (VM_PAGE_SIZE - 1));
        size_t chunk = count - completed;
        if (chunk > VM_PAGE_SIZE - in_page) chunk = VM_PAGE_SIZE - in_page;
        if (chunk > sizeof(contents)) chunk = sizeof(contents);
        if (!vm_copy_from_user(&process->address_space->vm, contents,
                               buffer + completed, chunk)) {
            return completed ? completed : linux_error(LINUX_EFAULT);
        }
        VmFrame *frame = memfd_frame(object, page, true);
        if (!frame) return completed ? completed : linux_error(LINUX_ENOMEM);
        memcpy(frame->data + in_page, contents, chunk);
        completed += chunk;
    }
    file->offset += completed;
    return completed;
}

static bool timespec_to_ticks(const LinuxTimespec *time, uint64_t *ticks) {
    if (time->seconds < 0 || time->nanoseconds < 0 ||
        time->nanoseconds >= 1000000000 ||
        (uint64_t)time->seconds > ((uint64_t)-1 - 99) / 100) return false;
    *ticks = (uint64_t)time->seconds * 100 +
             ((uint64_t)time->nanoseconds + 9999999) / 10000000;
    return true;
}

static LinuxTimespec ticks_to_timespec(uint64_t ticks) {
    LinuxTimespec time;
    time.seconds = (int64_t)(ticks / 100);
    time.nanoseconds = (int64_t)(ticks % 100) * 10000000;
    return time;
}

static uint64_t queue_write_user(Process *process, IpcQueue *queue,
                                 uintptr_t source, size_t count) {
    if (!count) return 0;
    size_t available = IPC_BUFFER_SIZE - queue->count;
    if (!available) return linux_error(LINUX_EAGAIN);
    if (count > available) count = available;
    uint8_t buffer[256];
    size_t completed = 0;
    while (completed < count) {
        size_t chunk = count - completed;
        if (chunk > sizeof(buffer)) chunk = sizeof(buffer);
        if (!vm_copy_from_user(&process->address_space->vm, buffer,
                               source + completed, chunk)) return linux_error(LINUX_EFAULT);
        for (size_t index = 0; index < chunk; ++index) {
            queue->data[queue->write_offset] = buffer[index];
            queue->write_offset = (queue->write_offset + 1) % IPC_BUFFER_SIZE;
        }
        queue->count += (uint32_t)chunk;
        completed += chunk;
    }
    return count;
}

static uint64_t queue_read_user(Process *process, IpcQueue *queue,
                                uintptr_t destination, size_t count) {
    if (!count) return 0;
    if (!queue->count) return linux_error(LINUX_EAGAIN);
    if (count > queue->count) count = queue->count;
    uint8_t buffer[256];
    size_t completed = 0;
    while (completed < count) {
        size_t chunk = count - completed;
        if (chunk > sizeof(buffer)) chunk = sizeof(buffer);
        for (size_t index = 0; index < chunk; ++index) {
            buffer[index] = queue->data[queue->read_offset];
            queue->read_offset = (queue->read_offset + 1) % IPC_BUFFER_SIZE;
        }
        if (!vm_copy_to_user(&process->address_space->vm, destination + completed,
                             buffer, chunk)) return linux_error(LINUX_EFAULT);
        queue->count -= (uint32_t)chunk;
        completed += chunk;
    }
    return count;
}

static IpcQueue *descriptor_read_queue(FileDescriptor *descriptor) {
    if (descriptor->type == FD_PIPE_READ && descriptor->object >= 0 &&
        descriptor->object < MAX_PIPE_OBJECTS && pipe_objects[descriptor->object].used) {
        return &pipe_objects[descriptor->object].queue;
    }
    if (descriptor->type == FD_UNIX_SOCKET && descriptor->object >= 0 &&
        descriptor->object < MAX_SOCKET_PAIRS && socket_pairs[descriptor->object].used &&
        (descriptor->endpoint == 0 || descriptor->endpoint == 1)) {
        return &socket_pairs[descriptor->object].incoming[descriptor->endpoint];
    }
    return NULL;
}

static IpcQueue *descriptor_write_queue(FileDescriptor *descriptor) {
    if (descriptor->type == FD_PIPE_WRITE && descriptor->object >= 0 &&
        descriptor->object < MAX_PIPE_OBJECTS && pipe_objects[descriptor->object].used) {
        return &pipe_objects[descriptor->object].queue;
    }
    if (descriptor->type == FD_UNIX_SOCKET && descriptor->object >= 0 &&
        descriptor->object < MAX_SOCKET_PAIRS && socket_pairs[descriptor->object].used &&
        (descriptor->endpoint == 0 || descriptor->endpoint == 1)) {
        return &socket_pairs[descriptor->object].incoming[1 - descriptor->endpoint];
    }
    return NULL;
}

static uint64_t linux_openat(Process *process, uintptr_t path_address, int flags) {
    char path[FS_NAME_MAX + 2];
    char name[FS_NAME_MAX + 1];
    if (!copy_user_string(process, path_address, path, sizeof(path)) ||
        !normalize_path(path, name)) return linux_error(LINUX_EFAULT);
    FsFileInfo info;
    bool root = !name[0];
    bool exists = root || file_info(name, &info);
    if (!exists && !(flags & LINUX_O_CREAT)) return linux_error(LINUX_ENOENT);
    if (root && (flags & LINUX_O_CREAT)) return linux_error(LINUX_EISDIR);
    if (!exists && !fs_write(name, "", 0)) return linux_error(LINUX_EACCES);
    if (!root && !file_info(name, &info)) return linux_error(LINUX_EIO);
    bool directory = root || info.type == FS_TYPE_DIRECTORY;
    if ((flags & LINUX_O_DIRECTORY) && !directory) return linux_error(LINUX_ENOTDIR);
    if (directory && (flags & (LINUX_O_WRONLY | LINUX_O_RDWR | LINUX_O_TRUNC))) {
        return linux_error(LINUX_EISDIR);
    }
    if (exists && (flags & LINUX_O_TRUNC) && !fs_write(name, "", 0)) {
        return linux_error(LINUX_EACCES);
    }
    int descriptor = find_descriptor(process);
    if (descriptor < 0) return linux_error(LINUX_ENFILE);
    FileDescriptor *file = &process->descriptors[descriptor];
    memset(file, 0, sizeof(*file));
    file->used = true;
    file->type = directory ? FD_DIRECTORY : FD_FILE;
    file->flags = flags;
    strcpy(file->name, name);
    if ((flags & LINUX_O_APPEND) && file_info(name, &info)) file->offset = info.size;
    return (uint64_t)descriptor;
}

static uint8_t directory_entry_type(uint8_t type) {
    if (type == FS_TYPE_DIRECTORY) return 4;
    if (type == FS_TYPE_SYMLINK) return 10;
    return 8;
}

static bool immediate_child_name(const char *directory, const char *path,
                                 char child[FS_NAME_MAX + 1]) {
    const char *relative = path;
    if (*directory) {
        size_t prefix = strlen(directory);
        if (strncmp(path, directory, prefix) || path[prefix] != '/') return false;
        relative = path + prefix + 1;
    }
    if (!*relative) return false;
    for (const char *cursor = relative; *cursor; ++cursor) {
        if (*cursor == '/') return false;
    }
    strcpy(child, relative);
    return true;
}

static uint64_t linux_getdents64(Process *process, int descriptor,
                                 uintptr_t buffer, size_t capacity) {
    if (descriptor < 3 || descriptor >= MAX_FILE_DESCRIPTORS ||
        !process->descriptors[descriptor].used) return linux_error(LINUX_EBADF);
    FileDescriptor *directory = &process->descriptors[descriptor];
    if (directory->type != FD_DIRECTORY) return linux_error(LINUX_ENOTDIR);
    size_t written = 0;
    int total = fs_file_count();
    for (int index = (int)directory->offset; index < total; ++index) {
        FsFileInfo info;
        char child[FS_NAME_MAX + 1];
        directory->offset = (uint32_t)(index + 1);
        if (!fs_file_info(index, &info) ||
            !immediate_child_name(directory->name, info.name, child)) continue;
        size_t name_length = strlen(child) + 1;
        uint16_t record_length = (uint16_t)((19 + name_length + 7) & ~7UL);
        if (written + record_length > capacity) {
            directory->offset = (uint32_t)index;
            if (!written) return linux_error(LINUX_EINVAL);
            break;
        }
        uint8_t record[64];
        memset(record, 0, sizeof(record));
        uint64_t inode = (uint64_t)index + 2;
        int64_t next = index + 1;
        memcpy(record, &inode, sizeof(inode));
        memcpy(record + 8, &next, sizeof(next));
        memcpy(record + 16, &record_length, sizeof(record_length));
        record[18] = directory_entry_type(info.type);
        memcpy(record + 19, child, name_length);
        if (!vm_copy_to_user(&process->address_space->vm, buffer + written,
                             record, record_length)) return linux_error(LINUX_EFAULT);
        written += record_length;
    }
    return written;
}

static bool normalized_user_path(Process *process, uintptr_t address,
                                 char output[FS_NAME_MAX + 1]) {
    char path[FS_NAME_MAX + 2];
    return copy_user_string(process, address, path, sizeof(path)) &&
           normalize_path(path, output);
}

static uint64_t linux_mkdir_path(Process *process, uintptr_t path_address,
                                 uint16_t mode) {
    char name[FS_NAME_MAX + 1];
    if (!normalized_user_path(process, path_address, name)) return linux_error(LINUX_EFAULT);
    if (!name[0]) return linux_error(LINUX_EEXIST);
    FsFileInfo info;
    if (fs_path_info(name, &info, false)) return linux_error(LINUX_EEXIST);
    return fs_mkdir(name, mode & 0777) ? 0 : linux_error(LINUX_ENOENT);
}

static uint64_t linux_symlink_path(Process *process, uintptr_t target_address,
                                   uintptr_t link_address) {
    char target[FS_NAME_MAX + 2];
    char name[FS_NAME_MAX + 1];
    if (!copy_user_string(process, target_address, target, sizeof(target)) ||
        !normalized_user_path(process, link_address, name)) return linux_error(LINUX_EFAULT);
    if (!target[0] || !name[0]) return linux_error(LINUX_EINVAL);
    FsFileInfo info;
    if (fs_path_info(name, &info, false)) return linux_error(LINUX_EEXIST);
    return fs_symlink(target, name) ? 0 : linux_error(LINUX_ENOENT);
}

static uint64_t linux_readlink_path(Process *process, uintptr_t path_address,
                                    uintptr_t buffer, size_t capacity) {
    char name[FS_NAME_MAX + 1];
    char target[FS_NAME_MAX + 1];
    if (!capacity) return linux_error(LINUX_EINVAL);
    if (!normalized_user_path(process, path_address, name)) return linux_error(LINUX_EFAULT);
    size_t wanted = capacity < FS_NAME_MAX ? capacity : FS_NAME_MAX;
    int length = fs_readlink(name, target, wanted);
    if (length < 0) return linux_error(LINUX_EINVAL);
    return vm_copy_to_user(&process->address_space->vm, buffer, target, (size_t)length) ?
           (uint64_t)length : linux_error(LINUX_EFAULT);
}

static uint64_t linux_unlink_path(Process *process, uintptr_t path_address,
                                  int flags) {
    char name[FS_NAME_MAX + 1];
    if (!normalized_user_path(process, path_address, name)) return linux_error(LINUX_EFAULT);
    if (!name[0]) return linux_error(LINUX_EBUSY);
    FsFileInfo info;
    if (!fs_path_info(name, &info, false)) return linux_error(LINUX_ENOENT);
    bool directory = info.type == FS_TYPE_DIRECTORY;
    if (directory && !(flags & LINUX_AT_REMOVEDIR)) return linux_error(LINUX_EISDIR);
    if (!directory && (flags & LINUX_AT_REMOVEDIR)) return linux_error(LINUX_ENOTDIR);
    return fs_delete(name) ? 0 : linux_error(directory ? LINUX_ENOTEMPTY : LINUX_EACCES);
}

static uint64_t linux_read(Process *process, int descriptor, uintptr_t buffer, size_t count) {
    if (descriptor < 3 || descriptor >= MAX_FILE_DESCRIPTORS ||
        !process->descriptors[descriptor].used) return linux_error(LINUX_EBADF);
    FileDescriptor *file = &process->descriptors[descriptor];
    if (file->type == FD_MEMFD && file->object >= 0 &&
        file->object < MAX_MEMFD_OBJECTS && memfd_objects[file->object].used) {
        return memfd_read_user(process, file, buffer, count);
    }
    if (file->type == FD_EVENTFD) {
        if (count < sizeof(uint64_t) || file->object < 0 ||
            file->object >= MAX_EVENT_OBJECTS ||
            !event_objects[file->object].used) return linux_error(LINUX_EINVAL);
        EventObject *event = &event_objects[file->object];
        if (!event->counter) return linux_error(LINUX_EAGAIN);
        uint64_t value = event->semaphore ? 1 : event->counter;
        if (!vm_copy_to_user(&process->address_space->vm, buffer,
                             &value, sizeof(value))) return linux_error(LINUX_EFAULT);
        if (event->semaphore) --event->counter;
        else event->counter = 0;
        return sizeof(value);
    }
    if (file->type == FD_TIMERFD) {
        if (count < sizeof(uint64_t) || file->object < 0 ||
            file->object >= MAX_TIMER_OBJECTS ||
            !timer_objects[file->object].used) return linux_error(LINUX_EINVAL);
        TimerObject *timer = &timer_objects[file->object];
        if (!timer->expires || scheduler_ticks < timer->expires) {
            return linux_error(LINUX_EAGAIN);
        }
        uint64_t expirations = 1;
        if (timer->interval) {
            expirations += (scheduler_ticks - timer->expires) / timer->interval;
            timer->expires += expirations * timer->interval;
        } else {
            timer->expires = 0;
        }
        if (!vm_copy_to_user(&process->address_space->vm, buffer,
                             &expirations, sizeof(expirations))) {
            return linux_error(LINUX_EFAULT);
        }
        return sizeof(expirations);
    }
    IpcQueue *queue = descriptor_read_queue(file);
    if (queue) return queue_read_user(process, queue, buffer, count);
    if (file->type != FD_FILE) return linux_error(LINUX_EBADF);
    if ((file->flags & 3) == LINUX_O_WRONLY) return linux_error(LINUX_EBADF);
    FsFileInfo info;
    if (!file_info(file->name, &info)) return linux_error(LINUX_ENOENT);
    if (file->offset >= info.size) return 0;
    uint64_t available = info.size - file->offset;
    if ((uint64_t)count > available) count = (size_t)available;
    uint8_t contents[512];
    size_t completed = 0;
    while (completed < count) {
        size_t chunk = count - completed;
        if (chunk > sizeof(contents)) chunk = sizeof(contents);
        int loaded = fs_read_at(file->name, file->offset + completed, contents, chunk);
        if (loaded <= 0 || !vm_copy_to_user(&process->address_space->vm,
                                             buffer + completed, contents, (size_t)loaded)) {
            return completed ? completed : linux_error(LINUX_EFAULT);
        }
        completed += (size_t)loaded;
    }
    file->offset += completed;
    return completed;
}

static uint64_t linux_write_file(Process *process, int descriptor,
                                 uintptr_t buffer, size_t count) {
    if (descriptor < 3 || descriptor >= MAX_FILE_DESCRIPTORS ||
        !process->descriptors[descriptor].used) return linux_error(LINUX_EBADF);
    FileDescriptor *file = &process->descriptors[descriptor];
    if (file->type == FD_MEMFD && file->object >= 0 &&
        file->object < MAX_MEMFD_OBJECTS && memfd_objects[file->object].used) {
        return memfd_write_user(process, file, buffer, count);
    }
    if (file->type == FD_EVENTFD) {
        if (count < sizeof(uint64_t) || file->object < 0 ||
            file->object >= MAX_EVENT_OBJECTS ||
            !event_objects[file->object].used) return linux_error(LINUX_EINVAL);
        uint64_t value;
        if (!vm_copy_from_user(&process->address_space->vm, &value,
                               buffer, sizeof(value))) return linux_error(LINUX_EFAULT);
        EventObject *event = &event_objects[file->object];
        if (value == (uint64_t)-1) return linux_error(LINUX_EINVAL);
        if (event->counter > (uint64_t)-2 - value) return linux_error(LINUX_EAGAIN);
        event->counter += value;
        return sizeof(value);
    }
    IpcQueue *queue = descriptor_write_queue(file);
    if (queue) return queue_write_user(process, queue, buffer, count);
    if (file->type != FD_FILE) return linux_error(LINUX_EBADF);
    if ((file->flags & 3) == 0) return linux_error(LINUX_EBADF);
    FsFileInfo info;
    if (file->flags & LINUX_O_APPEND) {
        file->offset = file_info(file->name, &info) ? info.size : 0;
    }
    uint8_t contents[512];
    size_t completed = 0;
    while (completed < count) {
        size_t chunk = count - completed;
        if (chunk > sizeof(contents)) chunk = sizeof(contents);
        if (!vm_copy_from_user(&process->address_space->vm, contents,
                               buffer + completed, chunk) ||
            !fs_write_at(file->name, file->offset + completed, contents, chunk)) {
            return completed ? completed : linux_error(LINUX_EFAULT);
        }
        completed += chunk;
    }
    file->offset += completed;
    return completed;
}

static uint64_t linux_vector_io(Process *process, int descriptor,
                                uintptr_t vectors, int count, bool writing) {
    if (count < 0 || count > 1024) return linux_error(LINUX_EINVAL);
    uint64_t total = 0;
    for (int index = 0; index < count; ++index) {
        LinuxIovec vector;
        if (!vm_copy_from_user(&process->address_space->vm, &vector,
                vectors + (uintptr_t)index * sizeof(vector), sizeof(vector))) {
            return total ? total : linux_error(LINUX_EFAULT);
        }
        if (vector.length > 0x7FFFFFFFUL || total + vector.length < total) {
            return total ? total : linux_error(LINUX_EINVAL);
        }
        uint64_t result;
        if (writing && (descriptor == 1 || descriptor == 2)) {
            result = write_console(process, vector.base, (size_t)vector.length);
        } else if (writing) {
            result = linux_write_file(process, descriptor, vector.base,
                                      (size_t)vector.length);
        } else {
            result = linux_read(process, descriptor, vector.base, (size_t)vector.length);
        }
        if ((int64_t)result < 0) return total ? total : result;
        total += result;
        if (result < vector.length) break;
    }
    return total;
}

static uint64_t linux_positioned_io(Process *process, int descriptor,
                                    uintptr_t buffer, size_t count,
                                    uint64_t offset, bool writing) {
    if (descriptor < 3 || descriptor >= MAX_FILE_DESCRIPTORS ||
        !process->descriptors[descriptor].used) return linux_error(LINUX_EBADF);
    FileDescriptor *file = &process->descriptors[descriptor];
    uint64_t saved = file->offset;
    file->offset = offset;
    uint64_t result = writing ? linux_write_file(process, descriptor, buffer, count) :
                                linux_read(process, descriptor, buffer, count);
    file->offset = saved;
    return result;
}

static uint64_t linux_duplicate_descriptor(Process *process, int old_descriptor,
                                           int new_descriptor, bool exact) {
    if (old_descriptor < 3 || old_descriptor >= MAX_FILE_DESCRIPTORS ||
        !process->descriptors[old_descriptor].used) return linux_error(LINUX_EBADF);
    if (!exact) new_descriptor = find_descriptor(process);
    if (new_descriptor < 3 || new_descriptor >= MAX_FILE_DESCRIPTORS) {
        return linux_error(LINUX_EBADF);
    }
    if (new_descriptor == old_descriptor) return old_descriptor;
    if (process->descriptors[new_descriptor].used) {
        descriptor_release_resources(&process->descriptors[new_descriptor]);
    }
    process->descriptors[new_descriptor] = process->descriptors[old_descriptor];
    descriptor_retain_resources(&process->descriptors[new_descriptor]);
    return (uint64_t)new_descriptor;
}

static uint64_t linux_access_path(Process *process, uintptr_t path_address) {
    char path[FS_NAME_MAX + 2];
    char name[FS_NAME_MAX + 1];
    FsFileInfo info;
    if (!copy_user_string(process, path_address, path, sizeof(path)) ||
        !normalize_path(path, name)) return linux_error(LINUX_EFAULT);
    return (!name[0] || file_info(name, &info)) ? 0 : linux_error(LINUX_ENOENT);
}

static uint64_t linux_ioctl(Process *process, int descriptor,
                            uint64_t request, uintptr_t argument) {
    if (descriptor == 0 || descriptor == 1 || descriptor == 2) {
        if (request == 0x5413 && argument) {
            uint16_t window[4] = {48, 160, 0, 0};
            return vm_copy_to_user(&process->address_space->vm, argument,
                                   window, sizeof(window)) ? 0 : linux_error(LINUX_EFAULT);
        }
        if (request == 0x5401 || request == 0x540F || request == 0x541B) return 0;
    }
    return linux_error(LINUX_ENOTTY);
}

static uint64_t linux_pipe2(Process *process, uintptr_t result_address) {
    int object = allocate_pipe_object();
    int first = find_descriptor(process);
    if (object < 0 || first < 0) return linux_error(LINUX_ENFILE);
    process->descriptors[first].used = true;
    int second = find_descriptor(process);
    if (second < 0) {
        memset(&process->descriptors[first], 0, sizeof(process->descriptors[first]));
        pipe_objects[object].used = false;
        return linux_error(LINUX_ENFILE);
    }
    FileDescriptor *read_end = &process->descriptors[first];
    FileDescriptor *write_end = &process->descriptors[second];
    memset(read_end, 0, sizeof(*read_end));
    memset(write_end, 0, sizeof(*write_end));
    read_end->used = true;
    read_end->type = FD_PIPE_READ;
    read_end->object = object;
    write_end->used = true;
    write_end->type = FD_PIPE_WRITE;
    write_end->object = object;
    int descriptors[2] = {first, second};
    if (!vm_copy_to_user(&process->address_space->vm, result_address,
                         descriptors, sizeof(descriptors))) {
        memset(read_end, 0, sizeof(*read_end));
        memset(write_end, 0, sizeof(*write_end));
        pipe_objects[object].used = false;
        return linux_error(LINUX_EFAULT);
    }
    return 0;
}

static uint64_t linux_socketpair(Process *process, int domain, int type,
                                 uintptr_t result_address) {
    if (domain != 1 || (type & 0xF) != 1) return linux_error(LINUX_EINVAL);
    int object = allocate_socket_pair();
    int first = find_descriptor(process);
    if (object < 0 || first < 0) return linux_error(LINUX_ENFILE);
    process->descriptors[first].used = true;
    int second = find_descriptor(process);
    if (second < 0) {
        memset(&process->descriptors[first], 0, sizeof(process->descriptors[first]));
        socket_pairs[object].used = false;
        return linux_error(LINUX_ENFILE);
    }
    FileDescriptor *left = &process->descriptors[first];
    FileDescriptor *right = &process->descriptors[second];
    memset(left, 0, sizeof(*left));
    memset(right, 0, sizeof(*right));
    left->used = true;
    left->type = FD_UNIX_SOCKET;
    left->object = object;
    left->endpoint = 0;
    right->used = true;
    right->type = FD_UNIX_SOCKET;
    right->object = object;
    right->endpoint = 1;
    int descriptors[2] = {first, second};
    if (!vm_copy_to_user(&process->address_space->vm, result_address,
                         descriptors, sizeof(descriptors))) {
        memset(left, 0, sizeof(*left));
        memset(right, 0, sizeof(*right));
        socket_pairs[object].used = false;
        return linux_error(LINUX_EFAULT);
    }
    return 0;
}

static uint32_t descriptor_ready_events(Process *process, int descriptor,
                                        uint32_t requested) {
    if (descriptor < 0 || descriptor >= MAX_FILE_DESCRIPTORS ||
        !process->descriptors[descriptor].used) return 0;
    FileDescriptor *file = &process->descriptors[descriptor];
    uint32_t ready = 0;
    IpcQueue *read_queue = descriptor_read_queue(file);
    IpcQueue *write_queue = descriptor_write_queue(file);
    if (file->type == FD_EVENTFD && file->object >= 0 &&
        file->object < MAX_EVENT_OBJECTS && event_objects[file->object].used) {
        EventObject *event = &event_objects[file->object];
        if ((requested & LINUX_EPOLLIN) && event->counter) ready |= LINUX_EPOLLIN;
        if ((requested & LINUX_EPOLLOUT) && event->counter < (uint64_t)-2) {
            ready |= LINUX_EPOLLOUT;
        }
    }
    if (file->type == FD_TIMERFD && file->object >= 0 &&
        file->object < MAX_TIMER_OBJECTS && timer_objects[file->object].used &&
        (requested & LINUX_EPOLLIN)) {
        TimerObject *timer = &timer_objects[file->object];
        if (timer->expires && scheduler_ticks >= timer->expires) {
            ready |= LINUX_EPOLLIN;
        }
    }
    if ((requested & LINUX_EPOLLIN) &&
        ((read_queue && read_queue->count) || file->type == FD_FILE ||
         file->type == FD_MEMFD)) ready |= LINUX_EPOLLIN;
    if ((requested & LINUX_EPOLLOUT) &&
        ((write_queue && write_queue->count < IPC_BUFFER_SIZE) || file->type == FD_FILE ||
         file->type == FD_MEMFD)) {
        ready |= LINUX_EPOLLOUT;
    }
    return ready;
}

static uint64_t linux_poll(Process *process, uintptr_t descriptors,
                           uint64_t descriptor_count) {
    if (descriptor_count > MAX_FILE_DESCRIPTORS) return linux_error(LINUX_EINVAL);
    uint64_t ready_count = 0;
    for (uint64_t index = 0; index < descriptor_count; ++index) {
        LinuxPollFd poll_descriptor;
        uintptr_t address = descriptors + index * sizeof(poll_descriptor);
        if (!vm_copy_from_user(&process->address_space->vm, &poll_descriptor,
                               address, sizeof(poll_descriptor))) {
            return linux_error(LINUX_EFAULT);
        }
        poll_descriptor.returned_events = 0;
        if (poll_descriptor.descriptor >= 0) {
            if (poll_descriptor.descriptor >= MAX_FILE_DESCRIPTORS ||
                !process->descriptors[poll_descriptor.descriptor].used) {
                poll_descriptor.returned_events = 0x20;
            } else {
                poll_descriptor.returned_events = (int16_t)descriptor_ready_events(
                    process, poll_descriptor.descriptor,
                    (uint16_t)poll_descriptor.events);
            }
        }
        if (poll_descriptor.returned_events) ++ready_count;
        if (!vm_copy_to_user(&process->address_space->vm, address,
                             &poll_descriptor, sizeof(poll_descriptor))) {
            return linux_error(LINUX_EFAULT);
        }
    }
    return ready_count;
}

static uint64_t linux_epoll_create(Process *process) {
    int object = allocate_epoll_object();
    int descriptor = find_descriptor(process);
    if (object < 0 || descriptor < 0) return linux_error(LINUX_ENFILE);
    FileDescriptor *file = &process->descriptors[descriptor];
    memset(file, 0, sizeof(*file));
    file->used = true;
    file->type = FD_EPOLL;
    file->object = object;
    return descriptor;
}

static uint64_t linux_eventfd2(Process *process, uint64_t initial_value, int flags) {
    if (initial_value > 0xFFFFFFFFUL || (flags & ~0x80801)) {
        return linux_error(LINUX_EINVAL);
    }
    int object = allocate_event_object();
    int descriptor = find_descriptor(process);
    if (object < 0 || descriptor < 0) return linux_error(LINUX_ENFILE);
    EventObject *event = &event_objects[object];
    event->counter = initial_value;
    event->semaphore = (flags & 1) != 0;
    FileDescriptor *file = &process->descriptors[descriptor];
    memset(file, 0, sizeof(*file));
    file->used = true;
    file->type = FD_EVENTFD;
    file->object = object;
    file->flags = flags;
    return descriptor;
}

static uint64_t linux_timerfd_create(Process *process, int clock_id, int flags) {
    if ((clock_id != 0 && clock_id != 1) || (flags & ~0x80800)) {
        return linux_error(LINUX_EINVAL);
    }
    int object = allocate_timer_object();
    int descriptor = find_descriptor(process);
    if (object < 0 || descriptor < 0) return linux_error(LINUX_ENFILE);
    FileDescriptor *file = &process->descriptors[descriptor];
    memset(file, 0, sizeof(*file));
    file->used = true;
    file->type = FD_TIMERFD;
    file->object = object;
    file->flags = flags;
    return descriptor;
}

static LinuxItimerspec timerfd_current_spec(const TimerObject *timer) {
    LinuxItimerspec current;
    uint64_t remaining = timer->expires > scheduler_ticks ?
                         timer->expires - scheduler_ticks : 0;
    current.value = ticks_to_timespec(remaining);
    current.interval = ticks_to_timespec(timer->interval);
    return current;
}

static uint64_t linux_timerfd_settime(Process *process, int descriptor, int flags,
                                      uintptr_t new_address, uintptr_t old_address) {
    if (descriptor < 3 || descriptor >= MAX_FILE_DESCRIPTORS ||
        !process->descriptors[descriptor].used ||
        process->descriptors[descriptor].type != FD_TIMERFD || (flags & ~1)) {
        return linux_error(LINUX_EINVAL);
    }
    TimerObject *timer = &timer_objects[process->descriptors[descriptor].object];
    LinuxItimerspec requested;
    if (!new_address || !vm_copy_from_user(&process->address_space->vm, &requested,
                                           new_address, sizeof(requested))) {
        return linux_error(LINUX_EFAULT);
    }
    uint64_t value_ticks;
    uint64_t interval_ticks;
    if (!timespec_to_ticks(&requested.value, &value_ticks) ||
        !timespec_to_ticks(&requested.interval, &interval_ticks)) {
        return linux_error(LINUX_EINVAL);
    }
    if (old_address) {
        LinuxItimerspec previous = timerfd_current_spec(timer);
        if (!vm_copy_to_user(&process->address_space->vm, old_address,
                             &previous, sizeof(previous))) return linux_error(LINUX_EFAULT);
    }
    timer->interval = interval_ticks;
    if (!value_ticks) timer->expires = 0;
    else if (flags & 1) timer->expires = value_ticks;
    else {
        if (scheduler_ticks > (uint64_t)-1 - value_ticks) {
            return linux_error(LINUX_EINVAL);
        }
        timer->expires = scheduler_ticks + value_ticks;
    }
    return 0;
}

static uint64_t linux_timerfd_gettime(Process *process, int descriptor,
                                      uintptr_t current_address) {
    if (descriptor < 3 || descriptor >= MAX_FILE_DESCRIPTORS ||
        !process->descriptors[descriptor].used ||
        process->descriptors[descriptor].type != FD_TIMERFD) {
        return linux_error(LINUX_EINVAL);
    }
    TimerObject *timer = &timer_objects[process->descriptors[descriptor].object];
    LinuxItimerspec current = timerfd_current_spec(timer);
    return vm_copy_to_user(&process->address_space->vm, current_address,
                           &current, sizeof(current)) ? 0 : linux_error(LINUX_EFAULT);
}

static uint64_t linux_epoll_ctl(Process *process, int epoll_descriptor, int operation,
                                int target_descriptor, uintptr_t event_address) {
    if (epoll_descriptor < 3 || epoll_descriptor >= MAX_FILE_DESCRIPTORS ||
        !process->descriptors[epoll_descriptor].used ||
        process->descriptors[epoll_descriptor].type != FD_EPOLL ||
        target_descriptor < 0 || target_descriptor >= MAX_FILE_DESCRIPTORS ||
        !process->descriptors[target_descriptor].used) return linux_error(LINUX_EBADF);
    EpollObject *epoll = &epoll_objects[process->descriptors[epoll_descriptor].object];
    int existing = -1;
    int available = -1;
    for (int index = 0; index < MAX_EPOLL_ENTRIES; ++index) {
        if (epoll->entries[index].used &&
            epoll->entries[index].descriptor == target_descriptor) existing = index;
        if (!epoll->entries[index].used && available < 0) available = index;
    }
    if (operation == LINUX_EPOLL_CTL_DEL) {
        if (existing < 0) return linux_error(LINUX_ENOENT);
        memset(&epoll->entries[existing], 0, sizeof(epoll->entries[existing]));
        return 0;
    }
    LinuxEpollEvent event;
    if (!event_address || !vm_copy_from_user(&process->address_space->vm, &event,
                                             event_address, sizeof(event))) {
        return linux_error(LINUX_EFAULT);
    }
    int slot = operation == LINUX_EPOLL_CTL_ADD ? available : existing;
    if (operation == LINUX_EPOLL_CTL_ADD && existing >= 0) return linux_error(LINUX_EEXIST);
    if ((operation != LINUX_EPOLL_CTL_ADD && operation != LINUX_EPOLL_CTL_MOD) || slot < 0) {
        return linux_error(LINUX_EINVAL);
    }
    epoll->entries[slot].used = true;
    epoll->entries[slot].descriptor = target_descriptor;
    epoll->entries[slot].event = event;
    return 0;
}

static uint64_t linux_epoll_wait(Process *process, int epoll_descriptor,
                                 uintptr_t events_address, int maximum_events) {
    if (epoll_descriptor < 3 || epoll_descriptor >= MAX_FILE_DESCRIPTORS ||
        !process->descriptors[epoll_descriptor].used ||
        process->descriptors[epoll_descriptor].type != FD_EPOLL || maximum_events <= 0) {
        return linux_error(LINUX_EINVAL);
    }
    EpollObject *epoll = &epoll_objects[process->descriptors[epoll_descriptor].object];
    int count = 0;
    for (int index = 0; index < MAX_EPOLL_ENTRIES && count < maximum_events; ++index) {
        EpollEntry *entry = &epoll->entries[index];
        if (!entry->used) continue;
        uint32_t ready = descriptor_ready_events(process, entry->descriptor,
                                                 entry->event.events);
        if (!ready) continue;
        LinuxEpollEvent result = entry->event;
        result.events = ready;
        if (!vm_copy_to_user(&process->address_space->vm,
                             events_address + (uintptr_t)count * sizeof(result),
                             &result, sizeof(result))) return linux_error(LINUX_EFAULT);
        ++count;
    }
    return count;
}

static bool stat_for_name(const char *name, LinuxStat *stat) {
    FsFileInfo info;
    bool root = !name[0];
    if (!root && !file_info(name, &info)) return false;
    memset(stat, 0, sizeof(*stat));
    stat->inode = 1;
    stat->links = 1;
    uint32_t type_mode = root || info.type == FS_TYPE_DIRECTORY ? 0040000U :
                         info.type == FS_TYPE_SYMLINK ? 0120000U : 0100000U;
    stat->mode = type_mode | (root ? 0755U : (info.flags & 0777U));
    stat->uid = root ? 0 : info.owner;
    stat->size = root ? 0 : info.size;
    stat->block_size = 512;
    stat->blocks = (info.size + 511) / 512;
    return true;
}

static bool stat_for_memfd(const FileDescriptor *file, LinuxStat *stat) {
    if (!file || file->type != FD_MEMFD || file->object < 0 ||
        file->object >= MAX_MEMFD_OBJECTS || !memfd_objects[file->object].used) {
        return false;
    }
    memset(stat, 0, sizeof(*stat));
    stat->inode = 0x4D454D00UL + (uint64_t)file->object;
    stat->links = 1;
    stat->mode = 0100600U;
    stat->uid = 1000;
    stat->size = (int64_t)memfd_objects[file->object].size;
    stat->block_size = VM_PAGE_SIZE;
    stat->blocks = (stat->size + 511) / 512;
    return true;
}

static void finish_process(Process *process, int code) {
    if (!process) return;
    process->exit_code = code;
    process->state = PROCESS_ZOMBIE;
    if (process->clear_child_tid) {
        uint32_t zero = 0;
        vm_copy_to_user(&process->address_space->vm, process->clear_child_tid,
                        &zero, sizeof(zero));
    }
    for (int index = 0; index < MAX_PROCESSES; ++index) {
        Process *waiter = &processes[index];
        if (waiter->used && waiter->state == PROCESS_BLOCKED_FUTEX &&
            waiter->address_space == process->address_space &&
            waiter->futex_address == process->clear_child_tid) {
            waiter->state = PROCESS_RUNNING;
        }
    }
}

static int clone_current(bool share_memory, uintptr_t child_stack, uint64_t flags,
                         uintptr_t parent_tid, uintptr_t child_tid, uintptr_t tls) {
    Process *parent = current_process();
    int slot = reserve_slot();
    if (!parent || slot < 0) return -1;
    Process *child = &processes[slot];
    memset(child, 0, sizeof(*child));
    __asm__ volatile ("fxsave %0" : "=m"(child->fx_state));
    if (share_memory) {
        child->address_space = parent->address_space;
        child->address_space->references++;
    } else {
        child->address_space = address_space_clone(parent->address_space);
        if (!child->address_space) return -1;
    }
    child->used = true;
    child->state = PROCESS_RUNNING;
    child->pid = next_pid++;
    child->tgid = share_memory ? parent->tgid : child->pid;
    child->parent_pid = parent->pid;
    memcpy(child->context, process_context, sizeof(child->context));
    child->context[CONTEXT_RAX] = 0;
    if (child_stack) child->context[CONTEXT_RSP] = child_stack;
    child->program_break = parent->program_break;
    child->mmap_hint = parent->mmap_hint;
    if ((flags & LINUX_CLONE_SETTLS) && tls == NOVA_AUTO_TLS) {
        if (!allocate_thread_tls(child->address_space, child->mmap_hint,
                                 &child->fs_base, &child->tls_mapping,
                                 &child->tls_mapping_size)) {
            address_space_release(child->address_space);
            memset(child, 0, sizeof(*child));
            return -1;
        }
        child->mmap_hint = child->tls_mapping + child->tls_mapping_size;
        if (share_memory && child->mmap_hint > parent->mmap_hint) {
            parent->mmap_hint = child->mmap_hint;
        }
    } else if (flags & LINUX_CLONE_SETTLS) {
        child->fs_base = tls;
    } else {
        child->fs_base = parent->fs_base;
        child->tls_mapping = parent->tls_mapping;
        child->tls_mapping_size = parent->tls_mapping_size;
    }
    child->gs_base = parent->gs_base;
    child->no_new_privs = parent->no_new_privs;
    child->seccomp_strict = parent->seccomp_strict;
    child->clear_child_tid = (flags & LINUX_CLONE_CHILD_SETTID) ? child_tid : 0;
    memcpy(child->signal_actions, parent->signal_actions, sizeof(child->signal_actions));
    memcpy(child->descriptors, parent->descriptors, sizeof(child->descriptors));
    for (int descriptor = 3; descriptor < MAX_FILE_DESCRIPTORS; ++descriptor) {
        if (child->descriptors[descriptor].used) {
            descriptor_retain_resources(&child->descriptors[descriptor]);
        }
    }
    strncpy(child->name, parent->name, sizeof(child->name) - 1);
    if ((flags & LINUX_CLONE_PARENT_SETTID) && parent_tid) {
        uint32_t pid = (uint32_t)child->pid;
        vm_copy_to_user(&parent->address_space->vm, parent_tid, &pid, sizeof(pid));
    }
    if ((flags & LINUX_CLONE_CHILD_SETTID) && child_tid) {
        uint32_t pid = (uint32_t)child->pid;
        vm_copy_to_user(&child->address_space->vm, child_tid, &pid, sizeof(pid));
    }
    return child->pid;
}

static bool replace_current_image(Process *process, const char *name) {
    AddressSpace *replacement = address_space_create();
    uint64_t context[18];
    uintptr_t program_break;
    uintptr_t fs_base;
    uintptr_t tls_mapping;
    size_t tls_mapping_size;
    if (!replacement || !load_elf_image(replacement, name, context, &program_break,
                                         &fs_base, &tls_mapping,
                                         &tls_mapping_size)) {
        address_space_release(replacement);
        return false;
    }
    AddressSpace *old = process->address_space;
    process->address_space = replacement;
    memcpy(process_context, context, sizeof(context));
    memcpy(process->context, context, sizeof(context));
    process->program_break = program_break;
    process->mmap_hint = align_up(program_break + VM_PAGE_SIZE);
    if (replacement->loader_mapping_end > process->mmap_hint) {
        process->mmap_hint = replacement->loader_mapping_end;
    }
    process->fs_base = fs_base;
    process->gs_base = 0;
    process->tls_mapping = tls_mapping;
    process->tls_mapping_size = tls_mapping_size;
    memcpy(&process->fx_state, &default_fx_state, sizeof(process->fx_state));
    process->pending_signals = 0;
    strncpy(process->name, name, sizeof(process->name) - 1);
    vm_kernel_activate();
    address_space_release(old);
    return true;
}

static uint8_t linux_protection(uint64_t protection) {
    uint8_t result = 0;
    if (protection & 1) result |= VM_PROT_READ;
    if (protection & 2) result |= VM_PROT_WRITE;
    if (protection & 4) result |= VM_PROT_EXEC;
    return result;
}

static uint64_t linux_mmap(Process *process, uintptr_t requested, size_t size,
                           uint64_t protection, uint64_t flags, int descriptor,
                           uint64_t offset) {
    if (!size || size > VM_USER_MMAP_LIMIT - VM_USER_MMAP_BASE) {
        return linux_error(LINUX_EINVAL);
    }
    if ((protection & 6) == 6) return linux_error(LINUX_EPERM);
    bool file_backed = !(flags & LINUX_MAP_ANONYMOUS);
    if (file_backed && (descriptor < 3 || descriptor >= MAX_FILE_DESCRIPTORS ||
        !process->descriptors[descriptor].used)) {
        return linux_error(LINUX_EBADF);
    }
    FileDescriptor *mapped_file = file_backed ? &process->descriptors[descriptor] : NULL;
    bool memfd_backed = mapped_file && mapped_file->type == FD_MEMFD;
    if (file_backed &&
        ((!memfd_backed && (mapped_file->type != FD_FILE ||
                            !(flags & LINUX_MAP_PRIVATE))) ||
         (memfd_backed &&
          (((flags & LINUX_MAP_PRIVATE) != 0) ==
           ((flags & LINUX_MAP_SHARED) != 0))))) {
        return linux_error(LINUX_EINVAL);
    }
    if (memfd_backed && (mapped_file->object < 0 ||
        mapped_file->object >= MAX_MEMFD_OBJECTS ||
        !memfd_objects[mapped_file->object].used)) return linux_error(LINUX_EBADF);
    if (file_backed && (offset & (VM_PAGE_SIZE - 1))) return linux_error(LINUX_EINVAL);
    size_t requested_size = size;
    size = align_up(size);
    uintptr_t address = 0;
    if (flags & LINUX_MAP_FIXED) {
        address = align_down(requested);
        if (address < VM_USER_BASE || address + size > VM_USER_MMAP_LIMIT) {
            return linux_error(LINUX_EINVAL);
        }
        for (uintptr_t page = address; page < address + size; page += VM_PAGE_SIZE) {
            vm_unmap_page(&process->address_space->vm, page);
        }
    } else {
        uintptr_t hint = requested ? requested : process->mmap_hint;
        address = vm_find_free_range(&process->address_space->vm, hint, size);
        if (!address) address = vm_find_free_range(&process->address_space->vm,
                                                    VM_USER_MMAP_BASE, size);
        if (!address) return linux_error(LINUX_ENOMEM);
    }
    uint8_t final_protection = linux_protection(protection);
    if (memfd_backed) {
        MemfdObject *object = &memfd_objects[mapped_file->object];
        if (offset > object->size || requested_size > object->size - offset) {
            return linux_error(LINUX_EINVAL);
        }
        bool shared = (flags & LINUX_MAP_SHARED) != 0;
        uint8_t private_mapping_protection = final_protection | VM_PROT_WRITE;
        uintptr_t page = address;
        for (; page < address + size; page += VM_PAGE_SIZE) {
            size_t backing_page = (size_t)(offset / VM_PAGE_SIZE) +
                                  (size_t)((page - address) / VM_PAGE_SIZE);
            VmFrame *frame = memfd_frame(object, backing_page, true);
            bool mapped = frame && (shared ?
                vm_map_shared_frame(&process->address_space->vm, page,
                                    frame, final_protection) :
                vm_map_page(&process->address_space->vm, page,
                            private_mapping_protection));
            if (mapped && !shared) {
                mapped = vm_copy_to_user(&process->address_space->vm, page,
                                         frame->data, VM_PAGE_SIZE);
            }
            if (!mapped) break;
        }
        if (page != address + size ||
            (!shared && !vm_protect(&process->address_space->vm, address,
                                    size, final_protection))) {
            for (uintptr_t rollback = address; rollback < page;
                 rollback += VM_PAGE_SIZE) {
                vm_unmap_page(&process->address_space->vm, rollback);
            }
            return linux_error(LINUX_ENOMEM);
        }
        process->mmap_hint = address + size;
        return address;
    }
    uint8_t mapping_protection = final_protection;
    if (file_backed) mapping_protection |= VM_PROT_WRITE;
    for (uintptr_t page = address; page < address + size; page += VM_PAGE_SIZE) {
        if (!vm_map_page(&process->address_space->vm, page, mapping_protection)) {
            for (uintptr_t rollback = address; rollback < page; rollback += VM_PAGE_SIZE) {
                vm_unmap_page(&process->address_space->vm, rollback);
            }
            return linux_error(LINUX_ENOMEM);
        }
    }
    if (!file_backed && (flags & LINUX_MAP_SHARED) &&
        !vm_mark_shared(&process->address_space->vm, address, size)) {
        for (uintptr_t page = address; page < address + size; page += VM_PAGE_SIZE) {
            vm_unmap_page(&process->address_space->vm, page);
        }
        return linux_error(LINUX_ENOMEM);
    }
    if (file_backed) {
        FileDescriptor *file = &process->descriptors[descriptor];
        FsFileInfo info;
        if (!file_info(file->name, &info)) {
            for (uintptr_t page = address; page < address + size; page += VM_PAGE_SIZE) {
                vm_unmap_page(&process->address_space->vm, page);
            }
            return linux_error(LINUX_ENOENT);
        }
        size_t copy = 0;
        if (offset < info.size) {
            uint64_t available = info.size - offset;
            copy = available < requested_size ? (size_t)available : requested_size;
        }
        uint8_t contents[512];
        size_t completed = 0;
        bool copied = true;
        while (completed < copy) {
            size_t chunk = copy - completed;
            if (chunk > sizeof(contents)) chunk = sizeof(contents);
            int loaded = fs_read_at(file->name, offset + completed, contents, chunk);
            if (loaded <= 0 || !vm_copy_to_user(&process->address_space->vm,
                                                 address + completed, contents,
                                                 (size_t)loaded)) {
                copied = false;
                break;
            }
            completed += (size_t)loaded;
        }
        if (!copied || !vm_protect(&process->address_space->vm, address,
                                   size, final_protection)) {
            for (uintptr_t page = address; page < address + size; page += VM_PAGE_SIZE) {
                vm_unmap_page(&process->address_space->vm, page);
            }
            return linux_error(LINUX_EIO);
        }
    }
    process->mmap_hint = address + size;
    return address;
}

static uint64_t linux_brk(Process *process, uintptr_t requested) {
    if (!requested) return process->program_break;
    if (requested < VM_USER_BASE || requested >= VM_USER_MMAP_BASE) {
        return process->program_break;
    }
    uintptr_t old_end = align_up(process->program_break);
    uintptr_t new_end = align_up(requested);
    if (new_end > old_end) {
        for (uintptr_t page = old_end; page < new_end; page += VM_PAGE_SIZE) {
            if (!vm_map_page(&process->address_space->vm, page,
                             VM_PROT_READ | VM_PROT_WRITE)) return process->program_break;
        }
    } else {
        for (uintptr_t page = new_end; page < old_end; page += VM_PAGE_SIZE) {
            vm_unmap_page(&process->address_space->vm, page);
        }
    }
    process->program_break = requested;
    return requested;
}

static uint64_t linux_futex(Process *process, uintptr_t address, int operation,
                            uint32_t value, uintptr_t timeout_address) {
    uint32_t current;
    operation &= 0x7F;
    if (operation == LINUX_FUTEX_WAIT) {
        if (!vm_copy_from_user(&process->address_space->vm, &current, address,
                               sizeof(current))) return linux_error(LINUX_EFAULT);
        if (current != value) return linux_error(LINUX_EAGAIN);
        process->state = PROCESS_BLOCKED_FUTEX;
        process->futex_address = address;
        process->wake_tick = 0;
        if (timeout_address) {
            LinuxTimespec timeout;
            if (!vm_copy_from_user(&process->address_space->vm, &timeout, timeout_address,
                                   sizeof(timeout))) return linux_error(LINUX_EFAULT);
            if (timeout.seconds >= 0 && timeout.nanoseconds >= 0) {
                process->wake_tick = scheduler_ticks + (uint64_t)timeout.seconds * 100 +
                                     (uint64_t)timeout.nanoseconds / 10000000UL + 1;
            }
        }
        return 0;
    }
    if (operation == LINUX_FUTEX_WAKE) {
        int woken = 0;
        for (int index = 0; index < MAX_PROCESSES && woken < (int)value; ++index) {
            Process *waiter = &processes[index];
            if (waiter->used && waiter->state == PROCESS_BLOCKED_FUTEX &&
                waiter->address_space == process->address_space &&
                waiter->futex_address == address) {
                waiter->state = PROCESS_RUNNING;
                ++woken;
            }
        }
        return (uint64_t)woken;
    }
    return linux_error(LINUX_ENOSYS);
}

static void deliver_signal(Process *process) {
    if (!process->pending_signals || process->state != PROCESS_RUNNING) return;
    int signal = 1;
    while (signal < 32 && !(process->pending_signals & (1UL << signal))) ++signal;
    if (signal >= 32) return;
    process->pending_signals &= ~(1UL << signal);
    LinuxSignalAction *action = &process->signal_actions[signal];
    if (action->handler == 1) return;
    if (!action->handler) {
        finish_process(process, 128 + signal);
        return;
    }
    uintptr_t stack = align_down(process->context[CONTEXT_RSP] -
                                 sizeof(LinuxSignalFrame) - sizeof(uint64_t));
    LinuxSignalFrame frame;
    frame.magic = 0x4E4F56415349474EUL;
    memcpy(frame.context, process->context, sizeof(frame.context));
    if (!vm_copy_to_user(&process->address_space->vm, stack + sizeof(uint64_t),
                         &frame, sizeof(frame)) ||
        !vm_copy_to_user(&process->address_space->vm, stack, &action->restorer,
                         sizeof(action->restorer))) {
        finish_process(process, 128 + signal);
        return;
    }
    process->context[CONTEXT_RIP] = action->handler;
    process->context[CONTEXT_RSP] = stack;
    process->context[CONTEXT_RDI] = (uint64_t)signal;
}

bool process_init(void) {
    uintptr_t control;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(control));
    control = (control | (1UL << 1)) & ~((1UL << 2) | (1UL << 3));
    __asm__ volatile ("mov %0, %%cr0" : : "r"(control) : "memory");
    __asm__ volatile ("mov %%cr4, %0" : "=r"(control));
    control |= (1UL << 9) | (1UL << 10);
    __asm__ volatile ("mov %0, %%cr4" : : "r"(control) : "memory");
    __asm__ volatile ("fninit; fxsave %0" : "=m"(default_fx_state));
    memset(gdt, 0, sizeof(gdt));
    gdt[1] = 0x00AF9A000000FFFFUL;
    gdt[2] = 0x00CF92000000FFFFUL;
    gdt[3] = 0x00CFF2000000FFFFUL;
    gdt[4] = 0x00AFFA000000FFFFUL;
    memset(&tss, 0, sizeof(tss));
    tss.rsp0 = (uintptr_t)syscall_stack + sizeof(syscall_stack);
    tss.io_map_base = sizeof(tss);
    process_syscall_rsp = tss.rsp0;
    set_tss_descriptor(5, (uintptr_t)&tss, sizeof(tss) - 1);
    DescriptorPointer gdt_pointer = {sizeof(gdt) - 1, (uintptr_t)gdt};
    arch_load_gdt(&gdt_pointer);

    memset(idt, 0, sizeof(idt));
    set_idt_gate(32, timer_entry, 0x8E);
    set_idt_gate(14, page_fault_entry, 0x8E);
    set_idt_gate(0x80, syscall_entry, 0xEE);
    DescriptorPointer idt_pointer = {sizeof(idt) - 1, (uintptr_t)idt};
    arch_load_idt(&idt_pointer);

    write_msr(0xC0000080U, read_msr(0xC0000080U) | 1UL | (1UL << 11));
    write_msr(0xC0000081U, ((uint64_t)0x10 << 48) | ((uint64_t)0x08 << 32));
    write_msr(0xC0000082U, (uintptr_t)linux_syscall_entry);
    write_msr(0xC0000084U, (1UL << 9) | (1UL << 10));

    memset(processes, 0, sizeof(processes));
    memset(pipe_objects, 0, sizeof(pipe_objects));
    memset(socket_pairs, 0, sizeof(socket_pairs));
    memset(epoll_objects, 0, sizeof(epoll_objects));
    memset(event_objects, 0, sizeof(event_objects));
    memset(timer_objects, 0, sizeof(timer_objects));
    memset(memfd_objects, 0, sizeof(memfd_objects));
    memset(process_context, 0, sizeof(process_context));
    scheduler_ticks = 0;
    syscall_counter = 0;
    cow_fault_counter = 0;
    demand_fault_counter = 0;
    current_slot = -1;
    foreground_slot = -1;
    last_slot = -1;
    next_pid = 1;
    return true;
}

static void initialize_pic_and_timer(void) {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, 0xFE);
    outb(0xA1, 0xFF);
    uint16_t divisor = 11932;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)divisor);
    outb(0x40, (uint8_t)(divisor >> 8));
}

void process_enable_preemption(void) {
    initialize_pic_and_timer();
    __asm__ volatile ("sti");
}

static bool install_builtin_file(const char *name, const uint8_t *start,
                                 const uint8_t *end) {
    size_t size = (size_t)(end - start);
    uint8_t *existing = heap_alloc(size);
    if (existing) {
        int loaded = fs_read(name, existing, size);
        bool current = loaded == (int)size && !memcmp(existing, start, size);
        heap_free(existing);
        if (current) {
            fs_chmod(name, 0555);
            return true;
        }
    }
    bool saved = fs_write(name, start, size);
    if (saved) saved = fs_chmod(name, 0555);
    return saved;
}

bool process_install_builtin(void) {
    if (!fs_is_ready()) return false;
    return install_builtin_file("nova-ld.so", builtin_nova_ld_start, builtin_nova_ld_end) &&
           install_builtin_file("ld-linux-x86-64.so.2", builtin_nova_ld_start,
                                builtin_nova_ld_end) &&
           install_builtin_file("libnova.so", builtin_libnova_start, builtin_libnova_end) &&
           install_builtin_file("libc.so.6", builtin_libc_start, builtin_libc_end) &&
           install_builtin_file("libpthread.so.0", builtin_libpthread_start,
                                builtin_libpthread_end) &&
           install_builtin_file("libdl.so.2", builtin_libdl_start,
                                builtin_libdl_end) &&
           install_builtin_file("libm.so.6", builtin_libm_start,
                                builtin_libm_end) &&
           install_builtin_file("libgcc_s.so.1", builtin_libgcc_start,
                                builtin_libgcc_end) &&
           install_builtin_file("libtls.so", builtin_libtls_start,
                                builtin_libtls_end) &&
           install_builtin_file("libtlsdesc.so", builtin_libtlsdesc_start,
                                builtin_libtlsdesc_end) &&
           install_builtin_file("libctor.so", builtin_libctor_start,
                                builtin_libctor_end) &&
           install_builtin_file("init.elf", builtin_init_start, builtin_init_end);
}

bool process_load(const char *name) {
    int parent_pid = current_process() ? current_process()->pid : 0;
    int slot = create_process(name, parent_pid);
    if (slot < 0) return false;
    foreground_slot = slot;
    return true;
}

static void wake_timers(void) {
    for (int index = 0; index < MAX_PROCESSES; ++index) {
        Process *process = &processes[index];
        if (!process->used || !process->wake_tick || scheduler_ticks < process->wake_tick) continue;
        if (process->state == PROCESS_BLOCKED_FUTEX ||
            process->state == PROCESS_BLOCKED_SLEEP) {
            process->state = PROCESS_RUNNING;
            process->wake_tick = 0;
        }
    }
}

void process_schedule(void) {
    vm_kernel_activate();
    wake_timers();
    int selected = -1;
    for (int offset = 1; offset <= MAX_PROCESSES; ++offset) {
        int slot = (last_slot + offset) % MAX_PROCESSES;
        if (processes[slot].used && processes[slot].state == PROCESS_RUNNING) {
            selected = slot;
            break;
        }
    }
    if (selected < 0) {
        current_slot = -1;
        return;
    }
    Process *process = &processes[selected];
    deliver_signal(process);
    if (process->state != PROCESS_RUNNING) return;
    current_slot = selected;
    last_slot = selected;
    memcpy(process_context, process->context, sizeof(process_context));
    __asm__ volatile ("fxrstor %0" : : "m"(process->fx_state));
    write_msr(0xC0000100U, process->fs_base);
    write_msr(0xC0000101U, process->gs_base);
    vm_space_activate(&process->address_space->vm);
    process_enter_user();
    __asm__ volatile ("fxsave %0" : "=m"(process->fx_state));
    memcpy(process->context, process_context, sizeof(process_context));
    vm_kernel_activate();
    current_slot = -1;
}

bool process_handle_page_fault(uint64_t error_code, uintptr_t address) {
    Process *process = current_process();
    int result = process ? vm_handle_page_fault(&process->address_space->vm,
                                                address, error_code) : VM_FAULT_UNHANDLED;
    if (result == VM_FAULT_COW) {
        ++cow_fault_counter;
        if (cow_fault_counter == 1) {
            serial_write("NovaOS: copy-on-write page fault resolved\r\n");
        }
        return true;
    }
    if (result == VM_FAULT_DEMAND) {
        ++demand_fault_counter;
        if (demand_fault_counter == 1) {
            serial_write("NovaOS: demand-paged user stack expanded\r\n");
        }
        return true;
    }
    if (process) {
        serial_write("NovaOS: user page fault terminated process\r\n");
        finish_process(process, 139);
    }
    return false;
}

uint64_t process_handle_syscall(uint64_t number, uint64_t argument1,
                                uint64_t argument2, uint64_t argument3) {
    (void)argument3;
    Process *process = current_process();
    syscall_counter++;
    if (!process) return (uint64_t)-1;
    if (number == NOVA_SYS_WRITE) return write_console(process, argument1, argument2);
    if (number == NOVA_SYS_YIELD) return 0;
    if (number == NOVA_SYS_EXIT) {
        finish_process(process, (int)argument1);
        return 0;
    }
    return (uint64_t)-1;
}

uint64_t process_handle_linux_syscall(uint64_t number, uint64_t argument1,
                                      uint64_t argument2, uint64_t argument3,
                                      uint64_t argument4, uint64_t argument5,
                                      uint64_t argument6) {
    Process *process = current_process();
    syscall_counter++;
    if (!process) return linux_error(LINUX_ESRCH);
    if (process->seccomp_strict && number != LINUX_SYS_READ &&
        number != LINUX_SYS_WRITE && number != LINUX_SYS_EXIT &&
        number != LINUX_SYS_EXIT_GROUP && number != LINUX_SYS_RT_SIGRETURN) {
        return linux_error(LINUX_EPERM);
    }
    switch (number) {
        case LINUX_SYS_READ:
            return linux_read(process, (int)argument1, argument2, argument3);
        case LINUX_SYS_WRITE:
            if (argument1 == 1 || argument1 == 2) {
                return write_console(process, argument2, argument3);
            }
            return linux_write_file(process, (int)argument1, argument2, argument3);
        case LINUX_SYS_CLOSE:
            if (argument1 < 3 || argument1 >= MAX_FILE_DESCRIPTORS ||
                !process->descriptors[argument1].used) return linux_error(LINUX_EBADF);
            descriptor_release_resources(&process->descriptors[argument1]);
            memset(&process->descriptors[argument1], 0,
                   sizeof(process->descriptors[argument1]));
            return 0;
        case LINUX_SYS_POLL:
            return linux_poll(process, argument1, argument2);
        case LINUX_SYS_IOCTL:
            return linux_ioctl(process, (int)argument1, argument2, argument3);
        case LINUX_SYS_PREAD64:
            return linux_positioned_io(process, (int)argument1, argument2,
                                       argument3, argument4, false);
        case LINUX_SYS_PWRITE64:
            return linux_positioned_io(process, (int)argument1, argument2,
                                       argument3, argument4, true);
        case LINUX_SYS_READV:
            return linux_vector_io(process, (int)argument1, argument2,
                                   (int)argument3, false);
        case LINUX_SYS_WRITEV:
            return linux_vector_io(process, (int)argument1, argument2,
                                   (int)argument3, true);
        case LINUX_SYS_ACCESS:
            return linux_access_path(process, argument1);
        case LINUX_SYS_DUP:
            return linux_duplicate_descriptor(process, (int)argument1, -1, false);
        case LINUX_SYS_DUP2:
            return linux_duplicate_descriptor(process, (int)argument1,
                                               (int)argument2, true);
        case LINUX_SYS_SOCKETPAIR:
            return linux_socketpair(process, (int)argument1, (int)argument2, argument4);
        case LINUX_SYS_FSTAT: {
            if (argument1 < 3 || argument1 >= MAX_FILE_DESCRIPTORS ||
                !process->descriptors[argument1].used) return linux_error(LINUX_EBADF);
            LinuxStat stat;
            FileDescriptor *file = &process->descriptors[argument1];
            if (!(file->type == FD_MEMFD ? stat_for_memfd(file, &stat) :
                  stat_for_name(file->name, &stat))) {
                return linux_error(LINUX_ENOENT);
            }
            return vm_copy_to_user(&process->address_space->vm, argument2,
                                   &stat, sizeof(stat)) ? 0 : linux_error(LINUX_EFAULT);
        }
        case LINUX_SYS_LSEEK: {
            if (argument1 < 3 || argument1 >= MAX_FILE_DESCRIPTORS ||
                !process->descriptors[argument1].used) return linux_error(LINUX_EBADF);
            FileDescriptor *file = &process->descriptors[argument1];
            FsFileInfo info;
            uint64_t file_size = file->type == FD_MEMFD && file->object >= 0 &&
                                 file->object < MAX_MEMFD_OBJECTS &&
                                 memfd_objects[file->object].used ?
                                 memfd_objects[file->object].size : 0;
            int64_t base = argument3 == 0 ? 0 : argument3 == 1 ? (int64_t)file->offset :
                           argument3 == 2 && file->type == FD_MEMFD ?
                           (int64_t)file_size :
                           argument3 == 2 && file_info(file->name, &info) ?
                           (int64_t)info.size : (int64_t)-1;
            int64_t offset = (int64_t)argument2;
            if (base < 0 || (offset > 0 && base > (int64_t)0x7FFFFFFFFFFFFFFFUL - offset) ||
                base + offset < 0) {
                return linux_error(LINUX_EINVAL);
            }
            file->offset = (uint64_t)(base + offset);
            return file->offset;
        }
        case LINUX_SYS_MMAP:
            return linux_mmap(process, argument1, argument2, argument3,
                              argument4, (int)argument5, argument6);
        case LINUX_SYS_MPROTECT:
            if ((argument3 & 6) == 6) return linux_error(LINUX_EPERM);
            return vm_protect(&process->address_space->vm, argument1, argument2,
                              linux_protection(argument3)) ? 0 : linux_error(LINUX_EINVAL);
        case LINUX_SYS_MUNMAP: {
            if (!argument2 || argument1 + argument2 < argument1) return linux_error(LINUX_EINVAL);
            uintptr_t end = align_up(argument1 + argument2);
            for (uintptr_t page = align_down(argument1); page < end; page += VM_PAGE_SIZE) {
                vm_unmap_page(&process->address_space->vm, page);
            }
            return 0;
        }
        case LINUX_SYS_MADVISE:
            return 0;
        case LINUX_SYS_BRK:
            return linux_brk(process, argument1);
        case LINUX_SYS_RT_SIGACTION: {
            if (argument1 == 0 || argument1 >= 32) return linux_error(LINUX_EINVAL);
            LinuxSignalAction *action = &process->signal_actions[argument1];
            if (argument3 && !vm_copy_to_user(&process->address_space->vm, argument3,
                                              action, sizeof(*action))) {
                return linux_error(LINUX_EFAULT);
            }
            if (argument2 && !vm_copy_from_user(&process->address_space->vm, action,
                                                argument2, sizeof(*action))) {
                return linux_error(LINUX_EFAULT);
            }
            return 0;
        }
        case LINUX_SYS_RT_SIGPROCMASK:
            return 0;
        case LINUX_SYS_RT_SIGRETURN: {
            LinuxSignalFrame frame;
            uintptr_t address = process_context[CONTEXT_RSP];
            if (!vm_copy_from_user(&process->address_space->vm, &frame, address,
                                   sizeof(frame)) || frame.magic != 0x4E4F56415349474EUL) {
                return linux_error(LINUX_EFAULT);
            }
            memcpy(process_context, frame.context, sizeof(process_context));
            return process_context[CONTEXT_RAX];
        }
        case LINUX_SYS_SCHED_YIELD:
            return 0;
        case LINUX_SYS_NANOSLEEP: {
            LinuxTimespec request;
            if (!vm_copy_from_user(&process->address_space->vm, &request, argument1,
                                   sizeof(request))) return linux_error(LINUX_EFAULT);
            if (request.seconds < 0 || request.nanoseconds < 0 ||
                request.nanoseconds >= 1000000000) return linux_error(LINUX_EINVAL);
            process->wake_tick = scheduler_ticks + (uint64_t)request.seconds * 100 +
                                 (uint64_t)request.nanoseconds / 10000000UL + 1;
            process->state = PROCESS_BLOCKED_SLEEP;
            return 0;
        }
        case LINUX_SYS_GETPID:
            return process->tgid;
        case LINUX_SYS_GETPPID:
            return process->parent_pid;
        case LINUX_SYS_GETTID:
            return process->pid;
        case LINUX_SYS_GETUID:
        case LINUX_SYS_GETGID:
        case LINUX_SYS_GETEUID:
        case LINUX_SYS_GETEGID:
            return 1000;
        case LINUX_SYS_GETPGID:
        case LINUX_SYS_GETSID:
            return argument1 && argument1 != (uint64_t)process->pid ?
                   linux_error(LINUX_ESRCH) : (uint64_t)process->tgid;
        case LINUX_SYS_CLONE: {
            bool share = (argument1 & LINUX_CLONE_VM) != 0;
            int pid = clone_current(share, argument2, argument1, argument3,
                                    argument4, argument5);
            return pid < 0 ? linux_error(LINUX_EAGAIN) : (uint64_t)pid;
        }
        case LINUX_SYS_FORK:
        case LINUX_SYS_VFORK: {
            int pid = clone_current(false, 0, 0, 0, 0, 0);
            return pid < 0 ? linux_error(LINUX_EAGAIN) : (uint64_t)pid;
        }
        case LINUX_SYS_EXECVE: {
            char path[FS_NAME_MAX + 2];
            char name[FS_NAME_MAX + 1];
            if (!copy_user_string(process, argument1, path, sizeof(path)) ||
                !normalize_path(path, name)) return linux_error(LINUX_EFAULT);
            return replace_current_image(process, name) ? 0 : linux_error(LINUX_ENOENT);
        }
        case LINUX_SYS_EXIT:
            finish_process(process, (int)argument1);
            return 0;
        case LINUX_SYS_EXIT_GROUP:
            for (int index = 0; index < MAX_PROCESSES; ++index) {
                if (processes[index].used && processes[index].tgid == process->tgid) {
                    finish_process(&processes[index], (int)argument1);
                }
            }
            return 0;
        case LINUX_SYS_WAIT4: {
            bool child_exists = false;
            for (int index = 0; index < MAX_PROCESSES; ++index) {
                Process *child = &processes[index];
                if (!child->used || child->parent_pid != process->pid ||
                    (argument1 != (uint64_t)-1 && argument1 != (uint64_t)child->pid)) continue;
                child_exists = true;
                if (child->state != PROCESS_ZOMBIE) continue;
                if (argument2) {
                    int status = (child->exit_code & 0xFF) << 8;
                    if (!vm_copy_to_user(&process->address_space->vm, argument2,
                                         &status, sizeof(status))) return linux_error(LINUX_EFAULT);
                }
                int pid = child->pid;
                reset_process_slot(child);
                return pid;
            }
            if (!child_exists) return linux_error(LINUX_ECHILD);
            return (argument3 & LINUX_WNOHANG) ? 0 : linux_error(LINUX_EAGAIN);
        }
        case LINUX_SYS_KILL:
            for (int index = 0; index < MAX_PROCESSES; ++index) {
                if (processes[index].used && processes[index].pid == (int)argument1) {
                    if (!argument2) return 0;
                    if (argument2 >= 32) return linux_error(LINUX_EINVAL);
                    processes[index].pending_signals |= 1UL << argument2;
                    return 0;
                }
            }
            return linux_error(LINUX_ESRCH);
        case LINUX_SYS_UNAME: {
            char names[6][65];
            memset(names, 0, sizeof(names));
            strcpy(names[0], "NovaOS");
            strcpy(names[1], "nova");
            strcpy(names[2], "0.2-native");
            strcpy(names[3], "NovaOS x86_64 direct kernel");
            strcpy(names[4], "x86_64");
            strcpy(names[5], "localdomain");
            return vm_copy_to_user(&process->address_space->vm, argument1,
                                   names, sizeof(names)) ? 0 : linux_error(LINUX_EFAULT);
        }
        case LINUX_SYS_GETRLIMIT: {
            LinuxRlimit limit = {0x40000000UL, 0x40000000UL};
            if (argument1 == 7) limit.current = limit.maximum = MAX_FILE_DESCRIPTORS;
            else if (argument1 == 3) limit.current = limit.maximum = VM_USER_STACK_SIZE;
            return vm_copy_to_user(&process->address_space->vm, argument2,
                                   &limit, sizeof(limit)) ? 0 : linux_error(LINUX_EFAULT);
        }
        case LINUX_SYS_GETRUSAGE: {
            uint8_t usage[144];
            memset(usage, 0, sizeof(usage));
            return vm_copy_to_user(&process->address_space->vm, argument2,
                                   usage, sizeof(usage)) ? 0 : linux_error(LINUX_EFAULT);
        }
        case LINUX_SYS_SIGALTSTACK:
            return 0;
        case LINUX_SYS_MKDIR:
            return linux_mkdir_path(process, argument1, (uint16_t)argument2);
        case LINUX_SYS_UNLINK:
            return linux_unlink_path(process, argument1, 0);
        case LINUX_SYS_READLINK:
            return linux_readlink_path(process, argument1, argument2, argument3);
        case LINUX_SYS_FCNTL:
            return 0;
        case LINUX_SYS_FTRUNCATE:
            return linux_ftruncate(process, (int)argument1, argument2);
        case LINUX_SYS_GETCWD: {
            static const char root[] = "/";
            if (argument2 < sizeof(root) || !vm_copy_to_user(&process->address_space->vm,
                                                             argument1, root, sizeof(root))) {
                return linux_error(LINUX_EFAULT);
            }
            return argument1;
        }
        case LINUX_SYS_PRCTL:
            if (argument1 == LINUX_PR_SET_NO_NEW_PRIVS) {
                if (argument2 != 1 || argument3 || argument4 || argument5) {
                    return linux_error(LINUX_EINVAL);
                }
                process->no_new_privs = true;
                return 0;
            }
            if (argument1 == LINUX_PR_GET_NO_NEW_PRIVS) {
                return process->no_new_privs ? 1 : 0;
            }
            return linux_error(LINUX_EINVAL);
        case LINUX_SYS_ARCH_PRCTL:
            if (argument1 == LINUX_ARCH_SET_FS) process->fs_base = argument2;
            else if (argument1 == LINUX_ARCH_SET_GS) process->gs_base = argument2;
            else if (argument1 == LINUX_ARCH_GET_FS) {
                return vm_copy_to_user(&process->address_space->vm, argument2,
                    &process->fs_base, sizeof(process->fs_base)) ? 0 : linux_error(LINUX_EFAULT);
            } else if (argument1 == LINUX_ARCH_GET_GS) {
                return vm_copy_to_user(&process->address_space->vm, argument2,
                    &process->gs_base, sizeof(process->gs_base)) ? 0 : linux_error(LINUX_EFAULT);
            } else return linux_error(LINUX_EINVAL);
            return 0;
        case LINUX_SYS_FUTEX:
            return linux_futex(process, argument1, (int)argument2,
                               (uint32_t)argument3, argument4);
        case LINUX_SYS_SCHED_GETAFFINITY: {
            if (!argument3) return linux_error(LINUX_EINVAL);
            uint64_t mask = 1;
            size_t size = argument2 < sizeof(mask) ? (size_t)argument2 : sizeof(mask);
            return vm_copy_to_user(&process->address_space->vm, argument3,
                                   &mask, size) ? size : linux_error(LINUX_EFAULT);
        }
        case LINUX_SYS_SET_TID_ADDRESS:
            process->clear_child_tid = argument1;
            return process->pid;
        case LINUX_SYS_GETDENTS64:
            return linux_getdents64(process, (int)argument1, argument2, argument3);
        case LINUX_SYS_EPOLL_WAIT:
            return linux_epoll_wait(process, (int)argument1, argument2, (int)argument3);
        case LINUX_SYS_EPOLL_CTL:
            return linux_epoll_ctl(process, (int)argument1, (int)argument2,
                                   (int)argument3, argument4);
        case LINUX_SYS_CLOCK_GETTIME: {
            LinuxTimespec time;
            time.seconds = (int64_t)(scheduler_ticks / 100);
            time.nanoseconds = (int64_t)(scheduler_ticks % 100) * 10000000;
            return vm_copy_to_user(&process->address_space->vm, argument2,
                                   &time, sizeof(time)) ? 0 : linux_error(LINUX_EFAULT);
        }
        case LINUX_SYS_OPENAT:
            return linux_openat(process, argument2, (int)argument3);
        case LINUX_SYS_MKDIRAT:
            return linux_mkdir_path(process, argument2, (uint16_t)argument3);
        case LINUX_SYS_NEWFSTATAT: {
            char path[FS_NAME_MAX + 2];
            char name[FS_NAME_MAX + 1];
            LinuxStat stat;
            if (!copy_user_string(process, argument2, path, sizeof(path)) ||
                !normalize_path(path, name)) return linux_error(LINUX_EFAULT);
            if (!stat_for_name(name, &stat)) return linux_error(LINUX_ENOENT);
            return vm_copy_to_user(&process->address_space->vm, argument3,
                                   &stat, sizeof(stat)) ? 0 : linux_error(LINUX_EFAULT);
        }
        case LINUX_SYS_UNLINKAT:
            return linux_unlink_path(process, argument2, (int)argument3);
        case LINUX_SYS_SYMLINKAT:
            return linux_symlink_path(process, argument1, argument3);
        case LINUX_SYS_READLINKAT:
            return linux_readlink_path(process, argument2, argument3, argument4);
        case LINUX_SYS_FACCESSAT:
            return linux_access_path(process, argument2);
        case LINUX_SYS_PPOLL:
            return linux_poll(process, argument1, argument2);
        case LINUX_SYS_SET_ROBUST_LIST:
            return 0;
        case LINUX_SYS_TIMERFD_CREATE:
            return linux_timerfd_create(process, (int)argument1, (int)argument2);
        case LINUX_SYS_TIMERFD_SETTIME:
            return linux_timerfd_settime(process, (int)argument1, (int)argument2,
                                         argument3, argument4);
        case LINUX_SYS_TIMERFD_GETTIME:
            return linux_timerfd_gettime(process, (int)argument1, argument2);
        case LINUX_SYS_EVENTFD2:
            return linux_eventfd2(process, argument1, (int)argument2);
        case LINUX_SYS_EPOLL_CREATE1:
            return linux_epoll_create(process);
        case LINUX_SYS_PIPE2:
            return linux_pipe2(process, argument1);
        case LINUX_SYS_DUP3:
            if (argument1 == argument2) return linux_error(LINUX_EINVAL);
            return linux_duplicate_descriptor(process, (int)argument1,
                                               (int)argument2, true);
        case LINUX_SYS_PRLIMIT64:
            if (argument4) {
                LinuxRlimit limit = {0x40000000UL, 0x40000000UL};
                if (argument2 == 7) limit.current = limit.maximum = MAX_FILE_DESCRIPTORS;
                else if (argument2 == 3) limit.current = limit.maximum = VM_USER_STACK_SIZE;
                if (!vm_copy_to_user(&process->address_space->vm, argument4,
                                     &limit, sizeof(limit))) return linux_error(LINUX_EFAULT);
            }
            return 0;
        case LINUX_SYS_SECCOMP:
            if (argument1 != LINUX_SECCOMP_SET_MODE_STRICT || argument2 || argument3) {
                return linux_error(LINUX_EINVAL);
            }
            if (!process->no_new_privs) return linux_error(LINUX_EACCES);
            process->seccomp_strict = true;
            return 0;
        case LINUX_SYS_GETRANDOM: {
            uint64_t state = scheduler_ticks ^ ((uint64_t)process->pid << 32) ^ 0x4E6F76614F53UL;
            for (size_t index = 0; index < argument2; ++index) {
                state ^= state << 13;
                state ^= state >> 7;
                state ^= state << 17;
                uint8_t byte = (uint8_t)state;
                if (!vm_copy_to_user(&process->address_space->vm, argument1 + index,
                                     &byte, 1)) return linux_error(LINUX_EFAULT);
            }
            return argument2;
        }
        case LINUX_SYS_MEMFD_CREATE:
            return linux_memfd_create(process, argument1, (int)argument2);
        default:
            return linux_error(LINUX_ENOSYS);
    }
}

bool process_is_running(void) {
    if (foreground_slot < 0 || foreground_slot >= MAX_PROCESSES) return false;
    Process *process = &processes[foreground_slot];
    return process->used && process->state != PROCESS_ZOMBIE;
}

int process_count(void) {
    int count = 0;
    for (int index = 0; index < MAX_PROCESSES; ++index) {
        if (processes[index].used && processes[index].state != PROCESS_ZOMBIE) ++count;
    }
    return count;
}

int process_foreground_pid(void) {
    return foreground_slot >= 0 && processes[foreground_slot].used ?
           processes[foreground_slot].pid : 0;
}

const char *process_name(void) {
    return foreground_slot >= 0 && processes[foreground_slot].used ?
           processes[foreground_slot].name : "none";
}

uint64_t process_syscall_count(void) {
    return syscall_counter;
}

const char *process_output(void) {
    return foreground_slot >= 0 && processes[foreground_slot].used ?
           processes[foreground_slot].output : "";
}

int process_exit_code(void) {
    return foreground_slot >= 0 && processes[foreground_slot].used ?
           processes[foreground_slot].exit_code : 0;
}
