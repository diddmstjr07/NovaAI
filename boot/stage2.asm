[org 0x8000]
[bits 16]

%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 1
%endif

KERNEL_LBA       equ 17
KERNEL_SEGMENT   equ 0x1000
VBE_MODE_INFO    equ 0x6000
; QEMU/Bochs VBE extension: 1024x768x32 high-resolution desktop mode.
VBE_MODE         equ 0x144
PML4_TABLE       equ 0x70000
PDPT_TABLE       equ 0x71000
PAGE_DIR_TABLES  equ 0x72000

stage2_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7A00
    sti
    cld

    mov [boot_drive], dl
    mov word [sectors_left], KERNEL_SECTORS
    mov word [load_segment], KERNEL_SEGMENT
    mov dword [disk_packet + 8], KERNEL_LBA
    mov dword [disk_packet + 12], 0

.load_kernel:
    cmp word [sectors_left], 0
    je .kernel_loaded

    mov ax, [load_segment]
    mov [disk_packet + 6], ax
    mov word [disk_packet + 4], 0
    mov word [disk_packet + 2], 1

    mov byte [retry_count], 3
.retry:
    mov si, disk_packet
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jnc .sector_ok

    xor ah, ah
    mov dl, [boot_drive]
    int 0x13
    dec byte [retry_count]
    jnz .retry
    mov si, kernel_error
    jmp fatal_error

.sector_ok:
    add word [load_segment], 0x20
    add dword [disk_packet + 8], 1
    adc dword [disk_packet + 12], 0
    dec word [sectors_left]
    jmp .load_kernel

.kernel_loaded:
    ; Refuse CPUs that cannot enter IA-32e Long Mode.
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .cpu_failed
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz .cpu_failed

    ; Ask VBE for mode information before switching to protected mode.
    xor ax, ax
    mov es, ax
    mov di, VBE_MODE_INFO
    mov ax, 0x4F01
    mov cx, VBE_MODE
    int 0x10
    cmp ax, 0x004F
    jne .video_failed

    ; Require a supported graphics mode with a linear framebuffer.
    mov ax, [VBE_MODE_INFO]
    and ax, 0x0091
    cmp ax, 0x0091
    jne .video_failed
    cmp byte [VBE_MODE_INFO + 27], 6
    jne .video_failed
    cmp word [VBE_MODE_INFO + 18], 1024
    jne .video_failed
    cmp word [VBE_MODE_INFO + 20], 768
    jne .video_failed
    mov al, [VBE_MODE_INFO + 25]
    cmp al, 24
    je .video_format_ok
    cmp al, 32
    jne .video_failed
.video_format_ok:

    mov ax, 0x4F02
    mov bx, VBE_MODE | 0x4000
    int 0x10
    cmp ax, 0x004F
    jne .video_failed

    ; Fast A20 gate.
    in al, 0x92
    or al, 0x02
    and al, 0xFE
    out 0x92, al

    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp CODE_SELECTOR:protected_mode

.video_failed:
    mov si, video_error
    jmp fatal_error

.cpu_failed:
    mov si, cpu_error
    jmp fatal_error

fatal_error:
    mov ax, 0x0003
    int 0x10
.print:
    lodsb
    test al, al
    jz .halt
    mov ah, 0x0E
    mov bx, 0x000C
    int 0x10
    jmp .print
.halt:
    cli
    hlt
    jmp .halt

[bits 32]
protected_mode:
    mov ax, DATA_SELECTOR
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, PML4_TABLE
    cld

    ; Identity-map the first 4 GiB with 2 MiB pages. This includes the
    ; low-memory kernel and the 32-bit VBE framebuffer aperture.
    mov edi, PML4_TABLE
    xor eax, eax
    mov ecx, (6 * 4096) / 4
    rep stosd

    mov dword [PML4_TABLE], PDPT_TABLE | 0x03
    mov dword [PML4_TABLE + 4], 0
    mov edi, PDPT_TABLE
    mov eax, PAGE_DIR_TABLES | 0x03
    mov ecx, 4
.build_pdpt:
    mov [edi], eax
    mov dword [edi + 4], 0
    add eax, 0x1000
    add edi, 8
    loop .build_pdpt

    mov edi, PAGE_DIR_TABLES
    mov eax, 0x00000083
    xor edx, edx
    mov ecx, 2048
.build_page_directories:
    mov [edi], eax
    mov [edi + 4], edx
    add eax, 0x00200000
    adc edx, 0
    add edi, 8
    loop .build_page_directories

    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax
    mov eax, PML4_TABLE
    mov cr3, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax
    jmp LONG_CODE_SELECTOR:long_mode

[bits 64]
long_mode:
    mov ax, DATA_SELECTOR
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, 0x01000000
    mov rbp, rsp
    cld

    mov rax, 0x00010000
    jmp rax

[bits 16]
boot_drive db 0
retry_count db 0
sectors_left dw 0
load_segment dw 0
kernel_error db 'Kernel read failed.', 13, 10, 0
video_error db 'VBE 1024x768 mode unavailable.', 13, 10, 0
cpu_error db 'This CPU does not support x86-64 Long Mode.', 13, 10, 0

align 4
disk_packet:
    db 0x10, 0
    dw 1
    dw 0, KERNEL_SEGMENT
    dq KERNEL_LBA

align 8
gdt_start:
    dq 0
gdt_code:
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00
gdt_data:
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00
gdt_long_code:
    dw 0x0000, 0x0000
    db 0x00, 10011010b, 10100000b, 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SELECTOR equ gdt_code - gdt_start
DATA_SELECTOR equ gdt_data - gdt_start
LONG_CODE_SELECTOR equ gdt_long_code - gdt_start

times (16 * 512) - ($ - $$) db 0
