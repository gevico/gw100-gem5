/* 多 ISA 可行性测试的 RV32 程序：代码、数据和栈都在核自己的 64 KiB 存储中 */
ENTRY(_start)
MEMORY
{
    RAM (rwx) : ORIGIN = 0x00000000, LENGTH = 64K
}
SECTIONS
{
    .text : { *(.text.start) *(.text*) } > RAM
    .rodata : { *(.rodata*) *(.srodata*) } > RAM
    .data : { *(.data*) *(.sdata*) } > RAM
    .bss (NOLOAD) : {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss*) *(.sbss*) *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM
    __stack_top = ORIGIN(RAM) + LENGTH(RAM);
    ASSERT(__bss_end <= __stack_top - 8K, "栈至少留 8 KiB")
    /DISCARD/ : { *(.comment) *(.note*) *(.riscv.attributes) }
}
