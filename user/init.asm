[bits 64]

global _start
extern nova_shared_probe
extern strlen
extern malloc
extern free
extern pthread_create
extern pthread_join
extern pthread_mutex_lock
extern pthread_mutex_unlock
extern sqrt
extern _Unwind_GetIP
extern nova_tls_get
extern nova_tls_set
extern nova_tls_gd_get
extern nova_tls_gd_set
extern nova_tlsdesc_get
extern nova_tlsdesc_set
extern nova_constructor_state

%macro WRITE_MESSAGE 2
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel %1]
    mov rdx, %2
    syscall
%endmacro

section .text
_start:
    ; Only the top stack page exists initially. This write deliberately lands
    ; two pages lower and must be satisfied by the demand-page fault path.
    mov qword [rsp - 8192], 0x4E4F5641

    call nova_constructor_state wrt ..plt
    mov rdx, 0x434F4E5354525543
    cmp rax, rdx
    jne .constructor_failed
    WRITE_MESSAGE constructor_message, constructor_message_length
    jmp .constructor_done
.constructor_failed:
    WRITE_MESSAGE constructor_failure, constructor_failure_length
.constructor_done:

    ; This absolute pointer is emitted as R_X86_64_RELATIVE in the PIE. The
    ; kernel's native dynamic ELF relocation pass must fix it before entry.
    mov rax, 1
    mov rdi, 1
    mov rsi, [rel abi_pointer]
    mov rdx, abi_message_length
    syscall
    WRITE_MESSAGE demand_message, demand_message_length

    ; Preserve an XMM register across a real scheduler round trip.  Skia and
    ; V8 require per-thread x87/SSE state rather than one CPU-global state.
    mov rax, 0x4E4F564153534532
    movq xmm0, rax
    mov eax, 24
    syscall
    movq rdx, xmm0
    mov rax, 0x4E4F564153534532
    cmp rdx, rax
    jne .simd_failed
    WRITE_MESSAGE simd_message, simd_message_length
    jmp .simd_done
.simd_failed:
    WRITE_MESSAGE simd_failure, simd_failure_length
.simd_done:

    mov rax, 0x4022000000000000
    movq xmm0, rax
    call sqrt wrt ..plt
    movq rdx, xmm0
    mov rax, 0x4008000000000000
    cmp rdx, rax
    jne .libm_failed
    WRITE_MESSAGE libm_message, libm_message_length
    jmp .libm_done
.libm_failed:
    WRITE_MESSAGE libm_failure, libm_failure_length
.libm_done:

    lea rdi, [rel unwind_context]
    call _Unwind_GetIP wrt ..plt
    cmp rax, 0x4E4F5641
    jne .unwind_failed
    WRITE_MESSAGE unwind_message, unwind_message_length
    jmp .unwind_done
.unwind_failed:
    WRITE_MESSAGE unwind_failure, unwind_failure_length
.unwind_done:

    ; This call is emitted through the PLT and resolved from libnova.so via
    ; DT_NEEDED by NovaOS's native dynamic linker.
    call nova_shared_probe wrt ..plt
    cmp eax, 0x4E4F5641
    jne .shared_link_failed
    WRITE_MESSAGE shared_link_message, shared_link_message_length
    jmp .shared_link_done
.shared_link_failed:
    WRITE_MESSAGE shared_link_failure, shared_link_failure_length
