#ifndef CAPAZ_CONFIG_H
#define CAPAZ_CONFIG_H

/*
 * Kernel build-time configuration knobs.
 *
 * These are intentionally plain C preprocessor defines so they can be
 * overridden from the build script (e.g. -DCONFIG_TICKLESS=1).
 */

/* Default periodic tick rate (Hz) when tickless mode is disabled. */
#ifndef CONFIG_TICK_HZ
#define CONFIG_TICK_HZ 100
#endif

/*
 * When enabled, the kernel will not start a periodic timer tick by default.
 * One-shot timer events are expected to be armed explicitly based on the next
 * deadline.
 */
#ifndef CONFIG_TICKLESS
#define CONFIG_TICKLESS 0
#endif

#if (CONFIG_TICK_HZ <= 0)
#error "CONFIG_TICK_HZ must be > 0"
#endif

#endif /* CAPAZ_CONFIG_H */
