[bits 64]

global nova_shared_probe

section .text
nova_shared_probe:
    mov eax, 0x4E4F5641
    ret
