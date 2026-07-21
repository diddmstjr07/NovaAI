[org 0x7C00]
[bits 16]

STAGE2_ADDRESS equ 0x8000
STAGE2_SECTORS equ 16

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld

    mov [boot_drive], dl

    mov si, loading_message
    call print_string

    ; Confirm that the boot disk supports INT 13h extensions.
    mov bx, 0x55AA
    mov ah, 0x41
    mov dl, [boot_drive]
    int 0x13
    jc disk_error
    cmp bx, 0xAA55
    jne disk_error
    test cx, 0x0001
    jz disk_error

    ; Use BIOS EDD/LBA services to load stage 2 from LBA 1.
    mov word [disk_packet + 2], STAGE2_SECTORS
    mov word [disk_packet + 4], STAGE2_ADDRESS
    mov word [disk_packet + 6], 0
    mov dword [disk_packet + 8], 1
    mov dword [disk_packet + 12], 0

    mov byte [retry_count], 3
.read_stage2:
    mov si, disk_packet
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jnc .stage2_loaded
    xor ah, ah
    mov dl, [boot_drive]
    int 0x13
    dec byte [retry_count]
    jnz .read_stage2
    jmp disk_error

.stage2_loaded:
    mov dl, [boot_drive]
    jmp 0x0000:STAGE2_ADDRESS

print_string:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp print_string
.done:
    ret

disk_error:
    mov si, error_message
    call print_string
    cli
    hlt
    jmp $

boot_drive db 0
retry_count db 0
loading_message db 'NovaOS booting...', 13, 10, 0
error_message db 'Stage 2 read failed.', 13, 10, 0

align 4
disk_packet:
    db 0x10, 0
    dw 0
    dw 0, 0
    dq 0

times 510 - ($ - $$) db 0
dw 0xAA55
