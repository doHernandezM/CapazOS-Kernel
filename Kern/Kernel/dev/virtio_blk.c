#include "dev/virtio_blk.h"

#include "kheap.h"
#include "mm/pmm.h"
#include "mm/mem.h"
#include "platform/dtb.h"
#include "serial/uart.h"

#define HH_PHYS_BASE 0xFFFF800000000000ULL
#define VIRTIO_PAGE_SIZE 0x1000ULL
#define CACHE_LINE_SIZE 64ULL

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

#define VIRTIO_MMIO_MAGIC 0x74726976U
#define VIRTIO_VERSION_1 1U
#define VIRTIO_VERSION_2 2U

#define VIRTIO_DEVICE_BLOCK 2U

#define VIRTIO_STATUS_ACKNOWLEDGE 1U
#define VIRTIO_STATUS_DRIVER      2U
#define VIRTIO_STATUS_DRIVER_OK   4U
#define VIRTIO_STATUS_FEATURES_OK 8U
#define VIRTIO_STATUS_FAILED      0x80U

#define VIRTQ_DESC_F_NEXT  1U
#define VIRTQ_DESC_F_WRITE 2U

#define VIRTIO_BLK_T_IN  0U
#define VIRTIO_BLK_T_OUT 1U

#define VIRTQ_SIZE 8U

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
    uint16_t used_event;
} __attribute__((packed));

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[VIRTQ_SIZE];
    uint16_t avail_event;
} __attribute__((packed));

struct virtio_blk_req {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed));

struct virtio_blk_dev {
    uint64_t base;
    volatile uint32_t *mmio;
    uint32_t queue_size;
    struct virtq_desc *desc;
    struct virtq_avail *avail;
    struct virtq_used *used;
    uint16_t last_used;
    struct virtio_blk_req req;
    uint8_t status;
    uint64_t capacity_sectors;
    uint32_t sector_size;
    bool ready;
};

static struct virtio_blk_dev g_blk;

static inline volatile uint32_t *mmio_ptr(uint64_t base)
{
    return (volatile uint32_t *)(HH_PHYS_BASE + base);
}

static inline void mmio_write(volatile uint32_t *base, uint32_t off, uint32_t val)
{
    base[off / 4] = val;
}

static inline uint32_t mmio_read(volatile uint32_t *base, uint32_t off)
{
    return base[off / 4];
}

static inline void mb(void)
{
    __asm__ volatile("dmb sy" ::: "memory");
}

static inline void dcache_clean_invalidate_range(void *addr, size_t len)
{
    uint64_t start = (uint64_t)(uintptr_t)addr;
    uint64_t end = start + (uint64_t)len;
    start &= ~(CACHE_LINE_SIZE - 1ULL);
    for (uint64_t p = start; p < end; p += CACHE_LINE_SIZE) {
        __asm__ volatile("dc civac, %0" :: "r"(p) : "memory");
    }
    __asm__ volatile("dsb sy" ::: "memory");
}

