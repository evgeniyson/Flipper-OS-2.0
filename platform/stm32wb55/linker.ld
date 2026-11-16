/* STM32WB55RG, application core.
 * The image starts at the beginning of flash. The top of flash holds the
 * radio co-processor firmware; the secure-flash boundary keeps it write
 * protected, and this firmware never programs flash anyway. 512K is far more
 * than needed and stays well clear of that area. */
ENTRY(Reset_Handler)

MEMORY {
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    RAM   (rwx) : ORIGIN = 0x20000008, LENGTH = 0x2FFF8   /* SRAM1, 192K */
}

_stack_top = ORIGIN(RAM) + LENGTH(RAM);
_stack_size = 0x1000;

SECTIONS {
    .isr_vector : {
        KEEP(*(.isr_vector))
    } > FLASH

    .text : {
        . = ALIGN(4);
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > FLASH

    .ARM.exidx : {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > FLASH

    _sidata = LOADADDR(.data);
    .data : {
        . = ALIGN(4);
        _sdata = .;
        *(.data .data.*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    .bss (NOLOAD) : {
        . = ALIGN(4);
        _sbss = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /* survives a reset: carries the "enter DFU" request across it */
    .noinit (NOLOAD) : {
        . = ALIGN(4);
        *(.noinit .noinit.*)
        . = ALIGN(4);
    } > RAM

    .stack_guard (NOLOAD) : {
        . = ALIGN(8);
        _stack_limit = .;
        . = . + _stack_size;
    } > RAM

    ASSERT(_stack_limit + _stack_size <= _stack_top, "RAM overflow")

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.comment)
    }
}