.shared_link_done:
    lea rdi, [rel libc_probe_text]
    call strlen wrt ..plt
    cmp rax, libc_probe_text_length
    jne .libc_failed
    mov rdi, 64
    call malloc wrt ..plt
    test rax, rax
    jz .libc_failed
    mov rdx, 0x4E4F56414D414C4C
    mov [rax], rdx
    mov rdi, rax
    call free wrt ..plt
    WRITE_MESSAGE libc_message, libc_message_length
    lea rdi, [rel pthread_mutex]
    call pthread_mutex_lock wrt ..plt
    test eax, eax
    jnz .pthread_failed
    lea rdi, [rel pthread_mutex]
    call pthread_mutex_unlock wrt ..plt
    test eax, eax
    jnz .pthread_failed
    lea rdi, [rel pthread_id]
    xor rsi, rsi
    lea rdx, [rel pthread_probe]
    lea rcx, [rel pthread_value]
    call pthread_create wrt ..plt
    test eax, eax
    jnz .pthread_failed
    mov rdi, [rel pthread_id]
    xor rsi, rsi
    call pthread_join wrt ..plt
    cmp dword [rel pthread_value], 0x50544852
    jne .pthread_failed
    call nova_tls_get wrt ..plt
    mov rdx, 0x544C53494E495421
    cmp rax, rdx
    jne .tls_failed
    call nova_tls_gd_get wrt ..plt
    mov rdx, 0x4744544C53494E49
    cmp rax, rdx
    jne .tls_failed
    call nova_tlsdesc_get wrt ..plt
    mov rdx, 0x544C534445534349
    cmp rax, rdx
    jne .tls_failed
    mov rdi, 0x1111222233334444
    call nova_tls_set wrt ..plt
    mov rdi, 0x2222333344445555
    call nova_tls_gd_set wrt ..plt
    mov rdi, 0x3333444455556666
    call nova_tlsdesc_set wrt ..plt
    lea rdi, [rel tls_pthread_id]
    xor rsi, rsi
    lea rdx, [rel tls_probe]
    lea rcx, [rel tls_thread_result]
    call pthread_create wrt ..plt
    test eax, eax
    jnz .tls_failed
    mov rdi, [rel tls_pthread_id]
    xor rsi, rsi
    call pthread_join wrt ..plt
    mov rdx, 0xAAAABBBBCCCCDDDD
    cmp qword [rel tls_thread_result], rdx
    jne .tls_failed
    mov rdx, 0xDDDDEEEEFFFF0001
    cmp qword [rel tls_thread_result + 8], rdx
    jne .tls_failed
    mov rdx, 0xEEEEFFFF00011112
    cmp qword [rel tls_thread_result + 16], rdx
    jne .tls_failed
    call nova_tls_get wrt ..plt
    mov rdx, 0x1111222233334444
    cmp rax, rdx
    jne .tls_failed
    call nova_tls_gd_get wrt ..plt
    mov rdx, 0x2222333344445555
    cmp rax, rdx
    jne .tls_failed
    call nova_tlsdesc_get wrt ..plt
    mov rdx, 0x3333444455556666
    cmp rax, rdx
    jne .tls_failed
    WRITE_MESSAGE tls_message, tls_message_length
    jmp .tls_done
.tls_failed:
    WRITE_MESSAGE tls_failure, tls_failure_length
.tls_done:
    WRITE_MESSAGE pthread_message, pthread_message_length
    jmp .libc_pthread_done
.libc_failed:
    WRITE_MESSAGE libc_failure, libc_failure_length
    jmp .libc_pthread_done
.pthread_failed:
    WRITE_MESSAGE pthread_failure, pthread_failure_length
