#include "gicv2.h"

/*
 * QEMU virt (GICv2):
 *   GICD: 0x08000000
 *   GICC: 0x08010000
 *
 * The kernel maps PA 0x0000_0000..0x3FFF_FFFF as Device at:
 *   VA 0xFFFF8000_0000_0000 + PA
 */
#define HH_DEV_BASE      0xFFFF800000000000ULL

#define GICD_BASE_PA     0x08000000ULL
#define GICC_BASE_PA     0x08010000ULL

#define GICD_BASE        (HH_DEV_BASE + GICD_BASE_PA)
#define GICC_BASE        (HH_DEV_BASE + GICC_BASE_PA)

/* Distributor registers */
#define GICD_CTLR        0x000
#define GICD_IGROUPR(n)  (0x080 + 4u*(n))
#define GICD_ISENABLER(n) (0x100 + 4u*(n))
#define GICD_ICENABLER(n) (0x180 + 4u*(n))
#define GICD_IPRIORITYR(n) (0x400 + 4u*(n))
/* Interrupt configuration registers (2 bits / interrupt). */
#define GICD_ICFGR(n)     (0xC00 + 4u*(n))

/* CPU interface registers */
#define GICC_CTLR        0x000
#define GICC_PMR         0x004
#define GICC_BPR         0x008
#define GICC_IAR         0x00C
#define GICC_EOIR        0x010

static inline void mmio_write32(uint64_t base, uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)(base + off) = val;
}

static inline uint32_t mmio_read32(uint64_t base, uint32_t off)
{
    return *(volatile uint32_t *)(base + off);
}

static inline void dsb_sy(void) { __asm__ volatile("dsb sy" ::: "memory"); }
static inline void isb(void)    { __asm__ volatile("isb" ::: "memory"); }

void gicv2_init(void)
{
    /* Disable distributor + CPU interface while configuring. */
    mmio_write32(GICD_BASE, GICD_CTLR, 0);
    mmio_write32(GICC_BASE, GICC_CTLR, 0);
    dsb_sy(); isb();

    /*
     * IMPORTANT (GICv2 w/ Security Extensions):
     * If the CPU is running in Secure state, and an interrupt is configured as
     * Non-secure (IGROUPR bit = 1), then a secure read of ICCIAR can return the
     * special INTID 1022 (0x3FE) instead of acknowledging the interrupt.
     *
     * Your trace shows iar==0x3FE, which matches this exact condition.
     *
     * CapazOS currently runs entirely in one world, so keep interrupts in Group 0
     * (Secure) by default.
     */
    uint32_t ig0 = mmio_read32(GICD_BASE, GICD_IGROUPR(0));
    ig0 &= ~0xFFFF0000u;               /* PPIs 16..31 -> Group 0 */
    mmio_write32(GICD_BASE, GICD_IGROUPR(0), ig0);

    /* Set a permissive priority mask (allow all). */
    mmio_write32(GICC_BASE, GICC_PMR, 0xFFu);
    mmio_write32(GICC_BASE, GICC_BPR, 0u);

    /* Enable the distributor + CPU interface (Group 0). */
    mmio_write32(GICD_BASE, GICD_CTLR, (1u << 0));
    mmio_write32(GICC_BASE, GICC_CTLR, (1u << 0));
    dsb_sy(); isb();
}

static void set_priority(uint32_t irq, uint8_t pri)
{
    uint32_t reg = irq / 4u;
    uint32_t shift = (irq % 4u) * 8u;
    uint32_t off = GICD_IPRIORITYR(reg);
    uint32_t v = mmio_read32(GICD_BASE, off);
    v &= ~(0xFFu << shift);
    v |= ((uint32_t)pri) << shift;
    mmio_write32(GICD_BASE, off, v);
}

void gicv2_config_irq(uint32_t irq, bool edge)
{
    /*
     * GICD_ICFGR: 2 bits per interrupt.
     * 00 = level-sensitive, 10 = edge-triggered.
     */
    uint32_t reg = irq / 16u;
    uint32_t shift = (irq % 16u) * 2u;
    uint32_t off = GICD_ICFGR(reg);
    uint32_t v = mmio_read32(GICD_BASE, off);
    v &= ~(3u << shift);
    v |= ((edge ? 2u : 0u) << shift);
    mmio_write32(GICD_BASE, off, v);
    dsb_sy();
}

void gicv2_enable_irq(uint32_t irq)
{
    /* Set a default priority for now (medium). */
    set_priority(irq, 0x80);

    uint32_t reg = irq / 32u;
    uint32_t bit = irq % 32u;
    mmio_write32(GICD_BASE, GICD_ISENABLER(reg), (1u << bit));
    dsb_sy();
}

void gicv2_disable_irq(uint32_t irq)
{
    uint32_t reg = irq / 32u;
    uint32_t bit = irq % 32u;
    mmio_write32(GICD_BASE, GICD_ICENABLER(reg), (1u << bit));
    dsb_sy();
}

uint32_t gicv2_acknowledge(void)
{
    return mmio_read32(GICC_BASE, GICC_IAR);
}

void gicv2_end_interrupt(uint32_t iar)
{
    mmio_write32(GICC_BASE, GICC_EOIR, iar);
    dsb_sy();
}
