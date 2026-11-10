ENTRY(play_overlay_entry)

MEMORY
{
    GLYPH_RAM (rx) : ORIGIN = 0x03092000, LENGTH = 0xD000
}

SECTIONS
{
    .overlay :
    {
        LONG(0) LONG(0) LONG(0) LONG(0)
        KEEP(*(.rodata.PLAY_SCAN_ENTRY))
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > GLYPH_RAM

    .data : { *(.data .data.*) } > GLYPH_RAM
    .bss (NOLOAD) : { *(.bss .bss.* COMMON) } > GLYPH_RAM

    .comment 0 : { *(.comment) }
    .ARM.attributes 0 : { *(.ARM.attributes) }

    /DISCARD/ :
    {
        *(.ARM.exidx*)
        *(.ARM.extab*)
    }
}

ASSERT(ADDR(.overlay) == 0x03092000, "the overlay must start at 0x03092000")
ASSERT(PLAY_SCAN_ENTRY == 0x03092010, "the entry table must follow the 16-byte header")
ASSERT(SIZEOF(.data) == 0 && SIZEOF(.bss) == 0, "the overlay may only hold code and constants")
