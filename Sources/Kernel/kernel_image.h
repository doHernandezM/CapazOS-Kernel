/*
 * kernel_image.h
 *
 * Minimal, self-describing kernel image header.
 *
 * The boot stage reads this structure from the first bytes of the kernel
 * image (at its physical load address) to determine the kernel's true
 * size and entry offset. This removes hard-coded kernel size/base
 * assumptions from early boot.
 */

#pragma once

#include <stdint.h>

/* "KIMG" in ASCII (little-endian in memory). */
#define KERNEL_IMAGE_MAGIC 0x474D494B
#define KERNEL_IMAGE_VERSION 2u

/* Keep this header small and stable; extend by adding fields at the end. */
typedef struct kernel_image_header {
    uint32_t magic;        /* KERNEL_IMAGE_MAGIC */
    uint32_t version;      /* KERNEL_IMAGE_VERSION */
    uint64_t image_size;   /* Loaded image size in bytes (through .data) */
    uint64_t runtime_size; /* Runtime footprint size in bytes (through .bss, page-aligned) */
    uint64_t entry_offset; /* Offset from image start to entry point */
    uint64_t flags;        /* Reserved for future use */
    uint64_t reserved[3];  /* Pad to 64 bytes */
} kernel_image_header_t;
