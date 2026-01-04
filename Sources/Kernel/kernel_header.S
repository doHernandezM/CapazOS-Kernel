/*
 * kernel_header.S
 *
 * Emits the kernel image header at the very start of the kernel binary.
 *
 * We implement this in assembly (not C) because C global initializers
 * cannot portably express link-time address differences as compile-time
 * constants. The assembler and linker, however, can materialize these
 * symbol differences directly.
 *
 * The linker script (kernel.ld) places the .kernel_header output section
 * first in the kernel image. The boot stage reads this header from the
 * kernel's physical load address to discover:
 *   - the kernel image's true byte size
 *   - the entry offset for the freestanding runtime entry (_kcrt0)
 */

    .section .kernel_header, "a", %progbits
    .balign 16
    .global __kernel_image_header
    .type   __kernel_image_header, %object

/*
 * Layout must match kernel_image_header_t in kernel_image.h (v2):
 *   uint32_t magic;
 *   uint32_t version;
 *   uint64_t image_size;
 *   uint64_t runtime_size;
 *   uint64_t entry_offset;
 *   uint64_t flags;
 *   uint64_t reserved[3];
 */
__kernel_image_header:
    /* magic = 'KIMG' (0x474D494B little-endian) */
    .word   0x474D494B
    /* version = 2 */
    .word   2
    /* image_size = __kernel_loaded_end - __kernel_image_start */
    .quad   __kernel_loaded_size
    /* runtime_size = __kernel_runtime_end - __kernel_image_start */
    .quad   __kernel_runtime_size
    /* entry_offset = _kcrt0 - __kernel_image_start */
    .quad   __kernel_entry_offset
    /* flags */
    .quad   0
    /* reserved[3] */
    .quad   0
    .quad   0
    .quad   0

    .size   __kernel_image_header, . - __kernel_image_header