.libc_pthread_done:

    ; Exercise the Linux-compatible IPC surface used by Chromium's event
    ; loops: pipe2 -> epoll_ctl -> epoll_wait -> read.
    mov rax, 293
    lea rdi, [rel pipe_fds]
    xor rsi, rsi
    syscall
    test rax, rax
    jnz .ipc_failed

    mov rax, 1
    mov edi, [rel pipe_fds + 4]
    lea rsi, [rel pipe_payload]
    mov rdx, 1
    syscall
    cmp rax, 1
    jne .ipc_failed

    mov rax, 291
    xor rdi, rdi
    syscall
    test rax, rax
    js .ipc_failed
    mov r12, rax

    mov rax, 233
    mov rdi, r12
    mov rsi, 1
    mov edx, [rel pipe_fds]
    lea r10, [rel epoll_event]
    syscall
    test rax, rax
    jnz .ipc_failed

    mov rax, 232
    mov rdi, r12
    lea rsi, [rel epoll_result]
    mov rdx, 1
    xor r10, r10
    syscall
    cmp rax, 1
    jne .ipc_failed
    cmp dword [rel epoll_result], 1
    jne .ipc_failed
    cmp qword [rel epoll_result + 4], 0x45504F4C
    jne .ipc_failed

    mov rax, 0
    mov edi, [rel pipe_fds]
    lea rsi, [rel pipe_result]
    mov rdx, 1
    syscall
    cmp rax, 1
    jne .ipc_failed
    cmp byte [rel pipe_result], 'P'
    jne .ipc_failed

    ; eventfd2 supplies Chromium-style wakeups and must participate in epoll.
    mov rax, 290
    xor rdi, rdi
    xor rsi, rsi
    syscall
    test rax, rax
    js .ipc_failed
    mov [rel eventfd_fd], eax

    mov qword [rel eventfd_value], 7
    mov rax, 1
    mov edi, [rel eventfd_fd]
    lea rsi, [rel eventfd_value]
    mov rdx, 8
    syscall
    cmp rax, 8
    jne .ipc_failed

    mov rax, 233
    mov rdi, r12
    mov rsi, 1
    mov edx, [rel eventfd_fd]
    lea r10, [rel eventfd_epoll_event]
    syscall
    test rax, rax
    jnz .ipc_failed

    mov rax, 232
    mov rdi, r12
    lea rsi, [rel epoll_result]
    mov rdx, 1
    xor r10, r10
    syscall
    cmp rax, 1
    jne .ipc_failed
    cmp dword [rel epoll_result], 1
    jne .ipc_failed
    cmp qword [rel epoll_result + 4], 0x45564644
    jne .ipc_failed

    mov qword [rel eventfd_value], 0
    mov rax, 0
    mov edi, [rel eventfd_fd]
    lea rsi, [rel eventfd_value]
    mov rdx, 8
    syscall
    cmp rax, 8
    jne .ipc_failed
    cmp qword [rel eventfd_value], 7
    jne .ipc_failed

    mov rax, 283
    mov rdi, 1
    xor rsi, rsi
    syscall
    test rax, rax
    js .ipc_failed
    mov [rel timerfd_fd], eax

    mov rax, 233
    mov rdi, r12
    mov rsi, 1
    mov edx, [rel timerfd_fd]
    lea r10, [rel timerfd_epoll_event]
    syscall
    test rax, rax
    jnz .ipc_failed

    mov rax, 286
    mov edi, [rel timerfd_fd]
    xor rsi, rsi
    lea rdx, [rel timerfd_spec]
    xor r10, r10
    syscall
    test rax, rax
    jnz .ipc_failed

    mov rax, 35
    lea rdi, [rel timerfd_sleep]
    xor rsi, rsi
    syscall

    mov rax, 232
    mov rdi, r12
    lea rsi, [rel epoll_result]
    mov rdx, 1
    xor r10, r10
    syscall
    cmp rax, 1
    jne .ipc_failed
    cmp dword [rel epoll_result], 1
    jne .ipc_failed
    cmp qword [rel epoll_result + 4], 0x54494D52
    jne .ipc_failed

    mov rax, 0
    mov edi, [rel timerfd_fd]
    lea rsi, [rel timerfd_expirations]
    mov rdx, 8
    syscall
    cmp rax, 8
    jne .ipc_failed
    cmp qword [rel timerfd_expirations], 1
    jne .ipc_failed

    ; A memfd is a real shared frame object: independent mmap calls must see
    ; the same writes, and mappings must remain valid after the FD is closed.
    mov rax, 319
    lea rdi, [rel memfd_name]
    xor rsi, rsi
    syscall
    test rax, rax
    js .ipc_failed
    mov [rel memfd_fd], eax

    mov rax, 77
    mov edi, [rel memfd_fd]
    mov rsi, 8192
    syscall
    test rax, rax
    jnz .ipc_failed

    mov rax, 9
    xor rdi, rdi
    mov rsi, 8192
    mov rdx, 3
    mov r10, 1
    mov r8d, [rel memfd_fd]
    xor r9, r9
    syscall
    test rax, rax
    js .ipc_failed
    mov [rel memfd_map_one], rax

    mov rax, 9
    xor rdi, rdi
    mov rsi, 8192
    mov rdx, 3
    mov r10, 1
    mov r8d, [rel memfd_fd]
    xor r9, r9
    syscall
    test rax, rax
    js .ipc_failed
    mov [rel memfd_map_two], rax

    mov rbx, [rel memfd_map_one]
    mov rdx, 0x4D454D4644534841
    mov [rbx + 4096], rdx
    mov rbx, [rel memfd_map_two]
    cmp [rbx + 4096], rdx
    jne .ipc_failed

    mov rax, 8
    mov edi, [rel memfd_fd]
    mov rsi, 4096
    xor rdx, rdx
    syscall
    cmp rax, 4096
    jne .ipc_failed
    mov rax, 0
    mov edi, [rel memfd_fd]
    lea rsi, [rel memfd_file_value]
    mov rdx, 8
    syscall
    cmp rax, 8
    jne .ipc_failed
    mov rdx, 0x4D454D4644534841
    cmp qword [rel memfd_file_value], rdx
    jne .ipc_failed

    mov rax, 57
    syscall
    test rax, rax
    jz .memfd_child
    test rax, rax
    js .ipc_failed
    mov [rel memfd_child_pid], rax
.memfd_parent_wait:
    mov rax, 61
    mov rdi, [rel memfd_child_pid]
    lea rsi, [rel memfd_child_status]
    mov rdx, 1
    xor r10, r10
    syscall
    cmp rax, [rel memfd_child_pid]
    je .memfd_child_reaped
    mov rax, 24
    syscall
    jmp .memfd_parent_wait
