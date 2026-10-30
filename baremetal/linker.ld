/*
 * 裸机测试的链接脚本：程序从 host DRAM 起始处（0x80000000）加载运行。
 * .bss 和栈按 16 B 对齐，start.S 依赖 __bss_start、__bss_end、__stack_top。
 */

ENTRY(_start)

SECTIONS
{
    . = 0x80000000;

    .text : {
        KEEP(*(.text.boot))
        *(.text.vectors)
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }

    .stack (NOLOAD) : ALIGN(16) {
        . += 0x10000;
        __stack_top = .;
    }
}
