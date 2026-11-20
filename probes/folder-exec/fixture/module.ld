PHDRS {
 text PT_LOAD FLAGS(5);
 ro PT_LOAD FLAGS(4);
 data PT_LOAD FLAGS(6);
 dynamic PT_DYNAMIC FLAGS(6);
}
SECTIONS {
 . = 0;
 .text : { *(.text*) *(.plt*) } :text
 . = ALIGN(0x1000);
 .rodata : { BYTE(0); *(.rodata*) } :ro
 .dynsym : { *(.dynsym) } :ro
 .dynstr : { *(.dynstr) } :ro
 .hash : { *(.hash) } :ro
 .gnu.hash : { *(.gnu.hash) } :ro
 .rela.dyn : { *(.rela*) } :ro
 . = ALIGN(0x1000);
 .data : { *(.data*) *(.got*) } :data
 .dynamic : { *(.dynamic) } :data :dynamic
 .bss (NOLOAD) : { *(.bss*) *(COMMON) } :data
 /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