.memfd_child_reaped:
    mov rbx, [rel memfd_map_one]
    mov rdx, 0x50524F434D454D46
    cmp [rbx + 256], rdx
    jne .ipc_failed

    mov rax, 3
    mov edi, [rel memfd_fd]
    syscall
    test rax, rax
    jnz .ipc_failed
    mov rbx, [rel memfd_map_two]
    mov rdx, 0x434C4F5345444D41
    mov [rbx + 128], rdx
    mov rbx, [rel memfd_map_one]
    cmp [rbx + 128], rdx
    jne .ipc_failed
    jmp .memfd_process_done

.memfd_child:
    mov rax, 9
    xor rdi, rdi
    mov rsi, 8192
    mov rdx, 3
    mov r10, 1
    mov r8d, [rel memfd_fd]
    xor r9, r9
    syscall
    test rax, rax
    js .memfd_child_failed
    mov rdx, 0x50524F434D454D46
    mov [rax + 256], rdx
    mov rax, 60
    xor rdi, rdi
    syscall
.memfd_child_failed:
    mov rax, 60
    mov rdi, 1
    syscall
.memfd_process_done:

    ; socketpair(AF_UNIX, SOCK_STREAM) is duplex. Verify both directions.
    mov rax, 53
    mov rdi, 1
    mov rsi, 1
    xor rdx, rdx
    lea r10, [rel socket_fds]
    syscall
    test rax, rax
    jnz .ipc_failed

    mov rax, 1
    mov edi, [rel socket_fds]
    lea rsi, [rel socket_payload]
    mov rdx, 1
    syscall
    cmp rax, 1
    jne .ipc_failed
    mov eax, [rel socket_fds + 4]
    mov [rel socket_pollfd], eax
    mov word [rel socket_pollfd + 4], 1
    mov word [rel socket_pollfd + 6], 0
    mov rax, 7
    lea rdi, [rel socket_pollfd]
    mov rsi, 1
    xor rdx, rdx
    syscall
    cmp rax, 1
    jne .ipc_failed
    test word [rel socket_pollfd + 6], 1
    jz .ipc_failed
    mov rax, 0
    mov edi, [rel socket_fds + 4]
    lea rsi, [rel socket_result]
    mov rdx, 1
    syscall
    cmp rax, 1
    jne .ipc_failed
    cmp byte [rel socket_result], 'S'
    jne .ipc_failed

    mov rax, 1
    mov edi, [rel socket_fds + 4]
    lea rsi, [rel socket_payload + 1]
    mov rdx, 1
    syscall
    cmp rax, 1
    jne .ipc_failed
    mov rax, 0
    mov edi, [rel socket_fds]
    lea rsi, [rel socket_result + 1]
    mov rdx, 1
    syscall
    cmp rax, 1
    jne .ipc_failed
    cmp byte [rel socket_result + 1], 'U'
    jne .ipc_failed
    WRITE_MESSAGE ipc_message, ipc_message_length
    jmp .ipc_done
.ipc_failed:
    WRITE_MESSAGE ipc_failure, ipc_failure_length
.ipc_done:

    ; Build a real nested NovaFS path, create a symbolic link, follow it via
    ; openat, and enumerate the directory through getdents64.
    mov rax, 258
    mov rdi, -100
    lea rsi, [rel chrome_dir]
    mov rdx, 0755o
    syscall
    test rax, rax
    jz .chrome_dir_ready
    cmp rax, -17
    jne .directory_failed
.chrome_dir_ready:
    mov rax, 258
    mov rdi, -100
    lea rsi, [rel chrome_lib_dir]
    mov rdx, 0755o
    syscall
    test rax, rax
    jz .chrome_lib_dir_ready
    cmp rax, -17
    jne .directory_failed
.chrome_lib_dir_ready:

    mov rax, 266
    lea rdi, [rel symlink_target]
    mov rsi, -100
    lea rdx, [rel symlink_path]
    syscall
    test rax, rax
    jz .symlink_ready
    cmp rax, -17
    jne .directory_failed
