#pragma once

#include <stdint.h>
#include <stddef.h>

#define KERNEL_SERVICES_ABI_VERSION 1

typedef struct kernel_services_v1 {
  uint32_t abi_version;

  void (*log)(const char *msg);
  void (*panic)(const char *msg);

  void *(*alloc)(size_t size);
  void (*free)(void *ptr);

  uint64_t (*irq_save)(void);
  void (*irq_restore)(uint64_t prev_daif);

  uint64_t (*time_now_ticks)(void);

  void (*yield)(void);
} kernel_services_v1_t;
