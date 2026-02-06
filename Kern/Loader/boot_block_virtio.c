#include "boot_hal.h"

#include <stdint.h>
#include <stdbool.h>

#define VIRTIO_MMIO_MAGIC_VALUE      0x000
#define VIRTIO_MMIO_VERSION          0x004
#define VIRTIO_MMIO_DEVICE_ID        0x008
#define VIRTIO_MMIO_VENDOR_ID        0x00C
#define VIRTIO_MMIO_DEVICE_FEATURES  0x010
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014
#define VIRTIO_MMIO_DRIVER_FEATURES  0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_MMIO_GUEST_PAGE_SIZE  0x028
#define VIRTIO_MMIO_QUEUE_SEL        0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX    0x034
#define VIRTIO_MMIO_QUEUE_NUM        0x038
#define VIRTIO_MMIO_QUEUE_ALIGN      0x03C
#define VIRTIO_MMIO_QUEUE_PFN        0x040
#define VIRTIO_MMIO_QUEUE_READY      0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY     0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060
#define VIRTIO_MMIO_INTERRUPT_ACK    0x064
#define VIRTIO_MMIO_STATUS           0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW   0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH  0x084
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW  0x090
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH 0x094
#define VIRTIO_MMIO_QUEUE_USED_LOW   0x0A0
#define VIRTIO_MMIO_QUEUE_USED_HIGH  0x0A4
#define VIRTIO_MMIO_CONFIG           0x100

#define VIRTIO_MAGIC 0x74726976U
#define VIRTIO_VERSION_1 1
#define VIRTIO_VERSION_2 2

#define VIRTIO_DEVICE_BLOCK 2
#define VIRTIO_VENDOR_QEMU 0x554D4551U

#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER      2
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_DRIVER_OK   4
#define VIRTIO_STATUS_FAILED      128

#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2

#define VIRTQ_SIZE 8

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTQ_SIZE];
    uint16_t unused;
} __attribute__((packed));

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[VIRTQ_SIZE];
} __attribute__((packed));

struct virtio_blk_req {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed));

struct virtio_blk_dev {
    volatile uint32_t *mmio;
    uint32_t version;
    uint32_t queue_size;
    struct virtq_desc *desc;
    struct virtq_avail *avail;
    struct virtq_used *used;
    uint16_t last_used;
    uint64_t capacity_sectors;
    bool ready;
} g_blk;

static struct virtq_desc g_desc[VIRTQ_SIZE] __attribute__((aligned(16)));
static struct virtq_avail g_avail __attribute__((aligned(16)));
static struct virtq_used g_used __attribute__((aligned(16)));
static uint8_t g_legacy_queue[4096 * 2] __attribute__((aligned(4096)));

static struct virtio_blk_req g_req;
static uint8_t g_status;

static inline void mmio_write(volatile uint32_t *base, uint32_t off, uint32_t v) {
    base[off / 4] = v;
}

static inline uint32_t mmio_read(volatile uint32_t *base, uint32_t off) {
    return base[off / 4];
}

static inline void memory_barrier(void) {
    __asm__ volatile("dsb ishst" ::: "memory");
}