.symlink_ready:
    mov rax, 267
    mov rdi, -100
    lea rsi, [rel symlink_path]
    lea rdx, [rel symlink_result]
    mov r10, 39
    syscall
    cmp rax, 11
    jne .directory_failed
    cmp dword [rel symlink_result], 0x62696C2F
    jne .directory_failed
    cmp dword [rel symlink_result + 4], 0x61766F6E
    jne .directory_failed
    cmp word [rel symlink_result + 8], 0x732E
    jne .directory_failed
    cmp byte [rel symlink_result + 10], 'o'
    jne .directory_failed

    mov rax, 257
    mov rdi, -100
    lea rsi, [rel symlink_path]
    xor rdx, rdx
    xor r10, r10
    syscall
    test rax, rax
    js .directory_failed
    mov r12, rax
    mov rax, 0
    mov rdi, r12
    lea rsi, [rel symlink_elf_magic]
    mov rdx, 4
    syscall
    cmp rax, 4
    jne .directory_failed
    cmp dword [rel symlink_elf_magic], 0x464C457F
    jne .directory_failed
    mov rax, 3
    mov rdi, r12
    syscall

    mov rax, 257
    mov rdi, -100
    lea rsi, [rel chrome_dir]
    mov rdx, 0x10000
    xor r10, r10
    syscall
    test rax, rax
    js .directory_failed
    mov r12, rax
    mov rax, 217
    mov rdi, r12
    lea rsi, [rel directory_entries]
    mov rdx, 128
    syscall
    test rax, rax
    jle .directory_failed
    mov rax, 3
    mov rdi, r12
    syscall
    WRITE_MESSAGE directory_message, directory_message_length
    jmp .directory_done
.directory_failed:
    WRITE_MESSAGE directory_failure, directory_failure_length
.directory_done:

    ; V8-class runtimes require a wide canonical address range and W^X JIT
    ; transitions. First prove a mapping above 1 TiB, then generate a tiny
    ; function as RW data, seal it RX, and execute it.
    mov r12, 0x0000010000000000
    mov rax, 9
    mov rdi, r12
    mov rsi, 4096
    mov rdx, 3
    mov r10, 0x32
    mov r8, -1
    xor r9, r9
    syscall
    cmp rax, r12
    je .jit_wide_ready
    test rax, rax
    js .jit_wide_error
    jmp .jit_wide_moved
.jit_wide_ready:
    mov dword [rax], 0x57494445
    mov rax, 11
    mov rdi, r12
    mov rsi, 4096
    syscall

    mov rax, 9
    xor rdi, rdi
    mov rsi, 4096
    mov rdx, 7
    mov r10, 0x22
    mov r8, -1
    xor r9, r9
    syscall
    test rax, rax
    jns .jit_wx_failed

    mov rax, 9
    xor rdi, rdi
    mov rsi, 4096
    mov rdx, 3
    mov r10, 0x22
    mov r8, -1
    xor r9, r9
    syscall
    test rax, rax
    js .jit_failed
    mov r12, rax
    mov byte [r12], 0xB8
    mov dword [r12 + 1], 0x4A495421
    mov byte [r12 + 5], 0xC3
    mov rax, 10
    mov rdi, r12
    mov rsi, 4096
    mov rdx, 5
    syscall
    test rax, rax
    jnz .jit_failed
    call r12
    cmp eax, 0x4A495421
    jne .jit_failed
    mov rax, 11
    mov rdi, r12
    mov rsi, 4096
    syscall
    WRITE_MESSAGE jit_message, jit_message_length
    jmp .jit_done
.jit_failed:
    WRITE_MESSAGE jit_failure, jit_failure_length
    jmp .jit_done
.jit_wide_error:
    cmp rax, -12
    je .jit_wide_enomem
    cmp rax, -22
    je .jit_wide_einval
    WRITE_MESSAGE jit_wide_error, jit_wide_error_length
    jmp .jit_done
.jit_wide_enomem:
    WRITE_MESSAGE jit_wide_enomem, jit_wide_enomem_length
    jmp .jit_done
.jit_wide_einval:
    WRITE_MESSAGE jit_wide_einval, jit_wide_einval_length
    jmp .jit_done
.jit_wide_moved:
    WRITE_MESSAGE jit_wide_moved, jit_wide_moved_length
    jmp .jit_done
.jit_wx_failed:
    WRITE_MESSAGE jit_wx_failure, jit_wx_failure_length
.jit_done:

    ; Start a disposable renderer-like child and lock it into a kernel-enforced
    ; no_new_privs + seccomp strict syscall policy.
    mov rax, 57
    syscall
    test rax, rax
    jz .sandbox_child
    mov r12, rax
.sandbox_wait:
    mov rax, 61
    mov rdi, r12
    lea rsi, [rel sandbox_status]
    mov rdx, 1
    xor r10, r10
    syscall
    test rax, rax
    jnz .sandbox_reaped
    mov rax, 24
    syscall
    jmp .sandbox_wait
