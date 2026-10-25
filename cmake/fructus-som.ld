/* The memory of SOM++ on fructus, in place of the fructus repository's
 * ld/fructus-sim.ld, which it follows below 0x0500.
 *
 *   0x0000 - 0x0002   `jmp _start', crt/crt0.s's .reset
 *   0x0003 - 0x01ff   zp_lo   .zplo, .zplo.bss       [rb, #-512..-1]
 *   0x0200 - 0x02ff   the cards of the write barrier, a byte for each 128
 *                     bytes of the low 32K, see src/memory/SlidingHeap.h
 *   0x0300 - 0x03ff   zp_hi   .zphi, .zphi.bss       [rb, #256..511]
 *   0x0400 - 0x04ff   the I/O page; 0x0420 is the console
 *   0x0500 - 0x05ff   the stack, growing down towards the I/O page, whose
 *                     guard traps an overflow. It's the C++ calls', since
 *                     the SOM frames are on the heap, and the collector's
 *                     mark stack is a table of the heap too: the deepest
 *                     seen is 180 bytes, in a collection
 *   0x0600 -          the SOM heap and its tables, up to the writable part
 *          - __ro_at  THE WRITABLE PART: the image's objects that the program
 *                     stores into (som_writable), and the C++ .data and .bss
 *   __ro_at - 0xefff  THE READ-ONLY PART: code, constants and the rest of
 *                     the image, whose only writes are the inline caches of
 *                     its methods
 *   0xf000 - 0xffcb   THE INTERPRETER, Interpreter::Start(), whose handler
 *                     for a bytecode is at 0x10000 - 16 * bytecode
 *   0xffcc - 0xffff   the vectors, isa/fructus.toml's [cpu.vectors]
 *
 * WRITES STAY BELOW 32K, because the machine has a mode where writes to the
 * top 32K go to the frame buffer: the read-only part starts at 0x8000, or
 * lower if it doesn't fit between there and the interpreter, and the
 * writable part ends where it starts.
 *
 * cmake/fructus-link.sh links twice. The first link, without __ro_at and
 * __rw_at, puts both parts right after the stack, to measure them; the
 * second puts them where they go. __heap_end, the top of the SOM heap, is
 * where the writable part starts.
 */

OUTPUT_FORMAT("elf32-fructus")
OUTPUT_ARCH(fructus)
ENTRY(_start)

__zp_base    = 0x0200;
__cards      = 0x0200;
__io         = 0x0400;
__shadow_sp  = 0x0410;
__shadow_lr  = 0x0412;
__shadow_isp = 0x0414;
__console    = 0x0420;
__uart_status  = 0x0422;    /* putchar waits on its busy bit; 0 on the simulator */
__stack_top  = 0x0600;
__heap_start = __stack_top;

SECTIONS
{
  .reset 0 : { KEEP(*(.reset)) }

  .zplo . : { *(.zplo) *(.zplo.*) }
  ASSERT(. <= 0x0200, "zp_lo overflows into the cards (0x0200)")

  .zphi 0x0300 : { *(.zphi) *(.zphi.*) }
  ASSERT(. <= 0x0400, "zp_hi overflows into the I/O page (0x0400)")

  /* the writable part */
  .rw (DEFINED(__rw_at) ? __rw_at : __stack_top) : {
    __rw_start = .;
    KEEP(*(som_writable))
    EXCLUDE_FILE(*som-image.o) *(.data .data.*)
  }
  .bss (NOLOAD) : {
    __bss_start = .;
    *(.bss) *(.bss.*) *(COMMON)
    __bss_end = .;
  }
  __rw_end = .;
  /* The SOM heap is everything up to the writable part, unless the link
     names its size: then it is that, whatever the program's size, and the
     program must fit above it. */
  __heap_end = DEFINED(__heap_size) ? __heap_start + __heap_size : __rw_start;
  ASSERT(!DEFINED(__rw_at) || __heap_end <= __rw_start,
         "the SOM heap runs into the writable part")

  /* The interpreter, Interpreter::Start(), whose handlers must be in 0xf010
     to 0xfff0: but a handler can't be at 0xffc0 or above, since the vectors
     are in its 16 bytes. Only the start of the function, before the first
     handler, can be below 0xf010. The rare paths of the function are in
     .text.unlikely, with the rest of the code. This comes before .text,
     which would take the function's section. */
  .dispatch 0xf000 : { KEEP(*(.text._ZN11Interpreter5StartEv)) }
  ASSERT(. <= 0xffcc, "the interpreter runs into the vectors (0xffcc)")

  /* the read-only part */
  .text (DEFINED(__ro_at) ? __ro_at : ALIGN(__rw_end, 2)) : {
    __ro_start = .;
    *(.text.startup) *(.text) *(.text.*)
  }
  .rodata : { *(.rodata) *(.rodata.*) }
  som_roots : { KEEP(*(som_roots)) }
  som_globals : { KEEP(*(som_globals)) }
  .data : { *(.data) *(.data.*) }
  __ro_end = .;

  ASSERT(__rw_end <= __ro_start, "the writable part runs into the read-only part")
  ASSERT(!DEFINED(__ro_at) || __ro_end <= 0xf000,
         "the read-only part runs into the interpreter (0xf000)")

  /DISCARD/ : { *(.comment) *(.note.*) }
}