static inline void dcache_invalidate_range(void *addr, size_t len)
{
    uint64_t start = (uint64_t)(uintptr_t)addr;
    uint64_t end = start + (uint64_t)len;
    start &= ~(CACHE_LINE_SIZE - 1ULL);
    for (uint64_t p = start; p < end; p += CACHE_LINE_SIZE) {
        __asm__ volatile("dc ivac, %0" :: "r"(p) : "memory");
    }
    __asm__ volatile("dsb sy" ::: "memory");
}
static bool virtio_blk_probe(uint64_t base)
{
    volatile uint32_t *mmio = mmio_ptr(base);
    uint32_t magic = mmio_read(mmio, VIRTIO_MMIO_MAGIC_VALUE);
    uint32_t version = mmio_read(mmio, VIRTIO_MMIO_VERSION);
    uint32_t devid = mmio_read(mmio, VIRTIO_MMIO_DEVICE_ID);

//    uart_write("virtio: probe base=", sizeof("virtio: probe base=") - 1);
//    uart_puthex64(base);
//    uart_write("\n", 1);
//    uart_write("virtio: magic=", sizeof("virtio: magic=") - 1);
//    uart_puthex64(magic);
//    uart_write(" version=", sizeof(" version=") - 1);
//    uart_putu64_dec((uint64_t)version);
//    uart_write(" dev=", sizeof(" dev=") - 1);
//    uart_putu64_dec((uint64_t)devid);
//    uart_write("\n", 1);

    if (magic != VIRTIO_MMIO_MAGIC) {
        return false;
    }
    if (version != VIRTIO_VERSION_1 && version != VIRTIO_VERSION_2) {
        return false;
    }
    if (devid != VIRTIO_DEVICE_BLOCK) {
        return false;
    }

    mmio_write(mmio, VIRTIO_MMIO_STATUS, 0);
    uint32_t status = 0;
    status |= VIRTIO_STATUS_ACKNOWLEDGE;
    mmio_write(mmio, VIRTIO_MMIO_STATUS, status);
    status |= VIRTIO_STATUS_DRIVER;
    mmio_write(mmio, VIRTIO_MMIO_STATUS, status);

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
        uint64_t desc_pa = 0;
        uint64_t avail_pa = 0;
        uint64_t used_pa = 0;

        desc = (struct virtq_desc *)kheap_alloc_pages(1, &desc_pa);
        avail = (struct virtq_avail *)kheap_alloc_pages(1, &avail_pa);
        used = (struct virtq_used *)kheap_alloc_pages(1, &used_pa);

        if (!desc || !avail || !used) {
            return false;
        }

        memset(desc, 0, VIRTIO_PAGE_SIZE);
        memset(avail, 0, VIRTIO_PAGE_SIZE);
        memset(used, 0, VIRTIO_PAGE_SIZE);

        mmio_write(mmio, VIRTIO_MMIO_QUEUE_DESC_LOW, (uint32_t)(desc_pa & 0xFFFFFFFFULL));
        mmio_write(mmio, VIRTIO_MMIO_QUEUE_DESC_HIGH, (uint32_t)(desc_pa >> 32));
        mmio_write(mmio, VIRTIO_MMIO_QUEUE_AVAIL_LOW, (uint32_t)(avail_pa & 0xFFFFFFFFULL));
        mmio_write(mmio, VIRTIO_MMIO_QUEUE_AVAIL_HIGH, (uint32_t)(avail_pa >> 32));
        mmio_write(mmio, VIRTIO_MMIO_QUEUE_USED_LOW, (uint32_t)(used_pa & 0xFFFFFFFFULL));
        mmio_write(mmio, VIRTIO_MMIO_QUEUE_USED_HIGH, (uint32_t)(used_pa >> 32));

        mmio_write(mmio, VIRTIO_MMIO_QUEUE_READY, 1);
    } else {
        mmio_write(mmio, VIRTIO_MMIO_GUEST_PAGE_SIZE, (uint32_t)VIRTIO_PAGE_SIZE);
        uint64_t queue_pa = 0;
        uint32_t pages = 2;
        uint8_t *queue = (uint8_t *)kheap_alloc_pages(pages, &queue_pa);
        if (!queue) {
            return false;
        }
        memset(queue, 0, (size_t)pages * VIRTIO_PAGE_SIZE);

        uint32_t desc_bytes = qsize * (uint32_t)sizeof(struct virtq_desc);
        uint32_t avail_bytes = (uint32_t)sizeof(struct virtq_avail);
        uint32_t used_off = (desc_bytes + avail_bytes + (VIRTIO_PAGE_SIZE - 1)) & ~(VIRTIO_PAGE_SIZE - 1);

        desc = (struct virtq_desc *)queue;
        avail = (struct virtq_avail *)(queue + desc_bytes);
        used = (struct virtq_used *)(queue + used_off);

        mmio_write(mmio, VIRTIO_MMIO_QUEUE_ALIGN, VIRTIO_PAGE_SIZE);
        mmio_write(mmio, VIRTIO_MMIO_QUEUE_PFN, (uint32_t)(queue_pa >> 12));
    }

    status |= VIRTIO_STATUS_DRIVER_OK;
    mmio_write(mmio, VIRTIO_MMIO_STATUS, status);

    uint32_t cap_lo = mmio_read(mmio, VIRTIO_MMIO_CONFIG + 0);
    uint32_t cap_hi = mmio_read(mmio, VIRTIO_MMIO_CONFIG + 4);
    uint64_t capacity = ((uint64_t)cap_hi << 32) | cap_lo;

    g_blk.base = base;
    g_blk.mmio = mmio;
    g_blk.queue_size = qsize;
    g_blk.desc = desc;
    g_blk.avail = avail;
    g_blk.used = used;
    g_blk.last_used = 0;
    g_blk.capacity_sectors = capacity;
    g_blk.sector_size = 512;
    g_blk.ready = true;
    return true;
}

