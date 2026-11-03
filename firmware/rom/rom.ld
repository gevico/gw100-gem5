/* 启动 ROM 的链接脚本：代码和只读数据在 ROM，BSS 在 SRAM 末尾的 ROM 保留区 */
ENTRY(_start)
MEMORY
{
    ROM (rx) : ORIGIN = 0x00000000, LENGTH = 64K
    RSV (rw) : ORIGIN = 0x101FF000, LENGTH = 4K
}
SECTIONS
{
    .text : { *(.text.start) *(.text*) } > ROM
    .rodata : { *(.rodata*) *(.srodata*) } > ROM
    .bootinfo (NOLOAD) : { KEEP(*(.bootinfo)) } > RSV
    .bss (NOLOAD) : {
        . = ALIGN(8);
        __bss_start = .;
        *(.bss*) *(.sbss*) *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > RSV
    .data : { *(.data*) *(.sdata*) } > RSV
    ASSERT(SIZEOF(.data) == 0, "启动 ROM 不允许有已初始化的可写数据")
    /DISCARD/ : { *(.comment) *(.note*) *(.riscv.attributes) }
}
