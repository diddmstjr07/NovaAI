ENTRY(_start)
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

PHDRS
{
    text PT_LOAD FLAGS(5);
}

SECTIONS
{
    . = 0x40001000;
    .text : ALIGN(16) { *(.text*) } :text
    .rodata : ALIGN(16) { *(.rodata*) } :text
    /DISCARD/ : { *(.comment) *(.note*) }
}
