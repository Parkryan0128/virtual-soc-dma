OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY { RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 128M }
PHDRS { code PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
  . = ORIGIN(RAM);
  .text : { KEEP(*(.text.start)) *(.text*) } > RAM :code
  .rodata : { *(.rodata*) } > RAM :code
  . = ALIGN(4096);
  .data : { *(.data*) *(.sdata*) } > RAM :data
  .bss (NOLOAD) : { . = ALIGN(16); __bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(16); __bss_end = .; } > RAM :data
  __stack_top = ORIGIN(RAM) + LENGTH(RAM) - 4096;
  ASSERT(__bss_end < __stack_top - 65536, "firmware exceeds RAM")
}
