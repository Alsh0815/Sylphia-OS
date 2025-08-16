OUTPUT_FORMAT(elf64-x86-64)
ENTRY(kernel_main)
SECTIONS
{
  . = 0x100000;
  .text : {
    *(.text .text.*)
  }
  .rodata : ALIGN(4K) {
    *(.rodata .rodata.*)
  }
  .data : ALIGN(4K) { 
    *(.data .data.*) 
  }
  .bss : ALIGN(4K) { 
    *(COMMON) *(.bss .bss.*) 
  }
}