bool virtio_blk_init(void)
{
    if (g_blk.ready) {
        return true;
    }

    dtb_range_t ranges[64];
    uint32_t count = (uint32_t)(sizeof(ranges) / sizeof(ranges[0]));
    if (!dtb_get_virtio_mmio_ranges(ranges, &count) || count == 0) {
//        uart_write("virtio: no mmio ranges; using fallback\n",
//                   sizeof("virtio: no mmio ranges; using fallback\n") - 1);
        const uint64_t base0 = 0x0A000000ULL;
        const uint64_t step = 0x200ULL;
        const uint32_t max = 32;
        count = 0;
        for (uint32_t i = 0; i < max && i < (uint32_t)(sizeof(ranges) / sizeof(ranges[0])); i++) {
            ranges[i].base = base0 + (step * (uint64_t)i);
            ranges[i].size = 0x200ULL;
            count++;
        }
    }

//    uart_write("virtio: mmio ranges=", sizeof("virtio: mmio ranges=") - 1);
//    uart_putu64_dec((uint64_t)count);
//    uart_write("\n", 1);

    for (uint32_t i = 0; i < count; i++) {
        if (virtio_blk_probe(ranges[i].base)) {
//            uart_write("virtio: blk ready\n", sizeof("virtio: blk ready\n") - 1);
            return true;
        }
    }

//    uart_write("virtio: blk not found\n", sizeof("virtio: blk not found\n") - 1);
    return false;
}

bool virtio_blk_ready(void)
{
    return g_blk.ready;
}

uint32_t virtio_blk_sector_size(void)
{
    return g_blk.sector_size;
}

uint64_t virtio_blk_capacity_sectors(void)
{
    return g_blk.capacity_sectors;
}

