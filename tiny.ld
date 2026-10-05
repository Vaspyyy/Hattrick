/* One RWX load segment plus only the program headers the loader needs. */
ENTRY(_start)
PHDRS {
  interp PT_INTERP;
  load   PT_LOAD FILEHDR PHDRS FLAGS(7);
  dyn    PT_DYNAMIC;
  stack  PT_GNU_STACK FLAGS(6);
}
SECTIONS {
  . = 0x400000 + SIZEOF_HEADERS;
  .interp  : { *(.interp) } :interp :load
  .hash    : { *(.hash) *(.gnu.hash) } :load
  .dynsym  : { *(.dynsym) }
  .dynstr  : { *(.dynstr) }
  .rela.dyn : { *(.rela.*) }
  .rel.dyn : { *(.rel.*) }
  .text    : { *(.text*) *(.rodata*) *(.data*) }
  .dynamic : { *(.dynamic) } :load :dyn
  .got     : { *(.got*) } :load
  .bss     : { *(.bss*) *(COMMON) }
  /DISCARD/ : { *(.note*) *(.eh_frame*) *(.comment) }
}
