/* 固件的链接脚本：镜像从装载区起始处开始，前 64 B 是镜像头 */
ENTRY(_start)
MEMORY
{
    LOAD (rwx) : ORIGIN = 0x10000000, LENGTH = 1M
    RUN  (rw)  : ORIGIN = 0x10100000, LENGTH = 1M - 4K
}
SECTIONS
{
    .header : { KEEP(*(.image_header)) . = 64; } > LOAD
    .text : { *(.text.entry) *(.text*) } > LOAD
    .rodata : { *(.rodata*) *(.srodata*) } > LOAD
    .data : { *(.data*) *(.sdata*) . = ALIGN(8); } > LOAD
    .bss (NOLOAD) : {
        . = ALIGN(8);
        __bss_start = .;
        *(.bss*) *(.sbss*) *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > RUN
    /DISCARD/ : { *(.comment) *(.note*) *(.riscv.attributes) }
}
