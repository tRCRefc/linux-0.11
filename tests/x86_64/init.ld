PHDRS
{
    load PT_LOAD FLAGS(7);
}

SECTIONS
{
    . = 0x0000008000000000;
    .text : ALIGN(4096) { *(.text*) } :load
    .rodata : { *(.rodata*) } :load
    .data : { *(.data*) } :load
    .bss : { *(.bss*) *(COMMON) } :load
    /DISCARD/ : { *(.comment*) *(.note*) }
}