.sandbox_reaped:
    cmp rax, r12
    jne .sandbox_failed
    cmp dword [rel sandbox_status], 0
    jne .sandbox_failed
    WRITE_MESSAGE sandbox_message, sandbox_message_length
    jmp .sandbox_done
.sandbox_failed:
    WRITE_MESSAGE sandbox_failure, sandbox_failure_length
.sandbox_done:

    ; mmap(NULL, 8192, PROT_READ|PROT_WRITE,
    ;      MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)
    mov rax, 9
    xor rdi, rdi
    mov rsi, 8192
    mov rdx, 3
    mov r10, 0x22
    mov r8, -1
    xor r9, r9
    syscall
    mov r14, rax
    mov dword [r14], 0

    ; clone(CLONE_VM, child_stack, ...): the child is a real scheduled thread
    ; sharing this process address space. It wakes the parent through futex.
    mov rax, 56
    mov rdi, 0x100
    lea rsi, [r14 + 8192]
    xor rdx, rdx
    xor r10, r10
    xor r8, r8
    syscall
    test rax, rax
    jz .thread_child

    cmp dword [r14], 1
    je .thread_ready
    mov rax, 202
    mov rdi, r14
    xor rsi, rsi
    xor rdx, rdx
    xor r10, r10
    syscall
.thread_ready:
    WRITE_MESSAGE thread_message, thread_message_length

    ; Map the first page of init.elf through NovaFS and verify the ELF magic.
    mov rax, 257
    mov rdi, -100
    lea rsi, [rel init_path]
    xor rdx, rdx
    xor r10, r10
    syscall
    test rax, rax
    js .file_mapping_failed
    mov r15, rax
    mov rax, 9
    xor rdi, rdi
    mov rsi, 4096
    mov rdx, 1
    mov r10, 2
    mov r8, r15
    xor r9, r9
    syscall
    test rax, rax
    js .file_mapping_failed
    mov rbx, rax
    cmp dword [rbx], 0x464C457F
    jne .file_mapping_failed
    WRITE_MESSAGE file_message, file_message_length
    mov rax, 11
    mov rdi, rbx
    mov rsi, 4096
    syscall
    mov rax, 3
    mov rdi, r15
    syscall
    jmp .file_mapping_done

.file_mapping_failed:
    WRITE_MESSAGE file_failure_message, file_failure_message_length
.file_mapping_done:

    ; Anonymous MAP_SHARED pages keep the same writable physical frames after
    ; fork, unlike the private mapping in r14 which becomes copy-on-write.
    mov rax, 9
    xor rdi, rdi
    mov rsi, 4096
    mov rdx, 3
    mov r10, 0x21
    mov r8, -1
    xor r9, r9
    syscall
    test rax, rax
    js .shared_mapping_failed
    mov r13, rax
    mov dword [r13], 0

    ; fork() uses COW for r14 while preserving r13 as shared memory.
    mov rax, 57
    syscall
    test rax, rax
    jz .fork_child

    mov rcx, 2000000
.parent_delay:
    pause
    loop .parent_delay
    cmp dword [r14], 1
    jne .isolation_failed
    cmp dword [r13], 0x53484D45
    jne .isolation_failed
    WRITE_MESSAGE parent_message, parent_message_length
.parent_loop:
    inc r12
    jmp .parent_loop

.isolation_failed:
    WRITE_MESSAGE failure_message, failure_message_length
    jmp .parent_loop

.shared_mapping_failed:
    WRITE_MESSAGE shared_mapping_failure, shared_mapping_failure_length
    jmp .parent_loop

.thread_child:
    mov dword [r14], 1
    mov rax, 202
    mov rdi, r14
    mov rsi, 1
    mov rdx, 1
    xor r10, r10
    syscall
    mov rax, 60
    xor rdi, rdi
    syscall
    jmp $

.fork_child:
    mov dword [r14], 2
    mov dword [r13], 0x53484D45
    WRITE_MESSAGE child_message, child_message_length
.child_loop:
    inc r11
    jmp .child_loop

.sandbox_child:
    mov rax, 157
    mov rdi, 38
    mov rsi, 1
    xor rdx, rdx
    xor r10, r10
    xor r8, r8
    syscall
    test rax, rax
    jnz .sandbox_child_failed
    mov rax, 317
    xor rdi, rdi
    xor rsi, rsi
    xor rdx, rdx
    syscall
    test rax, rax
    jnz .sandbox_child_failed
    mov rax, 39
    syscall
    cmp rax, -1
    jne .sandbox_child_failed
    WRITE_MESSAGE sandbox_child_message, sandbox_child_message_length
    mov rax, 60
    xor rdi, rdi
    syscall