bool virtio_blk_read(uint64_t lba, uint32_t count, void *buf, size_t buf_len)
{
    if (!g_blk.ready || !buf || count == 0) {
        return false;
    }

    uint64_t total = (uint64_t)count * (uint64_t)g_blk.sector_size;
    if (buf_len < total) {
        return false;
    }

    struct virtio_blk_req *req = &g_blk.req;
    req->type = VIRTIO_BLK_T_IN;
    req->reserved = 0;
    req->sector = lba;
    g_blk.status = 0xFFu;

    uint64_t req_pa = pmm_virt_to_phys((uint64_t)(uintptr_t)req);
    uint64_t buf_pa = pmm_virt_to_phys((uint64_t)(uintptr_t)buf);
    uint64_t st_pa = pmm_virt_to_phys((uint64_t)(uintptr_t)&g_blk.status);

    g_blk.desc[0].addr = req_pa;
    g_blk.desc[0].len = sizeof(*req);
    g_blk.desc[0].flags = VIRTQ_DESC_F_NEXT;
    g_blk.desc[0].next = 1;

    g_blk.desc[1].addr = buf_pa;
    g_blk.desc[1].len = (uint32_t)total;
    g_blk.desc[1].flags = VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE;
    g_blk.desc[1].next = 2;

    g_blk.desc[2].addr = st_pa;
    g_blk.desc[2].len = 1;
    g_blk.desc[2].flags = VIRTQ_DESC_F_WRITE;
    g_blk.desc[2].next = 0;

    dcache_clean_invalidate_range(&g_blk.req, sizeof(g_blk.req));
    dcache_clean_invalidate_range(buf, (size_t)total);
    dcache_clean_invalidate_range(&g_blk.status, 1);
    dcache_clean_invalidate_range(g_blk.desc, sizeof(struct virtq_desc) * g_blk.queue_size);
    dcache_clean_invalidate_range(g_blk.avail, sizeof(struct virtq_avail));

    uint16_t idx = g_blk.avail->idx;
    g_blk.avail->ring[idx % g_blk.queue_size] = 0;
    mb();
    g_blk.avail->idx = idx + 1;
    mb();
    /* Make sure the device sees the updated avail ring/index. */
    dcache_clean_invalidate_range(g_blk.avail, sizeof(struct virtq_avail));

    mmio_write(g_blk.mmio, VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    uint32_t spin = 0;
    while (g_blk.used->idx == g_blk.last_used) {
        /* Refresh used ring from memory while polling. */
        dcache_invalidate_range(g_blk.used, sizeof(struct virtq_used));
        __asm__ volatile("nop");
        if (++spin > 2000000U) {
            uart_write("virtio: read timeout\n", sizeof("virtio: read timeout\n") - 1);
            uart_write("virtio: status=", sizeof("virtio: status=") - 1);
            uart_putu64_dec((uint64_t)mmio_read(g_blk.mmio, VIRTIO_MMIO_STATUS));
            uart_write(" isr=", sizeof(" isr=") - 1);
            uart_putu64_dec((uint64_t)mmio_read(g_blk.mmio, VIRTIO_MMIO_INTERRUPT_STATUS));
            uart_write(" used=", sizeof(" used=") - 1);
            uart_putu64_dec((uint64_t)g_blk.used->idx);
            uart_write(" last=", sizeof(" last=") - 1);
            uart_putu64_dec((uint64_t)g_blk.last_used);
            uart_write("\n", 1);
            return false;
        }
    }
    mb();
    g_blk.last_used = g_blk.used->idx;

    dcache_invalidate_range(g_blk.used, sizeof(struct virtq_used));
    dcache_invalidate_range(&g_blk.status, 1);
    dcache_invalidate_range(buf, (size_t)total);

    if (g_blk.status != 0) {
        return false;
    }

    return true;
}

bool virtio_blk_write(uint64_t lba, uint32_t count, const void *buf, size_t buf_len)
{
    if (!g_blk.ready || !buf || count == 0) {
        return false;
    }

    uint64_t total = (uint64_t)count * (uint64_t)g_blk.sector_size;
    if (buf_len < total) {
        return false;
    }

    struct virtio_blk_req *req = &g_blk.req;
    req->type = VIRTIO_BLK_T_OUT;
    req->reserved = 0;
    req->sector = lba;
    g_blk.status = 0xFFu;

    uint64_t req_pa = pmm_virt_to_phys((uint64_t)(uintptr_t)req);
    uint64_t buf_pa = pmm_virt_to_phys((uint64_t)(uintptr_t)buf);
    uint64_t st_pa = pmm_virt_to_phys((uint64_t)(uintptr_t)&g_blk.status);

    g_blk.desc[0].addr = req_pa;
    g_blk.desc[0].len = sizeof(*req);
    g_blk.desc[0].flags = VIRTQ_DESC_F_NEXT;
    g_blk.desc[0].next = 1;

    g_blk.desc[1].addr = buf_pa;
    g_blk.desc[1].len = (uint32_t)total;
    g_blk.desc[1].flags = VIRTQ_DESC_F_NEXT;
    g_blk.desc[1].next = 2;

    g_blk.desc[2].addr = st_pa;
    g_blk.desc[2].len = 1;
    g_blk.desc[2].flags = VIRTQ_DESC_F_WRITE;
    g_blk.desc[2].next = 0;

    dcache_clean_invalidate_range(&g_blk.req, sizeof(g_blk.req));
    dcache_clean_invalidate_range((void *)buf, (size_t)total);
    dcache_clean_invalidate_range(&g_blk.status, 1);
    dcache_clean_invalidate_range(g_blk.desc, sizeof(struct virtq_desc) * g_blk.queue_size);
    dcache_clean_invalidate_range(g_blk.avail, sizeof(struct virtq_avail));

    uint16_t idx = g_blk.avail->idx;
    g_blk.avail->ring[idx % g_blk.queue_size] = 0;
    mb();
    g_blk.avail->idx = idx + 1;
    mb();
    dcache_clean_invalidate_range(g_blk.avail, sizeof(struct virtq_avail));

    mmio_write(g_blk.mmio, VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    uint32_t spin = 0;
    while (g_blk.used->idx == g_blk.last_used) {
        dcache_invalidate_range(g_blk.used, sizeof(struct virtq_used));
        __asm__ volatile("nop");
        if (++spin > 2000000U) {
            uart_write("virtio: write timeout\n", sizeof("virtio: write timeout\n") - 1);
            return false;
        }
    }
    mb();
    g_blk.last_used = g_blk.used->idx;

    dcache_invalidate_range(g_blk.used, sizeof(struct virtq_used));
    dcache_invalidate_range(&g_blk.status, 1);

    if (g_blk.status != 0) {
        return false;
    }

    return true;
}
