[bits 64]

global pthread_create
global pthread_join
global pthread_exit

section .text
; int pthread_create(uint64_t *thread, const void *attr,
;                    void *(*start)(void *), void *argument)
pthread_create:
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov r14, rdi
    mov r12, rdx
    mov r13, rcx

    ; Private 64 KiB stack, shared into the CLONE_VM address space.
    mov eax, 9
    xor edi, edi
    mov esi, 65536
    mov edx, 3
    mov r10d, 0x22
    mov r8, -1
    xor r9d, r9d
    syscall
    test rax, rax
    js .create_failed
    lea r15, [rax + 65536]
    and r15, -16

    mov eax, 56
    ; CLONE_VM | CLONE_SETTLS. TLS=-1 asks NovaOS to instantiate a fresh
    ; copy of the executable's initial PT_TLS template for this thread.
    mov edi, 0x00080100
    mov rsi, r15
    xor edx, edx
    xor r10d, r10d
    mov r8, -1
    syscall
    test rax, rax
    jz .thread_child
    test rax, rax
    js .create_failed
    mov [r14], rax
    xor eax, eax
.create_return:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

.create_failed:
    mov eax, 12
    jmp .create_return

.thread_child:
    mov rdi, r13
    call r12
    mov rdi, rax
    mov eax, 60
    syscall
    ud2

; int pthread_join(uint64_t thread, void **result)
pthread_join:
    push rbx
    mov rbx, rdi
.join_wait:
    mov eax, 61
    mov rdi, rbx
    xor esi, esi
    mov edx, 1
    xor r10d, r10d
    syscall
    cmp rax, rbx
    je .join_done
    mov eax, 24
    syscall
    jmp .join_wait
.join_done:
    xor eax, eax
    pop rbx
    ret

pthread_exit:
    mov eax, 60
    syscall
    ud2
