[bits 64]

global _start
extern kernel_main
extern __bss_start
extern __bss_end

section .text.entry
_start:
    cli
    cld
    mov rsp, 0x01000000
    mov rbp, rsp

    ; A raw kernel image does not contain .bss, so clear it explicitly.
    mov rdi, __bss_start
    mov rcx, __bss_end
    sub rcx, rdi
    xor eax, eax
    mov rdx, rcx
    shr rcx, 3
    rep stosq
    mov rcx, rdx
    and rcx, 7
    rep stosb

    call kernel_main

.halt:
    cli
    hlt
    jmp .halt
