ENTRY(_start)
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

PHDRS
{
    code PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x00010000;

    .text : ALIGN(16)
    {
        KEEP(*(.text.entry))
        *(.text*)
    } :code

    .rodata : ALIGN(16)
    {
        *(.rodata*)
    } :code

    .data : ALIGN(16)
    {
        *(.data*)
    } :data

    /* Keep large frame buffers above the legacy VGA/BIOS memory hole. */
    ASSERT(. < 0x000A0000, "loadable kernel overlaps legacy VGA memory")
    . = 0x00100000;
    .bss : ALIGN(16)
    {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        __bss_end = .;
    } :data
    ASSERT(__bss_end < 0x00F00000, "kernel BSS collides with stack guard")

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