static bool virtio_blk_init_at(uint64_t base) {
    volatile uint32_t *mmio = (volatile uint32_t *)(uintptr_t)base;
    if (mmio_read(mmio, VIRTIO_MMIO_MAGIC_VALUE) != VIRTIO_MAGIC) {
        return false;
    }
    uint32_t version = mmio_read(mmio, VIRTIO_MMIO_VERSION);
    if (version != VIRTIO_VERSION_1 && version != VIRTIO_VERSION_2) {
        return false;
    }
    if (mmio_read(mmio, VIRTIO_MMIO_DEVICE_ID) != VIRTIO_DEVICE_BLOCK) {
        return false;
    }

    /* Reset */
    mmio_write(mmio, VIRTIO_MMIO_STATUS, 0);

    uint32_t status = 0;
    status |= VIRTIO_STATUS_ACKNOWLEDGE;
    status |= VIRTIO_STATUS_DRIVER;
    mmio_write(mmio, VIRTIO_MMIO_STATUS, status);

    /* Feature negotiation: accept none. */
    if (version == VIRTIO_VERSION_2) {
        mmio_write(mmio, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
        (void)mmio_read(mmio, VIRTIO_MMIO_DEVICE_FEATURES);
        mmio_write(mmio, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 1);
        (void)mmio_read(mmio, VIRTIO_MMIO_DEVICE_FEATURES);
        mmio_write(mmio, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
        mmio_write(mmio, VIRTIO_MMIO_DRIVER_FEATURES, 0);
        mmio_write(mmio, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 1);
        mmio_write(mmio, VIRTIO_MMIO_DRIVER_FEATURES, 0);
    } else {
        (void)mmio_read(mmio, VIRTIO_MMIO_DEVICE_FEATURES);
        mmio_write(mmio, VIRTIO_MMIO_DRIVER_FEATURES, 0);
    }

    status |= VIRTIO_STATUS_FEATURES_OK;
    mmio_write(mmio, VIRTIO_MMIO_STATUS, status);
    if (!(mmio_read(mmio, VIRTIO_MMIO_STATUS) & VIRTIO_STATUS_FEATURES_OK)) {
        mmio_write(mmio, VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_FAILED);
        return false;
    }

    mmio_write(mmio, VIRTIO_MMIO_QUEUE_SEL, 0);
    uint32_t qmax = mmio_read(mmio, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (qmax == 0) {
        return false;
    }
    uint32_t qsize = (qmax < VIRTQ_SIZE) ? qmax : VIRTQ_SIZE;
    mmio_write(mmio, VIRTIO_MMIO_QUEUE_NUM, qsize);

    struct virtq_desc *desc = NULL;
    struct virtq_avail *avail = NULL;
    struct virtq_used *used = NULL;
    if (version == VIRTIO_VERSION_2) {
        uint64_t desc_pa = (uint64_t)(uintptr_t)g_desc;
        uint64_t avail_pa = (uint64_t)(uintptr_t)&g_avail;
        uint64_t used_pa = (uint64_t)(uintptr_t)&g_used;

        desc = g_desc;
        avail = &g_avail;
        used = &g_used;
        for (uint32_t i = 0; i < VIRTQ_SIZE; i++) {
            desc[i].addr = 0;
            desc[i].len = 0;
            desc[i].flags = 0;
            desc[i].next = 0;
        }
        avail->flags = 0;
        avail->idx = 0;
        used->flags = 0;
        used->idx = 0;

        mmio_write(mmio, VIRTIO_MMIO_QUEUE_DESC_LOW, (uint32_t)(desc_pa & 0xFFFFFFFFULL));
        mmio_write(mmio, VIRTIO_MMIO_QUEUE_DESC_HIGH, (uint32_t)(desc_pa >> 32));
        mmio_write(mmio, VIRTIO_MMIO_QUEUE_AVAIL_LOW, (uint32_t)(avail_pa & 0xFFFFFFFFULL));
        mmio_write(mmio, VIRTIO_MMIO_QUEUE_AVAIL_HIGH, (uint32_t)(avail_pa >> 32));
        mmio_write(mmio, VIRTIO_MMIO_QUEUE_USED_LOW, (uint32_t)(used_pa & 0xFFFFFFFFULL));
        mmio_write(mmio, VIRTIO_MMIO_QUEUE_USED_HIGH, (uint32_t)(used_pa >> 32));
        mmio_write(mmio, VIRTIO_MMIO_QUEUE_READY, 1);
    } else {
        for (uint32_t i = 0; i < sizeof(g_legacy_queue); i++) {
            g_legacy_queue[i] = 0;
        }
        uint32_t desc_bytes = qsize * (uint32_t)sizeof(struct virtq_desc);
        uint32_t avail_bytes = (uint32_t)sizeof(struct virtq_avail);
        uint32_t used_off = (desc_bytes + avail_bytes + 4095u) & ~4095u;
        desc = (struct virtq_desc *)g_legacy_queue;
        avail = (struct virtq_avail *)(g_legacy_queue + desc_bytes);
        used = (struct virtq_used *)(g_legacy_queue + used_off);
        mmio_write(mmio, VIRTIO_MMIO_GUEST_PAGE_SIZE, 4096u);
        mmio_write(mmio, VIRTIO_MMIO_QUEUE_ALIGN, 4096u);
        mmio_write(mmio, VIRTIO_MMIO_QUEUE_PFN, (uint32_t)(((uint64_t)(uintptr_t)g_legacy_queue) >> 12));
    }

    status |= VIRTIO_STATUS_DRIVER_OK;
    mmio_write(mmio, VIRTIO_MMIO_STATUS, status);

    uint32_t cap_lo = mmio_read(mmio, VIRTIO_MMIO_CONFIG + 0);
    uint32_t cap_hi = mmio_read(mmio, VIRTIO_MMIO_CONFIG + 4);
    uint64_t capacity = ((uint64_t)cap_hi << 32) | cap_lo;

    g_blk.mmio = mmio;
    g_blk.version = version;
    g_blk.queue_size = qsize;
    g_blk.desc = desc;
    g_blk.avail = avail;
    g_blk.used = used;
    g_blk.last_used = 0;
    g_blk.capacity_sectors = capacity;
    g_blk.ready = true;
    return true;
}

static bool virtio_blk_init(void) {
    if (g_blk.ready) {
        return true;
    }
    const uint64_t base0 = 0x0A000000ULL;
    const uint64_t step = 0x200ULL;
    for (uint32_t i = 0; i < 32; i++) {
        if (virtio_blk_init_at(base0 + (step * (uint64_t)i))) {
            return true;
        }
    }
    return false;
}

static bool virtio_blk_read(uint64_t lba, uint32_t count, void *buf) {
    if (!virtio_blk_init()) {
        return false;
    }
    if (!buf || count == 0) {
        return false;
    }

    g_req.type = 0; /* VIRTIO_BLK_T_IN */
    g_req.reserved = 0;
    g_req.sector = lba;
    g_status = 0xFF;

    g_blk.desc[0].addr = (uint64_t)(uintptr_t)&g_req;
    g_blk.desc[0].len = sizeof(g_req);
    g_blk.desc[0].flags = VIRTQ_DESC_F_NEXT;
    g_blk.desc[0].next = 1;

    g_blk.desc[1].addr = (uint64_t)(uintptr_t)buf;
    g_blk.desc[1].len = count * 512u;
    g_blk.desc[1].flags = VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE;
    g_blk.desc[1].next = 2;

    g_blk.desc[2].addr = (uint64_t)(uintptr_t)&g_status;
    g_blk.desc[2].len = 1;
    g_blk.desc[2].flags = VIRTQ_DESC_F_WRITE;
    g_blk.desc[2].next = 0;

    uint16_t idx = g_blk.avail->idx;
    g_blk.avail->ring[idx % g_blk.queue_size] = 0;
    g_blk.avail->idx = idx + 1;
    memory_barrier();

    mmio_write(g_blk.mmio, VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    while (g_blk.used->idx == g_blk.last_used) {
        __asm__ volatile("nop");
    }
    g_blk.last_used = g_blk.used->idx;

    if (g_status != 0) {
        return false;
    }
    return true;
}

bool boot_block_read(uint64_t lba, uint32_t count, void *buf)
{
    return virtio_blk_read(lba, count, buf);
}
