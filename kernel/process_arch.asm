[bits 64]

global arch_load_gdt
global arch_load_idt
global process_enter_user
global syscall_entry
global linux_syscall_entry
global timer_entry
global page_fault_entry

extern process_kernel_rsp
extern process_syscall_rsp
extern process_user_rsp
extern process_context
extern scheduler_ticks
extern process_handle_syscall
extern process_handle_linux_syscall
extern process_handle_page_fault

%macro PUSH_USER_REGISTERS 0
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro POP_USER_REGISTERS 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
%endmacro

%macro SAVE_USER_CONTEXT 0
    cld
    mov rsi, rsp
    lea rdi, [rel process_context]
    mov rcx, 15
    rep movsq
    mov rax, [rsp + 120]
    mov [rel process_context + 120], rax
    mov rax, [rsp + 136]
    mov [rel process_context + 128], rax
    mov rax, [rsp + 144]
    mov [rel process_context + 136], rax
%endmacro

%macro SAVE_USER_CONTEXT_WITH_ERROR 0
    cld
    mov rsi, rsp
    lea rdi, [rel process_context]
    mov rcx, 15
    rep movsq
    mov rax, [rsp + 128]
    mov [rel process_context + 120], rax
    mov rax, [rsp + 144]
    mov [rel process_context + 128], rax
    mov rax, [rsp + 152]
    mov [rel process_context + 136], rax
%endmacro

%macro RETURN_TO_KERNEL_SCHEDULER 0
    mov rsp, [rel process_kernel_rsp]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    sti
    ret
%endmacro

section .text

arch_load_gdt:
    lgdt [rdi]
    push qword 0x08
    lea rax, [rel .new_code]
    push rax
    retfq
.new_code:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov ax, 0x28
    ltr ax
    ret

arch_load_idt:
    lidt [rdi]
    ret

process_enter_user:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov [rel process_kernel_rsp], rsp

    lea rax, [rel process_context]
    push qword 0x1B
    push qword [rax + 136]
    mov rdx, [rax + 128]
    or rdx, 0x202
    push rdx
    push qword 0x23
    push qword [rax + 120]

    mov r15, [rax]
    mov r14, [rax + 8]
    mov r13, [rax + 16]
    mov r12, [rax + 24]
    mov r11, [rax + 32]
    mov r10, [rax + 40]
    mov r9, [rax + 48]
    mov r8, [rax + 56]
    mov rbp, [rax + 64]
    mov rdi, [rax + 72]
    mov rsi, [rax + 80]
    mov rdx, [rax + 88]
    mov rcx, [rax + 96]
    mov rbx, [rax + 104]
    mov rax, [rax + 112]
    iretq

syscall_entry:
    PUSH_USER_REGISTERS
    SAVE_USER_CONTEXT

    mov rdi, [rel process_context + 112]
    mov rsi, [rel process_context + 72]
    mov rdx, [rel process_context + 80]
    mov rcx, [rel process_context + 88]
    call process_handle_syscall
    mov [rel process_context + 112], rax
    RETURN_TO_KERNEL_SCHEDULER

; Native x86-64 Linux-compatible SYSCALL entry. SYSCALL does not switch
; stacks, so save the Ring 3 RSP first and move to the TSS-owned kernel stack.
; RCX and R11 contain the return RIP and RFLAGS as defined by the architecture.
linux_syscall_entry:
    mov [rel process_user_rsp], rsp
    mov rsp, [rel process_syscall_rsp]

    mov [rel process_context + 0], r15
    mov [rel process_context + 8], r14
    mov [rel process_context + 16], r13
    mov [rel process_context + 24], r12
    mov [rel process_context + 32], r11
    mov [rel process_context + 40], r10
    mov [rel process_context + 48], r9
    mov [rel process_context + 56], r8
    mov [rel process_context + 64], rbp
    mov [rel process_context + 72], rdi
    mov [rel process_context + 80], rsi
    mov [rel process_context + 88], rdx
    mov [rel process_context + 96], rcx
    mov [rel process_context + 104], rbx
    mov [rel process_context + 112], rax
    mov [rel process_context + 120], rcx
    mov [rel process_context + 128], r11
    mov rbx, [rel process_user_rsp]
    mov [rel process_context + 136], rbx

    ; SysV C call: handler(number, a1, a2, a3, a4, a5, a6).
    ; Reserve 16 bytes so the stack remains correctly aligned at CALL.
    sub rsp, 16
    mov rbx, [rel process_context + 48]
    mov [rsp], rbx
    mov r9, [rel process_context + 56]
    mov r8, [rel process_context + 40]
    mov rcx, [rel process_context + 88]
    mov rdx, [rel process_context + 80]
    mov rsi, [rel process_context + 72]
    mov rdi, [rel process_context + 112]
    call process_handle_linux_syscall
    add rsp, 16
    mov [rel process_context + 112], rax
    RETURN_TO_KERNEL_SCHEDULER

; Vector 14 pushes an error code before RIP/CS/RFLAGS/RSP/SS. A user write to
; a shared COW page is fixed in C and retried at the exact faulting instruction.
page_fault_entry:
    PUSH_USER_REGISTERS
    mov rax, [rsp + 136]
    and eax, 3
    cmp eax, 3
    jne .kernel_page_fault

    SAVE_USER_CONTEXT_WITH_ERROR
    mov rdi, [rsp + 120]
    mov rsi, cr2
    call process_handle_page_fault
    test al, al
    jz .terminate_faulting_process

    POP_USER_REGISTERS
    add rsp, 8
    iretq

.terminate_faulting_process:
    RETURN_TO_KERNEL_SCHEDULER

.kernel_page_fault:
    cli
.kernel_page_fault_halt:
    hlt
    jmp .kernel_page_fault_halt

timer_entry:
    PUSH_USER_REGISTERS
    inc qword [rel scheduler_ticks]
    mov rax, [rsp + 128]
    and eax, 3
    cmp eax, 3
    je .preempt_user

    mov al, 0x20
    out 0x20, al
    POP_USER_REGISTERS
    iretq

.preempt_user:
    SAVE_USER_CONTEXT
    mov al, 0x20
    out 0x20, al
    RETURN_TO_KERNEL_SCHEDULER
