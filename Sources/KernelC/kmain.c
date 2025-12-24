//
//  kmain.c
//  OSpost
//

#include "uart_pl011.h"
#include "exceptions.h"
#include "kiface.h"

#if KTEST_ENABLE
#include "kernel_test.h"
#endif

extern void swift_kmain(void);

__attribute__((noreturn))
static void halt_forever(void) {
    uart_puts("\n====================\n");
    uart_puts("=¡FOVEREVER HALTED!=\n");
    uart_puts("====================\n");

    for (;;) {
        __asm__ volatile("wfe");
    }
}

void kmain(void) {
  uart_puts("C Kernel: 0.0.1\n");

  work_request_t boot_req = {
    .intent = INTENT_INTERACTIVE,
    .latency = LATENCY_LOW,
    .throughput = THROUGHPUT_LOW,
    .energy_hint_mw = 0,
  };

  (void)k_submit_work(&boot_req, CAP_INVALID);

#if KTEST_ENABLE
  kernel_test_run();
#endif

  swift_kmain();
  halt_forever();
}
