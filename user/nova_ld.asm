[bits 64]

global nova_interpreter_marker
global __tls_get_addr
global __nova_tlsdesc_static
global nova_interpreter_start

section .text
nova_interpreter_marker:
    mov eax, 0x4C442121
    ret

; NovaOS enters the interpreter here with RDI pointing at a read-only startup
; table: magic, final entry, function count, then relocated constructor slots.
nova_interpreter_start:
    mov rax, 0x4E4F564153544152
    cmp [rdi], rax
    jne .startup_invalid
    push r12
    push r13
    push r14
    sub rsp, 8
    mov r14, [rdi + 8]
    mov r13, [rdi + 16]
    lea r12, [rdi + 24]
.constructor_loop:
    test r13, r13
    jz .constructors_done
    xor edi, edi
    xor esi, esi
    xor edx, edx
    call [r12]
    add r12, 8
    dec r13
    jmp .constructor_loop
.constructors_done:
    add rsp, 32
    jmp r14
.startup_invalid:
    ud2

; void *__tls_get_addr(const struct { uint64_t module, offset; } *index)
; FS:8 points at NovaOS's compact DTV. DTV[0] stores the largest module ID and
; each subsequent slot stores that module's per-thread TLS block base.
__tls_get_addr:
    mov rax, [fs:8]
    test rax, rax
    jz .tls_missing
    mov rcx, [rdi]
    test rcx, rcx
    jz .tls_missing
    cmp rcx, [rax]
    ja .tls_missing
    mov rax, [rax + rcx * 8]
    test rax, rax
    jz .tls_missing
    add rax, [rdi + 8]
    ret
.tls_missing:
    xor eax, eax
    ret

; GNU2 TLSDESC static resolver. RAX points at the two-word descriptor and its
; second word contains the signed offset from this thread's FS base.
__nova_tlsdesc_static:
    mov rax, [rax + 8]
    ret