.sandbox_child_failed:
    mov rax, 60
    mov rdi, 1
    syscall
    jmp $

pthread_probe:
    mov dword [rdi], 0x50544852
    xor eax, eax
    ret

tls_probe:
    push rbx
    mov rbx, rdi
    call nova_tls_get wrt ..plt
    mov rdx, 0x544C53494E495421
    cmp rax, rdx
    jne .tls_probe_failed
    call nova_tls_gd_get wrt ..plt
    mov rdx, 0x4744544C53494E49
    cmp rax, rdx
    jne .tls_probe_failed
    call nova_tlsdesc_get wrt ..plt
    mov rdx, 0x544C534445534349
    cmp rax, rdx
    jne .tls_probe_failed
    mov rdi, 0xAAAABBBBCCCCDDDD
    call nova_tls_set wrt ..plt
    mov rdi, 0xDDDDEEEEFFFF0001
    call nova_tls_gd_set wrt ..plt
    mov rdi, 0xEEEEFFFF00011112
    call nova_tlsdesc_set wrt ..plt
    call nova_tls_get wrt ..plt
    mov [rbx], rax
    call nova_tls_gd_get wrt ..plt
    mov [rbx + 8], rax
    call nova_tlsdesc_get wrt ..plt
    mov [rbx + 16], rax
    pop rbx
    xor eax, eax
    ret
.tls_probe_failed:
    mov qword [rbx], 0
    pop rbx
    xor eax, eax
    ret

