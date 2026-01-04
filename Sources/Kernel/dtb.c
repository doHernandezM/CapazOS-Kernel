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

/* Parsed results (bounded; no allocator yet). */
#define DTB_MAX_MEMORY_RANGES   16
#define DTB_MAX_RESERVED_RANGES 64

static dtb_range_t g_mem_ranges[DTB_MAX_MEMORY_RANGES];
static uint32_t    g_mem_count;

static dtb_range_t g_rsv_ranges[DTB_MAX_RESERVED_RANGES];
static uint32_t    g_rsv_count;

static bool g_parsed;

static void ensure_parsed(void);

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

/* -------------------------------------------------------------------------- */
/* Minimal string helpers (no libc). */

static bool streq(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (*a != *b) return false;
        a++; b++;
    }
    return (*a == '\0' && *b == '\0');
}

static void strlcpy0(char *dst, uint32_t cap, const char *src) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    uint32_t i = 0;
    while (i + 1 < cap && src[i] != '\0') { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void strip_after_colon(char *s) {
    if (!s) return;
    for (uint32_t i = 0; s[i] != '\0'; i++) {
        if (s[i] == ':') { s[i] = '\0'; return; }
    }
}

static bool comp_eq(const char *node_name, const char *comp, uint32_t comp_len) {
    if (!node_name || !comp) return false;
    for (uint32_t i = 0; i < comp_len; i++) {
        if (node_name[i] != comp[i]) return false;
    }
    return node_name[comp_len] == '\0';
}

static uint32_t split_path(const char *path, const char **comps, uint32_t max_comps) {
    if (!path || !comps || max_comps == 0) return 0;
    uint32_t n = 0;
    const char *p = path;
    while (*p == '/') p++;
    while (*p != '\0' && n < max_comps) {
        comps[n++] = p;
        while (*p != '\0' && *p != '/') p++;
        while (*p == '/') p++;
    }
    return n;
}

static void print_hex64(const char *label, uint64_t v) {
    uart_puts(label);
    uart_puthex64(v);
    uart_putnl();
}

/* -------------------------------------------------------------------------- */
/* /chosen stdout-path -> /aliases resolution -> node path -> reg decode. */

static bool dtb_read_chosen_stdout(char *out, uint32_t cap, char *out_key, uint32_t key_cap)
{
    if (!g_struct || !g_strings || !out || cap == 0) return false;
    out[0] = '\0';
    if (out_key && key_cap) out_key[0] = '\0';

    const uint8_t *p = g_struct;
    int depth = -1;
    bool in_chosen = false;
    int chosen_depth = -1;

    for (;;) {
        uint32_t token = be32(p); p += 4;
        if (token == FDT_BEGIN_NODE) {
            const char *name = (const char *)p;
            size_t nlen = 0; while (p[nlen] != '\0') nlen++;
            p += nlen + 1; p = align4(p);
            depth++;

            if (!in_chosen) {
                if (name[0] == 'c' && name[1] == 'h' && name[2] == 'o' && name[3] == 's' &&
                    name[4] == 'e' && name[5] == 'n' && name[6] == '\0')
                {
                    in_chosen = true;
                    chosen_depth = depth;
                }
            }
            continue;
        }

        if (token == FDT_END_NODE) {
            if (depth == chosen_depth) { in_chosen = false; chosen_depth = -1; }
            depth--;
            continue;
        }

        if (token == FDT_NOP) continue;
        if (token == FDT_END) break;
        if (token != FDT_PROP) return false;

        uint32_t len = be32(p); p += 4;
        uint32_t nameoff = be32(p); p += 4;
        const uint8_t *data = p;
        p += len; p = align4(p);

        if (!in_chosen || depth != chosen_depth) continue;

        const char *pname = str_at(nameoff);
        if (streq(pname, "stdout-path") || streq(pname, "linux,stdout-path")) {
            if (len == 0) continue;
            /* Property is a NUL-terminated string. */
            strlcpy0(out, cap, (const char *)data);
            strip_after_colon(out);
            if (out_key && key_cap) strlcpy0(out_key, key_cap, pname);
            return true;
        }
    }

    return false;
}

static bool dtb_resolve_alias(const char *alias, char *out_path, uint32_t cap)
{
    if (!g_struct || !g_strings || !alias || !out_path || cap == 0) return false;
    out_path[0] = '\0';

    const uint8_t *p = g_struct;
    int depth = -1;
    bool in_aliases = false;
    int aliases_depth = -1;

    for (;;) {
        uint32_t token = be32(p); p += 4;
        if (token == FDT_BEGIN_NODE) {
            const char *name = (const char *)p;
            size_t nlen = 0; while (p[nlen] != '\0') nlen++;
            p += nlen + 1; p = align4(p);
            depth++;

            if (!in_aliases) {
                if (name[0] == 'a' && name[1] == 'l' && name[2] == 'i' && name[3] == 'a' &&
                    name[4] == 's' && name[5] == 'e' && name[6] == 's' && name[7] == '\0')
                {
                    in_aliases = true;
                    aliases_depth = depth;
                }
            }
            continue;
        }

        if (token == FDT_END_NODE) {
            if (depth == aliases_depth) { in_aliases = false; aliases_depth = -1; }
            depth--;
            continue;
        }

        if (token == FDT_NOP) continue;
        if (token == FDT_END) break;
        if (token != FDT_PROP) return false;

        uint32_t len = be32(p); p += 4;
        uint32_t nameoff = be32(p); p += 4;
        const uint8_t *data = p;
        p += len; p = align4(p);

        if (!in_aliases || depth != aliases_depth) continue;

        const char *pname = str_at(nameoff);
        if (streq(pname, alias)) {
            strlcpy0(out_path, cap, (const char *)data);
            return out_path[0] == '/';
        }
    }

    return false;
}


#ifndef DTB_NODE_CTX_T
#define DTB_NODE_CTX_T
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
#endif

#ifndef MAX_DEPTH
#define MAX_DEPTH 32
#endif

/* Find a node by absolute path and decode reg[0].addr into *out_addr. */
static bool dtb_find_node_reg_addr_by_path(const char *path, uint64_t *out_addr)
{
    if (!g_struct || !g_strings || !path || !out_addr) return false;
    if (path[0] != '/') return false;

    const char *comps[32];
    uint32_t comp_count = split_path(path, comps, 32);
    if (comp_count == 0) return false;

    node_ctx_t stack[MAX_DEPTH];
    const char *names[MAX_DEPTH];
    int depth = -1;

    uint32_t cur_addr_cells = 2;
    uint32_t cur_size_cells = 2;

    int target_depth = -1;
    const uint8_t *p = g_struct;

    for (;;) {
        uint32_t token = be32(p); p += 4;

        if (token == FDT_BEGIN_NODE) {
            const char *name = (const char *)p;
            size_t nlen = 0; while (p[nlen] != '\0') nlen++;
            p += nlen + 1; p = align4(p);

            if (depth + 1 >= MAX_DEPTH) return false;
            depth++;

            names[depth] = name;

            node_ctx_t ctx = {0};
            ctx.parent_addr_cells = (depth == 0) ? cur_addr_cells : stack[depth - 1].addr_cells;
            ctx.parent_size_cells = (depth == 0) ? cur_size_cells : stack[depth - 1].size_cells;
            ctx.addr_cells = ctx.parent_addr_cells;
            ctx.size_cells = ctx.parent_size_cells;
            ctx.is_memory = false;
            ctx.is_uart_candidate = false;
            stack[depth] = ctx;

            if (depth == (int)comp_count) {
                bool match = true;
                for (uint32_t i = 0; i < comp_count; i++) {
                    const char *c = comps[i];
                    uint32_t clen = 0; while (c[clen] && c[clen] != '/') clen++;
                    if (!comp_eq(names[i + 1], c, clen)) { match = false; break; }
                }
                if (match) target_depth = depth;
            }
            continue;
        }

        if (token == FDT_END_NODE) {
            if (depth == target_depth) target_depth = -1;
            depth--;
            continue;
        }

        if (token == FDT_NOP) continue;
        if (token == FDT_END) break;

        if (token != FDT_PROP) return false;

        uint32_t len = be32(p); p += 4;
        uint32_t nameoff = be32(p); p += 4;
        const uint8_t *data = p;
        p += len; p = align4(p);

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

        if (depth == target_depth && streq(pname, "reg")) {
            uint32_t addr_cells = ctx->parent_addr_cells;
            uint32_t size_cells = ctx->parent_size_cells;
            if (addr_cells == 0 || addr_cells > 2) return false;
            uint32_t tuple_bytes = 4u * (addr_cells + size_cells);
            if (len < tuple_bytes) return false;

            uint64_t addr = 0;
            if (addr_cells == 1) addr = (uint64_t)be32(data);
            else addr = be64(data);

            *out_addr = addr;
            return true;
        }
    }

    return false;
}

static bool dtb_resolve_stdout_uart(uint64_t *out_uart_phys,
                                    char *out_chosen, uint32_t chosen_cap,
                                    char *out_node_path, uint32_t node_cap,
                                    char *out_key, uint32_t key_cap)
{
    if (!out_uart_phys) return false;
    *out_uart_phys = 0;
    if (out_chosen && chosen_cap) out_chosen[0] = '\0';
    if (out_node_path && node_cap) out_node_path[0] = '\0';
    if (out_key && key_cap) out_key[0] = '\0';

    char chosen[128] = {0};
    char key[32] = {0};
    if (!dtb_read_chosen_stdout(chosen, (uint32_t)sizeof(chosen), key, (uint32_t)sizeof(key))) return false;

    if (out_chosen && chosen_cap) strlcpy0(out_chosen, chosen_cap, chosen);
    if (out_key && key_cap) strlcpy0(out_key, key_cap, key);

    char path[128] = {0};
    if (chosen[0] == '/') {
        strlcpy0(path, (uint32_t)sizeof(path), chosen);
    } else {
        if (!dtb_resolve_alias(chosen, path, (uint32_t)sizeof(path))) return false;
    }

    if (out_node_path && node_cap) strlcpy0(out_node_path, node_cap, path);

    uint64_t uart_phys = 0;
    if (!dtb_find_node_reg_addr_by_path(path, &uart_phys)) return false;
    *out_uart_phys = uart_phys;
    return true;
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

    /* Build structured results up front (bounded, no allocator yet). */
    g_mem_count = 0;
    g_rsv_count = 0;
    g_parsed = false;
    ensure_parsed();

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

static void append_range(dtb_range_t *arr, uint32_t *count, uint32_t max,
                         uint64_t base, uint64_t size)
{
    if (size == 0) return;
    if (*count >= max) return;
    arr[*count].base = base;
    arr[*count].size = size;
    (*count)++;
}

/* Parse all (addr,size) tuples in a "reg" property and append into out[]. */
static void parse_reg_all(const uint8_t *data, uint32_t len,
                          uint32_t addr_cells, uint32_t size_cells,
                          dtb_range_t *out, uint32_t *io_count, uint32_t max)
{
    if (!data || len == 0) return;
    if (addr_cells == 0 || size_cells == 0) return;
    if (addr_cells > 2 || size_cells > 2) return;

    uint32_t tuple_bytes = 4u * (addr_cells + size_cells);
    if (tuple_bytes == 0) return;

    for (uint32_t off = 0; off + tuple_bytes <= len; off += tuple_bytes) {
        uint64_t addr = 0;
        uint64_t size = 0;

        const uint8_t *p = data + off;
        for (uint32_t i = 0; i < addr_cells; i++) {
            addr = (addr << 32) | (uint64_t)be32(p);
            p += 4;
        }
        for (uint32_t i = 0; i < size_cells; i++) {
            size = (size << 32) | (uint64_t)be32(p);
            p += 4;
        }
        append_range(out, io_count, max, addr, size);
    }
}


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

/* -----------------------------
 * Structured result collection
 * ----------------------------- */

static void collect_memreserve_ranges(void) {
    if (!g_rsvmap) return;

    const uint8_t *p = g_rsvmap;
    for (int i = 0; i < 128; i++) { /* hard cap */
        uint64_t addr = be64(p); p += 8;
        uint64_t size = be64(p); p += 8;
        if (addr == 0 && size == 0) break;
        append_range(g_rsv_ranges, &g_rsv_count, DTB_MAX_RESERVED_RANGES, addr, size);
    }
}

static void collect_memory_ranges(void) {
    if (!g_struct || !g_strings) return;

    node_ctx_t stack[64];
    int depth = -1;
    const uint8_t *p = g_struct;

    while (true) {
        uint32_t token = be32(p); p += 4;
        if (token == FDT_END) break;

        if (token == FDT_BEGIN_NODE) {
            const char *name = (const char *)p;
            size_t nlen = 0;
            while (p[nlen] != '\0') nlen++;
            p += (nlen + 1);
            p = align4(p);

            depth++;
            if (depth >= (int)(sizeof(stack)/sizeof(stack[0]))) {
                /* Too deep; abort collection. */
                return;
            }

            node_ctx_t *ctx = &stack[depth];
            uint32_t parent_addr = (depth == 0) ? 2 : stack[depth - 1].addr_cells;
            uint32_t parent_size = (depth == 0) ? 2 : stack[depth - 1].size_cells;
            ctx->parent_addr_cells = parent_addr;
            ctx->parent_size_cells = parent_size;
            ctx->addr_cells = parent_addr;
            ctx->size_cells = parent_size;
            ctx->is_memory = false;
            ctx->is_uart_candidate = false;

            /* Node name might be "memory" or "memory@..." */
            if (name[0] == 'm' && name[1] == 'e' && name[2] == 'm' && name[3] == 'o' &&
                name[4] == 'r' && name[5] == 'y' && (name[6] == '\0' || name[6] == '@'))
            {
                ctx->is_memory = true;
            }
            continue;
        }

        if (token == FDT_END_NODE) {
            depth--;
            continue;
        }

        if (token == FDT_NOP) continue;

        if (token == FDT_PROP) {
            uint32_t len = be32(p); p += 4;
            uint32_t nameoff = be32(p); p += 4;
            const uint8_t *data = p;
            p += len;
            p = align4(p);
            if (depth < 0) continue;

            const char *pname = (const char *)(g_strings + nameoff);
            node_ctx_t *ctx = &stack[depth];

            if (pname[0] == '#' && pname[1] == 'a') { /* #address-cells */
                if (len >= 4) ctx->addr_cells = be32(data);
                continue;
            }
            if (pname[0] == '#' && pname[1] == 's') { /* #size-cells */
                if (len >= 4) ctx->size_cells = be32(data);
                continue;
            }

            if (pname[0] == 'r' && pname[1] == 'e' && pname[2] == 'g' && ctx->is_memory) {
                parse_reg_all(data, len,
                              ctx->parent_addr_cells, ctx->parent_size_cells,
                              g_mem_ranges, &g_mem_count, DTB_MAX_MEMORY_RANGES);
                continue;
            }
            continue;
        }

        /* Unknown token; stop. */
        break;
    }
}

static void collect_reserved_memory_ranges(void) {
    if (!g_struct || !g_strings) return;

    node_ctx_t stack[64];
    int depth = -1;
    const uint8_t *p = g_struct;
    bool in_reserved = false;
    int reserved_depth = -1;

    while (true) {
        uint32_t token = be32(p); p += 4;
        if (token == FDT_END) break;

        if (token == FDT_BEGIN_NODE) {
            const char *name = (const char *)p;
            size_t nlen = 0;
            while (p[nlen] != '\0') nlen++;
            p += (nlen + 1);
            p = align4(p);

            depth++;
            if (depth >= (int)(sizeof(stack)/sizeof(stack[0]))) {
                return;
            }

            node_ctx_t *ctx = &stack[depth];
            uint32_t parent_addr = (depth == 0) ? 2 : stack[depth - 1].addr_cells;
            uint32_t parent_size = (depth == 0) ? 2 : stack[depth - 1].size_cells;
            ctx->parent_addr_cells = parent_addr;
            ctx->parent_size_cells = parent_size;
            ctx->addr_cells = parent_addr;
            ctx->size_cells = parent_size;
            ctx->is_memory = false;
            ctx->is_uart_candidate = false;

            if (!in_reserved &&
                name[0] == 'r' && name[1] == 'e' && name[2] == 's' && name[3] == 'e' &&
                name[4] == 'r' && name[5] == 'v' && name[6] == 'e' && name[7] == 'd' &&
                name[8] == '-' && name[9] == 'm' && name[10] == 'e' && name[11] == 'm' &&
                name[12] == 'o' && name[13] == 'r' && name[14] == 'y' && name[15] == '\0')
            {
                in_reserved = true;
                reserved_depth = depth;
            }
            continue;
        }

        if (token == FDT_END_NODE) {
            if (in_reserved && depth == reserved_depth) {
                in_reserved = false;
                reserved_depth = -1;
            }
            depth--;
            continue;
        }

        if (token == FDT_NOP) continue;

        if (token == FDT_PROP) {
            uint32_t len = be32(p); p += 4;
            uint32_t nameoff = be32(p); p += 4;
            const uint8_t *data = p;
            p += len;
            p = align4(p);
            if (depth < 0) continue;

            const char *pname = (const char *)(g_strings + nameoff);
            node_ctx_t *ctx = &stack[depth];

            if (pname[0] == '#' && pname[1] == 'a') { /* #address-cells */
                if (len >= 4) ctx->addr_cells = be32(data);
                continue;
            }
            if (pname[0] == '#' && pname[1] == 's') { /* #size-cells */
                if (len >= 4) ctx->size_cells = be32(data);
                continue;
            }

            if (pname[0] == 'r' && pname[1] == 'e' && pname[2] == 'g' &&
                in_reserved && reserved_depth >= 0 && depth > reserved_depth)
            {
                parse_reg_all(data, len,
                              ctx->parent_addr_cells, ctx->parent_size_cells,
                              g_rsv_ranges, &g_rsv_count, DTB_MAX_RESERVED_RANGES);
                continue;
            }
            continue;
        }

        break;
    }
}

static void ensure_parsed(void) {
    if (g_parsed) return;
    g_mem_count = 0;
    g_rsv_count = 0;
    collect_memreserve_ranges();
    collect_reserved_memory_ranges();
    collect_memory_ranges();
    g_parsed = true;
}

bool dtb_get_memory_ranges(dtb_range_t out[], uint32_t *inout_count) {
    if (!inout_count) return false;
    ensure_parsed();

    uint32_t cap = *inout_count;
    uint32_t n = g_mem_count;
    if (cap < n) n = cap;
    for (uint32_t i = 0; i < n; i++) out[i] = g_mem_ranges[i];
    *inout_count = n;
    return (g_mem_count != 0);
}

bool dtb_get_reserved_ranges(dtb_range_t out[], uint32_t *inout_count) {
    if (!inout_count) return false;
    ensure_parsed();

    uint32_t cap = *inout_count;
    uint32_t n = g_rsv_count;
    if (cap < n) n = cap;
    for (uint32_t i = 0; i < n; i++) out[i] = g_rsv_ranges[i];
    *inout_count = n;
    /* It's valid for there to be 0 reserved ranges. */
    return true;
}

bool dtb_first_memory_range(uint64_t *out_base, uint64_t *out_size) {
    dtb_range_t r[1];
    uint32_t n = 1;
    if (!dtb_get_memory_ranges(r, &n) || n == 0) return false;
    if (out_base) *out_base = r[0].base;
    if (out_size) *out_size = r[0].size;
    return true;
}

bool dtb_find_pl011_uart(uint64_t *out_phys) {
    /* Prefer /chosen stdout-path (or linux,stdout-path) -> /aliases -> reg decode. */
    uint64_t uart_phys = 0;
    if (dtb_resolve_stdout_uart(&uart_phys, NULL, 0, NULL, 0, NULL, 0) && uart_phys != 0) {
        if (out_phys) *out_phys = uart_phys;
        return true;
    }

    /* Fallback: first compatible node containing "arm,pl011". */
    uart_phys = 0;
    if (walk_struct_find(false, true, NULL, NULL, &uart_phys) && uart_phys != 0) {
        if (out_phys) *out_phys = uart_phys;
        return true;
    }

    return false;
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

    /* Memory ranges */
    {
        dtb_range_t mem[DTB_MAX_MEMORY_RANGES];
        uint32_t mem_count = DTB_MAX_MEMORY_RANGES;
        if (dtb_get_memory_ranges(mem, &mem_count) && mem_count) {
            uart_puts("DTB: memory ranges:\n");
            for (uint32_t i = 0; i < mem_count; i++) {
                uart_puts("  ["); uart_puthex64(i); uart_puts("] base=");
                uart_puthex64(mem[i].base);
                uart_puts(" size="); uart_puthex64(mem[i].size);
                uart_putnl();
            }
        } else {
            uart_puts("DTB: memory ranges: <none>\n");
        }
    }

    /* Reserved ranges (memreserve + /reserved-memory) */
    {
        dtb_range_t rsv[DTB_MAX_RESERVED_RANGES];
        uint32_t rsv_count = DTB_MAX_RESERVED_RANGES;
        if (dtb_get_reserved_ranges(rsv, &rsv_count) && rsv_count) {
            uart_puts("DTB: reserved ranges:\n");
            for (uint32_t i = 0; i < rsv_count; i++) {
                uart_puts("  ["); uart_puthex64(i); uart_puts("] base=");
                uart_puthex64(rsv[i].base);
                uart_puts(" size="); uart_puthex64(rsv[i].size);
                uart_putnl();
            }
        } else {
            uart_puts("DTB: reserved ranges: <none>\n");
        }
    }

    /* UART */
    {
        uint64_t uart_phys = 0;
        char chosen[128] = {0};
        char path[128] = {0};
        char key[32] = {0};

        if (dtb_resolve_stdout_uart(&uart_phys, chosen, (uint32_t)sizeof(chosen),
                                   path, (uint32_t)sizeof(path),
                                   key, (uint32_t)sizeof(key)))
        {
            uart_puts("DTB: chosen "); uart_puts(key[0] ? key : "stdout-path");
            uart_puts("=\""); uart_puts(chosen); uart_puts("\"\n");
            uart_puts("DTB: chosen resolved path=\""); uart_puts(path); uart_puts("\"\n");
            uart_puts("DTB: chosen uart phys="); uart_puthex64(uart_phys); uart_putnl();
        } else {
            /* Fallback: compatible scan (legacy behaviour). */
            uart_puts("DTB: chosen stdout-path not found; scanning for pl011...\n");
            if (dtb_find_pl011_uart(&uart_phys)) {
                uart_puts("DTB: pl011 uart phys="); uart_puthex64(uart_phys); uart_putnl();
            } else {
                uart_puts("DTB: pl011 uart not found\n");
            }
        }
    }
}
