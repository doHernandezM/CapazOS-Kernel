#include "dtb.h"

#include <stddef.h>

#include "uart_pl011.h"

/*
 * Minimal Flattened Device Tree (FDT/DTB) parser sufficient for QEMU 'virt':
 *  - validate header
 *  - parse memreserve map
 *  - parse /memory reg
 *  - find first node with compatible containing "arm,pl011" and read reg[0].addr
 *
 * This is intentionally tiny and allocation-free for early boot.
 */

/* FDT tokens (big-endian u32 in the structure block). */
enum {
    FDT_BEGIN_NODE = 0x1,
    FDT_END_NODE   = 0x2,
    FDT_PROP       = 0x3,
    FDT_NOP        = 0x4,
    FDT_END        = 0x9,
};

#define FDT_MAGIC 0xD00DFEEDu

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
} fdt_header_t;

static const uint8_t *g_fdt;
static uint32_t g_fdt_totalsize;
static const uint8_t *g_struct;
static const uint8_t *g_strings;
static const uint8_t *g_rsvmap;

static inline uint32_t be32(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

static inline uint64_t be64(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return ((uint64_t)be32(b) << 32) | (uint64_t)be32(b + 4);
}

static inline const char *str_at(uint32_t off) {
    return (const char *)(g_strings + off);
}

static inline const uint8_t *align4(const uint8_t *p) {
    uintptr_t x = (uintptr_t)p;
    return (const uint8_t *)((x + 3u) & ~3u);
}

static void print_hex64(const char *label, uint64_t v) {
    uart_puts(label);
    uart_puthex64(v);
    uart_putnl();
}

bool dtb_init(const void *fdt, uint64_t fdt_size) {
    if (!fdt) return false;

    const fdt_header_t *h = (const fdt_header_t *)fdt;
    uint32_t magic = be32(&h->magic);
    if (magic != FDT_MAGIC) return false;

    uint32_t totalsize = be32(&h->totalsize);
    if (totalsize == 0) return false;

    /* If caller provided a size, require totalsize <= provided. */
    if (fdt_size != 0 && totalsize > (uint32_t)fdt_size) return false;

    g_fdt = (const uint8_t *)fdt;
    g_fdt_totalsize = totalsize;

    uint32_t off_struct  = be32(&h->off_dt_struct);
    uint32_t off_strings = be32(&h->off_dt_strings);
    uint32_t off_rsvmap  = be32(&h->off_mem_rsvmap);

    if (off_struct >= totalsize || off_strings >= totalsize || off_rsvmap >= totalsize) return false;

    g_struct  = g_fdt + off_struct;
    g_strings = g_fdt + off_strings;
    g_rsvmap  = g_fdt + off_rsvmap;
    return true;
}

/* Parse a "reg" blob as (addr,size) using given cell counts (1 or 2). */
static bool parse_reg_first(const uint8_t *data, uint32_t len,
                            uint32_t addr_cells, uint32_t size_cells,
                            uint64_t *out_addr, uint64_t *out_size)
{
    if (!data || len < 8) return false;
    if (addr_cells == 0 || size_cells == 0) return false;
    if (addr_cells > 2 || size_cells > 2) return false;

    uint32_t need = 4u * (addr_cells + size_cells);
    if (len < need) return false;

    uint64_t addr = 0;
    uint64_t size = 0;

    const uint8_t *p = data;
    for (uint32_t i = 0; i < addr_cells; i++) {
        addr = (addr << 32) | (uint64_t)be32(p);
        p += 4;
    }
    for (uint32_t i = 0; i < size_cells; i++) {
        size = (size << 32) | (uint64_t)be32(p);
        p += 4;
    }

    if (out_addr) *out_addr = addr;
    if (out_size) *out_size = size;
    return true;
}

typedef struct {
    /* inherited from parent for reg parsing in this node */
    uint32_t parent_addr_cells;
    uint32_t parent_size_cells;

    /* for children */
    uint32_t addr_cells;
    uint32_t size_cells;

    bool is_memory;
    bool is_uart_candidate;
} node_ctx_t;

#define MAX_DEPTH 32

static bool walk_struct_find(bool want_mem, bool want_uart,
                             uint64_t *mem_base, uint64_t *mem_size,
                             uint64_t *uart_phys)
{
    if (!g_struct || !g_strings) return false;

    node_ctx_t stack[MAX_DEPTH];
    int depth = -1;

    /* Root defaults in AArch64 DTBs are typically 2/2. */
    uint32_t cur_addr_cells = 2;
    uint32_t cur_size_cells = 2;

    const uint8_t *p = g_struct;

    bool found_mem = false;
    bool found_uart = false;

    for (;;) {
        uint32_t token = be32(p);
        p += 4;

        if (token == FDT_BEGIN_NODE) {
            const char *name = (const char *)p;
            size_t nlen = 0;
            while (p[nlen] != '\0') nlen++;
            p += nlen + 1;
            p = align4(p);

            if (depth + 1 >= MAX_DEPTH) return false;
            depth++;

            node_ctx_t ctx = {0};
            ctx.parent_addr_cells = (depth == 0) ? cur_addr_cells : stack[depth - 1].addr_cells;
            ctx.parent_size_cells = (depth == 0) ? cur_size_cells : stack[depth - 1].size_cells;
            ctx.addr_cells = ctx.parent_addr_cells;
            ctx.size_cells = ctx.parent_size_cells;

            /* Identify nodes by name prefix. */
            ctx.is_memory = (name[0] == 'm' && name[1] == 'e' && name[2] == 'm' &&
                             name[3] == 'o' && name[4] == 'r' && name[5] == 'y');
            ctx.is_uart_candidate = false;

            stack[depth] = ctx;
            continue;
        }

        if (token == FDT_END_NODE) {
            if (depth < 0) return false;
            depth--;
            continue;
        }

        if (token == FDT_NOP) {
            continue;
        }

        if (token == FDT_END) {
            break;
        }

        if (token != FDT_PROP) {
            return false;
        }

        uint32_t len = be32(p); p += 4;
        uint32_t nameoff = be32(p); p += 4;
        const uint8_t *data = p;
        p += len;
        p = align4(p);

        if (depth < 0) continue;
        node_ctx_t *ctx = &stack[depth];
        const char *pname = str_at(nameoff);

        /* Handle cell sizes. */
        if (pname[0] == '#' && pname[1] == 'a') { /* "#address-cells" */
            if (len == 4) {
                ctx->addr_cells = be32(data);
            }
            continue;
        }
        if (pname[0] == '#' && pname[1] == 's') { /* "#size-cells" */
            if (len == 4) {
                ctx->size_cells = be32(data);
            }
            continue;
        }

        if (want_uart && pname[0] == 'c' && pname[1] == 'o') { /* "compatible" */
            /* compatible is a sequence of NUL-terminated strings */
            const uint8_t *q = data;
            const uint8_t *end = data + len;
            while (q < end && *q) {
                const char *s = (const char *)q;
                /* look for "arm,pl011" */
                const char *needle = "arm,pl011";
                const char *t = s;
                const char *u = needle;
                while (*t && *u && *t == *u) { t++; u++; }
                if (*u == '\0') {
                    ctx->is_uart_candidate = true;
                    break;
                }
                /* advance to next string */
                while (q < end && *q) q++;
                while (q < end && *q == '\0') q++;
            }
            continue;
        }

        if ((want_mem || want_uart) && pname[0] == 'r' && pname[1] == 'e' && pname[2] == 'g') {
            uint64_t addr = 0, size = 0;
            if (parse_reg_first(data, len, ctx->parent_addr_cells, ctx->parent_size_cells, &addr, &size)) {
                if (want_mem && ctx->is_memory && !found_mem) {
                    found_mem = true;
                    if (mem_base) *mem_base = addr;
                    if (mem_size) *mem_size = size;
                }
                if (want_uart && ctx->is_uart_candidate && !found_uart) {
                    found_uart = true;
                    if (uart_phys) *uart_phys = addr;
                }
            }
            continue;
        }
    }

    return (want_mem ? found_mem : true) && (want_uart ? found_uart : true);
}

bool dtb_first_memory_range(uint64_t *out_base, uint64_t *out_size) {
    uint64_t base = 0, size = 0;
    bool ok = walk_struct_find(true, false, &base, &size, NULL);
    if (!ok) return false;
    if (out_base) *out_base = base;
    if (out_size) *out_size = size;
    return true;
}

bool dtb_find_pl011_uart(uint64_t *out_phys) {
    uint64_t uart = 0;
    bool ok = walk_struct_find(false, true, NULL, NULL, &uart);
    if (!ok) return false;
    if (out_phys) *out_phys = uart;
    return true;
}

static void dump_rsvmap(void) {
    uart_puts("DTB: memreserve map\n");
    if (!g_rsvmap) {
        uart_puts("  (none)\n");
        return;
    }

    const uint8_t *p = g_rsvmap;
    for (int i = 0; i < 64; i++) { /* hard cap */
        uint64_t addr = be64(p); p += 8;
        uint64_t size = be64(p); p += 8;
        if (addr == 0 && size == 0) break;

        uart_puts("  addr="); uart_puthex64(addr);
        uart_puts(" size="); uart_puthex64(size);
        uart_putnl();
    }
}


static void dump_reserved_memory_node(void) {
    uart_puts("DTB: /reserved-memory\n");

    if (!g_struct || !g_strings) {
        uart_puts("  (unavailable)\n");
        return;
    }

    node_ctx_t stack[MAX_DEPTH];
    int depth = -1;

    /* Root defaults. */
    uint32_t cur_addr_cells = 2;
    uint32_t cur_size_cells = 2;

    const uint8_t *p = g_struct;

    bool in_reserved = false;
    bool seen_reserved = false;
    int reserved_depth = -1;

    for (;;) {
        uint32_t token = be32(p);
        p += 4;

        if (token == FDT_BEGIN_NODE) {
            const char *name = (const char *)p;
            size_t nlen = 0;
            while (p[nlen] != '\0') nlen++;
            p += nlen + 1;
            p = align4(p);

            if (depth + 1 >= MAX_DEPTH) return;
            depth++;

            node_ctx_t ctx = {0};
            ctx.parent_addr_cells = (depth == 0) ? cur_addr_cells : stack[depth - 1].addr_cells;
            ctx.parent_size_cells = (depth == 0) ? cur_size_cells : stack[depth - 1].size_cells;
            ctx.addr_cells = ctx.parent_addr_cells;
            ctx.size_cells = ctx.parent_size_cells;
            ctx.is_memory = false;
            ctx.is_uart_candidate = false;

            /* Track entry into /reserved-memory subtree. */
            if (reserved_depth < 0) {
                const char *needle = "reserved-memory";
                const char *t = name;
                const char *u = needle;
                while (*t && *u && *t == *u) { t++; u++; }
                if (*u == '\0') {
                    reserved_depth = depth;
                    in_reserved = true;
                    seen_reserved = true;
                }
            }

            stack[depth] = ctx;
            continue;
        }

        if (token == FDT_END_NODE) {
            if (depth < 0) return;
            if (depth == reserved_depth) {
                reserved_depth = -1;
                in_reserved = false;
            }
            depth--;
            continue;
        }

        if (token == FDT_NOP) continue;
        if (token == FDT_END) break;

        if (token != FDT_PROP) return;

        uint32_t len = be32(p); p += 4;
        uint32_t nameoff = be32(p); p += 4;
        const uint8_t *data = p;
        p += len;
        p = align4(p);

        if (!in_reserved || depth <= reserved_depth) continue; /* only children of reserved-memory */
        if (depth < 0) continue;

        node_ctx_t *ctx = &stack[depth];
        const char *pname = str_at(nameoff);

        if (pname[0] == '#' && pname[1] == 'a') {
            if (len == 4) ctx->addr_cells = be32(data);
            continue;
        }
        if (pname[0] == '#' && pname[1] == 's') {
            if (len == 4) ctx->size_cells = be32(data);
            continue;
        }

        if (pname[0] == 'r' && pname[1] == 'e' && pname[2] == 'g') {
            uint64_t addr = 0, size = 0;
            if (parse_reg_first(data, len, ctx->parent_addr_cells, ctx->parent_size_cells, &addr, &size)) {
                uart_puts("  addr="); uart_puthex64(addr);
                uart_puts(" size="); uart_puthex64(size);
                uart_putnl();
            }
        }
    }

    if (!seen_reserved) {
        uart_puts("  (not present)\n");
    }
}

void dtb_dump_summary(void) {
    uart_puts("\nDTB: summary\n");
    print_hex64("DTB: va=", (uintptr_t)g_fdt);
    print_hex64("DTB: totalsize=", g_fdt_totalsize);

    uint64_t mem_base = 0, mem_size = 0;
    if (dtb_first_memory_range(&mem_base, &mem_size)) {
        uart_puts("DTB: memory[0] base="); uart_puthex64(mem_base);
        uart_puts(" size="); uart_puthex64(mem_size);
        uart_putnl();
    } else {
        uart_puts("DTB: memory node not found\n");
    }

    dump_rsvmap();
    dump_reserved_memory_node();

    uint64_t uart_phys = 0;
    if (dtb_find_pl011_uart(&uart_phys)) {
        uart_puts("DTB: pl011 uart phys="); uart_puthex64(uart_phys); uart_putnl();
    } else {
        uart_puts("DTB: pl011 uart not found\n");
    }
}