section .rodata
abi_message db 'init.elf: Linux x86-64 syscall ABI entered Ring 3', 13, 10
abi_message_length equ $ - abi_message
constructor_message db 'init.elf: DT_INIT_ARRAY user-mode constructor bootstrap verified', 13, 10
constructor_message_length equ $ - constructor_message
constructor_failure db 'init.elf: ERROR shared-object constructor was not executed', 13, 10
constructor_failure_length equ $ - constructor_failure
demand_message db 'init.elf: 48-bit high stack demand paging verified', 13, 10
demand_message_length equ $ - demand_message
simd_message db 'init.elf: per-thread x87/SSE context switching verified', 13, 10
simd_message_length equ $ - simd_message
simd_failure db 'init.elf: ERROR x87/SSE context was corrupted', 13, 10
simd_failure_length equ $ - simd_failure
libm_message db 'init.elf: versioned libm.so.6 SSE/x87 ABI verified', 13, 10
libm_message_length equ $ - libm_message
libm_failure db 'init.elf: ERROR libm.so.6 result mismatch', 13, 10
libm_failure_length equ $ - libm_failure
unwind_message db 'init.elf: versioned libgcc_s.so.1 unwind ABI verified', 13, 10
unwind_message_length equ $ - unwind_message
unwind_failure db 'init.elf: ERROR libgcc_s.so.1 unwind ABI failed', 13, 10
unwind_failure_length equ $ - unwind_failure
shared_link_message db 'init.elf: PT_INTERP, DT_NEEDED and PLT symbol resolution verified', 13, 10
shared_link_message_length equ $ - shared_link_message
shared_link_failure db 'init.elf: ERROR shared symbol resolution failed', 13, 10
shared_link_failure_length equ $ - shared_link_failure
libc_probe_text db 'Nova libc ABI', 0
libc_probe_text_length equ $ - libc_probe_text - 1
libc_message db 'init.elf: GLIBC string and allocator ABI verified', 13, 10
libc_message_length equ $ - libc_message
libc_failure db 'init.elf: ERROR libc.so.6 ABI failed', 13, 10
libc_failure_length equ $ - libc_failure
pthread_message db 'init.elf: libpthread.so.0 create/join ABI verified', 13, 10
pthread_message_length equ $ - pthread_message
pthread_failure db 'init.elf: ERROR libpthread.so.0 ABI failed', 13, 10
pthread_failure_length equ $ - pthread_failure
tls_message db 'init.elf: PT_TLS IE/GD/TLSDESC thread isolation verified', 13, 10
tls_message_length equ $ - tls_message
tls_failure db 'init.elf: ERROR PT_TLS per-thread isolation failed', 13, 10
tls_failure_length equ $ - tls_failure
ipc_message db 'init.elf: memfd shared frames and fd event IPC verified', 13, 10
ipc_message_length equ $ - ipc_message
ipc_failure db 'init.elf: ERROR IPC compatibility failed', 13, 10
ipc_failure_length equ $ - ipc_failure
directory_message db 'init.elf: nested directories, symlink and getdents64 verified', 13, 10
directory_message_length equ $ - directory_message
directory_failure db 'init.elf: ERROR directory or symlink ABI failed', 13, 10
directory_failure_length equ $ - directory_failure
jit_message db 'init.elf: wide virtual address and W^X JIT transition verified', 13, 10
jit_message_length equ $ - jit_message
jit_failure db 'init.elf: ERROR wide VM or W^X JIT transition failed', 13, 10
jit_failure_length equ $ - jit_failure
jit_wide_error db 'init.elf: ERROR high MAP_FIXED returned a kernel error', 13, 10
jit_wide_error_length equ $ - jit_wide_error
jit_wide_enomem db 'init.elf: ERROR high MAP_FIXED returned ENOMEM', 13, 10
jit_wide_enomem_length equ $ - jit_wide_enomem
jit_wide_einval db 'init.elf: ERROR high MAP_FIXED returned EINVAL', 13, 10
jit_wide_einval_length equ $ - jit_wide_einval
jit_wide_moved db 'init.elf: ERROR MAP_FIXED address was unexpectedly moved', 13, 10
jit_wide_moved_length equ $ - jit_wide_moved
jit_wx_failure db 'init.elf: ERROR writable-executable mapping was accepted', 13, 10
jit_wx_failure_length equ $ - jit_wx_failure
sandbox_child_message db 'sandbox child: forbidden syscall rejected by seccomp', 13, 10
sandbox_child_message_length equ $ - sandbox_child_message
sandbox_message db 'init.elf: no_new_privs multiprocess sandbox verified', 13, 10
sandbox_message_length equ $ - sandbox_message
sandbox_failure db 'init.elf: ERROR multiprocess sandbox failed', 13, 10
sandbox_failure_length equ $ - sandbox_failure
thread_message db 'init.elf: clone thread and futex wake verified', 13, 10
thread_message_length equ $ - thread_message
file_message db 'init.elf: NovaFS file-backed mmap verified', 13, 10
file_message_length equ $ - file_message
file_failure_message db 'init.elf: ERROR file-backed mmap failed', 13, 10
file_failure_message_length equ $ - file_failure_message
child_message db 'init.elf: fork child running in an isolated CR3 address space', 13, 10
child_message_length equ $ - child_message
parent_message db 'init.elf: fork COW isolation and MAP_SHARED verified', 13, 10
parent_message_length equ $ - parent_message
failure_message db 'init.elf: ERROR address-space isolation failed', 13, 10
failure_message_length equ $ - failure_message
shared_mapping_failure db 'init.elf: ERROR anonymous MAP_SHARED failed', 13, 10
shared_mapping_failure_length equ $ - shared_mapping_failure
init_path db 'init.elf', 0
chrome_dir db 'chrome', 0
chrome_lib_dir db 'chrome/lib', 0
symlink_target db '/libnova.so', 0
symlink_path db 'chrome/lib/libnova.so', 0
memfd_name db 'nova-shared-surface', 0

section .data
abi_pointer dq abi_message
pthread_id dq 0
pthread_value dd 0
align 8
tls_pthread_id dq 0
tls_thread_result dq 0, 0, 0
align 8
pthread_mutex times 8 dq 0
align 8
unwind_context dq 0x4E4F5641, 0
sandbox_status dd 0
pipe_fds dd 0, 0
eventfd_fd dd 0
timerfd_fd dd 0
memfd_fd dd 0
socket_fds dd 0, 0
socket_pollfd dd 0
    dw 0, 0
pipe_payload db 'P'
pipe_result db 0
socket_payload db 'S', 'U'
socket_result db 0, 0
epoll_event:
    dd 1
    dq 0x45504F4C
eventfd_epoll_event:
    dd 1
    dq 0x45564644
timerfd_epoll_event:
    dd 1
    dq 0x54494D52
epoll_result:
    times 12 db 0
eventfd_value dq 0
timerfd_spec:
    dq 0, 0
    dq 0, 10000000
timerfd_sleep:
    dq 0, 20000000
timerfd_expirations dq 0
memfd_map_one dq 0
memfd_map_two dq 0
memfd_file_value dq 0
memfd_child_pid dq 0
memfd_child_status dd 0
symlink_result:
    times 40 db 0
symlink_elf_magic dd 0
directory_entries:
    times 128 db 0
