#include "boot_hal.h"
#include "platform_config.h"

#include "boot_info.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Loader configuration.
#define LOADER_SECTOR_SIZE 512u
#define LOADER_KERNEL_PHYS_BASE 0x40280000ULL

extern boot_info_t boot_info;

// Linker-provided bounds for the loader stack.
extern uint8_t __boot_stack_top[];

// -------------------------------------------------------------------------
// Minimal libc helpers (freestanding).

static void *loader_memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

static void *loader_memset(void *dst, int c, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    while (n--) {
        *d++ = (uint8_t)c;
    }
    return dst;
}

static size_t loader_strlen(const char *s)
{
    size_t n = 0;
    while (s && *s++) n++;
    return n;
}

static uint8_t to_upper_ascii(uint8_t c)
{
    if (c >= 'a' && c <= 'z') return (uint8_t)(c - 'a' + 'A');
    return c;
}

// -------------------------------------------------------------------------
// UART logging helpers.

static void loader_puts(const char *s)
{
    boot_uart_puts(s);
}

static void loader_puthex_nibble(uint8_t v)
{
    v &= 0xF;
    if (v < 10) boot_uart_putc((char)('0' + v));
    else boot_uart_putc((char)('A' + (v - 10)));
}

static void loader_puthex64(uint64_t v)
{
    for (int i = 15; i >= 0; i--) {
        uint8_t n = (uint8_t)(v >> (i * 4));
        loader_puthex_nibble(n);
    }
}

static void loader_panic(const char *msg)
{
    loader_puts("loader: ");
    loader_puts(msg);
    loader_puts("\n");
    for (;;) {
        __asm__ volatile("wfe");
    }
}

// -------------------------------------------------------------------------
// Endian helpers.

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_le64(const uint8_t *p)
{
    return (uint64_t)read_le32(p) | ((uint64_t)read_le32(p + 4) << 32);
}

static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

// -------------------------------------------------------------------------
// GPT parsing (GPT-only).

static const uint8_t gpt_guid_efi_system[16] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};
static const uint8_t gpt_guid_ms_basic[16] = {
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
    0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
};

static bool guid_is_zero(const uint8_t *g)
{
    for (int i = 0; i < 16; i++) {
        if (g[i] != 0) return false;
    }
    return true;
}

static bool guid_equals(const uint8_t *a, const uint8_t *b)
{
    for (int i = 0; i < 16; i++) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

static uint64_t gpt_find_partition_lba(void)
{
    uint8_t sector[LOADER_SECTOR_SIZE];
    if (!boot_block_read(1, 1, sector)) {
        return 0;
    }

    if (sector[0] != 'E' || sector[1] != 'F' || sector[2] != 'I' || sector[3] != ' ' ||
        sector[4] != 'P' || sector[5] != 'A' || sector[6] != 'R' || sector[7] != 'T') {
        return 0;
    }

    uint64_t entries_lba = read_le64(sector + 72);
    uint32_t num_entries = read_le32(sector + 80);
    uint32_t entry_size = read_le32(sector + 84);
    if (entries_lba == 0 || num_entries == 0 || entry_size < 128 || entry_size > LOADER_SECTOR_SIZE) {
        return 0;
    }

    uint64_t cur_lba = (uint64_t)-1;
    uint64_t fallback_first_lba = 0;
    for (uint32_t i = 0; i < num_entries; i++) {
        uint64_t entry_lba = entries_lba + ((uint64_t)i * entry_size) / LOADER_SECTOR_SIZE;
        uint32_t entry_off = (uint32_t)(((uint64_t)i * entry_size) % LOADER_SECTOR_SIZE);
        if (entry_lba != cur_lba) {
            if (!boot_block_read(entry_lba, 1, sector)) {
                return 0;
            }
            cur_lba = entry_lba;
        }
        const uint8_t *entry = sector + entry_off;
        const uint8_t *type_guid = entry + 0;
        if (guid_is_zero(type_guid)) {
            continue;
        }
        uint64_t first_lba = read_le64(entry + 32);
        if (first_lba == 0) {
            continue;
        }
        if (fallback_first_lba == 0) {
            // Accept the first non-empty GPT entry as a generic fallback.
            fallback_first_lba = first_lba;
        }
        if (!guid_equals(type_guid, gpt_guid_efi_system) && !guid_equals(type_guid, gpt_guid_ms_basic)) {
            continue;
        }
        return first_lba;
    }
    return fallback_first_lba;
}

// -------------------------------------------------------------------------
// FAT16 parsing (GPT + FAT16 only).

typedef struct {
    uint64_t part_lba;
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint16_t root_entry_count;
    uint16_t sectors_per_fat;
    uint32_t total_sectors;
    uint32_t fat_start_lba;
    uint32_t root_dir_lba;
    uint32_t root_dir_sectors;
    uint32_t data_start_lba;
} fat16_volume_t;

typedef struct {
    uint8_t name[11];
    uint8_t attr;
    uint16_t first_cluster;
    uint32_t size;
} fat16_dirent_t;

static bool fat16_parse(uint64_t part_lba, fat16_volume_t *out)
{
    uint8_t sector[LOADER_SECTOR_SIZE];
    if (!boot_block_read(part_lba, 1, sector)) {
        return false;
    }
    if (sector[510] != 0x55 || sector[511] != 0xAA) {
        return false;
    }

    uint16_t bytes_per_sector = read_le16(sector + 11);
    uint8_t sectors_per_cluster = sector[13];
    uint16_t reserved = read_le16(sector + 14);
    uint8_t num_fats = sector[16];
    uint16_t root_entries = read_le16(sector + 17);
    uint16_t total16 = read_le16(sector + 19);
    uint16_t spf16 = read_le16(sector + 22);
    uint32_t total32 = read_le32(sector + 32);

    if (bytes_per_sector != LOADER_SECTOR_SIZE) {
        return false;
    }
    if (sectors_per_cluster == 0 || reserved == 0 || num_fats == 0 || spf16 == 0) {
        return false;
    }
    if (root_entries == 0) {
        return false; // FAT32 has root_entries == 0
    }

    uint32_t total = total16 ? total16 : total32;
    if (total == 0) {
        return false;
    }

    uint32_t root_dir_sectors = ((uint32_t)root_entries * 32u + (bytes_per_sector - 1)) / bytes_per_sector;
    uint32_t fat_start_lba = (uint32_t)(part_lba + reserved);
    uint32_t root_dir_lba = fat_start_lba + (uint32_t)num_fats * (uint32_t)spf16;
    uint32_t data_start_lba = root_dir_lba + root_dir_sectors;

    out->part_lba = part_lba;
    out->bytes_per_sector = bytes_per_sector;
    out->sectors_per_cluster = sectors_per_cluster;
    out->reserved_sectors = reserved;
    out->num_fats = num_fats;
    out->root_entry_count = root_entries;
    out->sectors_per_fat = spf16;
    out->total_sectors = total;
    out->fat_start_lba = fat_start_lba;
    out->root_dir_lba = root_dir_lba;
    out->root_dir_sectors = root_dir_sectors;
    out->data_start_lba = data_start_lba;
    return true;
}

static uint64_t fat16_cluster_to_lba(const fat16_volume_t *vol, uint16_t cluster)
{
    return (uint64_t)vol->data_start_lba + ((uint64_t)(cluster - 2) * vol->sectors_per_cluster);
}

static uint16_t fat16_next_cluster(const fat16_volume_t *vol, uint16_t cluster)
{
    uint32_t fat_offset = (uint32_t)cluster * 2u;
    uint64_t fat_lba = (uint64_t)vol->fat_start_lba + (fat_offset / LOADER_SECTOR_SIZE);
    uint32_t fat_off = fat_offset % LOADER_SECTOR_SIZE;

    static uint64_t cached_lba = (uint64_t)-1;
    static uint8_t cached_sector[LOADER_SECTOR_SIZE];

    if (fat_lba != cached_lba) {
        if (!boot_block_read(fat_lba, 1, cached_sector)) {
            return 0xFFFF;
        }
        cached_lba = fat_lba;
    }

    uint16_t entry = read_le16(cached_sector + fat_off);
    return entry;
}

static bool fat16_is_eoc(uint16_t cluster)
{
    return cluster >= 0xFFF8;
}

static bool make_short_name(const char *name, uint8_t out[11])
{
    if (!name || !*name) return false;
    loader_memset(out, 0x20, 11);

    const char *dot = NULL;
    for (const char *p = name; *p; p++) {
        if (*p == '.') {
            if (dot) return false;
            dot = p;
        }
    }

    size_t base_len = dot ? (size_t)(dot - name) : loader_strlen(name);
    size_t ext_len = dot ? loader_strlen(dot + 1) : 0;
    if (base_len == 0 || base_len > 8 || ext_len > 3) {
        return false;
    }

    for (size_t i = 0; i < base_len; i++) {
        out[i] = to_upper_ascii((uint8_t)name[i]);
    }
    for (size_t i = 0; i < ext_len; i++) {
        out[8 + i] = to_upper_ascii((uint8_t)dot[1 + i]);
    }
    return true;
}

static bool name_matches(const uint8_t *entry_name, const uint8_t *target)
{
    for (int i = 0; i < 11; i++) {
        if (entry_name[i] != target[i]) return false;
    }
    return true;
}

static bool fat16_find_in_root(const fat16_volume_t *vol, const uint8_t target[11], fat16_dirent_t *out)
{
    uint8_t sector[LOADER_SECTOR_SIZE];
    for (uint32_t s = 0; s < vol->root_dir_sectors; s++) {
        if (!boot_block_read(vol->root_dir_lba + s, 1, sector)) {
            return false;
        }
        for (uint32_t off = 0; off < LOADER_SECTOR_SIZE; off += 32) {
            uint8_t first = sector[off + 0];
            if (first == 0x00) return false;
            if (first == 0xE5) continue;
            uint8_t attr = sector[off + 11];
            if (attr == 0x0F) continue; // LFN
            if (attr & 0x08) continue;  // volume label
            if (name_matches(&sector[off], target)) {
                loader_memcpy(out->name, &sector[off], 11);
                out->attr = attr;
                out->first_cluster = read_le16(&sector[off + 26]);
                out->size = read_le32(&sector[off + 28]);
                return true;
            }
        }
    }
    return false;
}

static bool fat16_find_in_dir(const fat16_volume_t *vol, uint16_t dir_cluster, const uint8_t target[11], fat16_dirent_t *out)
{
    if (dir_cluster == 0) {
        return fat16_find_in_root(vol, target, out);
    }

    uint8_t sector[LOADER_SECTOR_SIZE];
    uint16_t cluster = dir_cluster;
    while (cluster >= 2 && !fat16_is_eoc(cluster)) {
        uint64_t lba = fat16_cluster_to_lba(vol, cluster);
        for (uint32_t s = 0; s < vol->sectors_per_cluster; s++) {
            if (!boot_block_read(lba + s, 1, sector)) {
                return false;
            }
            for (uint32_t off = 0; off < LOADER_SECTOR_SIZE; off += 32) {
                uint8_t first = sector[off + 0];
                if (first == 0x00) return false;
                if (first == 0xE5) continue;
                uint8_t attr = sector[off + 11];
                if (attr == 0x0F) continue;
                if (attr & 0x08) continue;
                if (name_matches(&sector[off], target)) {
                    loader_memcpy(out->name, &sector[off], 11);
                    out->attr = attr;
                    out->first_cluster = read_le16(&sector[off + 26]);
                    out->size = read_le32(&sector[off + 28]);
                    return true;
                }
            }
        }
        cluster = fat16_next_cluster(vol, cluster);
    }
    return false;
}

static bool fat16_find_path(const fat16_volume_t *vol, const char *path, fat16_dirent_t *out)
{
    if (!path || !*path) return false;

    while (*path == '/') path++;

    uint16_t dir_cluster = 0;
    char namebuf[16];
    while (*path) {
        size_t len = 0;
        while (path[len] && path[len] != '/' && len < sizeof(namebuf) - 1) {
            namebuf[len] = path[len];
            len++;
        }
        if (path[len] && path[len] != '/') {
            return false;
        }
        namebuf[len] = '\0';
        path += len;
        while (*path == '/') path++;

        uint8_t shortname[11];
        if (!make_short_name(namebuf, shortname)) {
            return false;
        }
        fat16_dirent_t entry;
        if (!fat16_find_in_dir(vol, dir_cluster, shortname, &entry)) {
            return false;
        }
        if (*path == '\0') {
            *out = entry;
            return true;
        }
        if (!(entry.attr & 0x10)) {
            return false;
        }
        dir_cluster = entry.first_cluster;
    }
    return false;
}

static bool fat16_read_file_at(const fat16_volume_t *vol, const fat16_dirent_t *file,
                               uint32_t offset, uint32_t len, uint8_t *dst)
{
    if (!file || !dst) return false;
    if (len == 0) return true;
    if (offset > file->size) return false;
    if (offset + len > file->size) return false;

    uint32_t cluster_size = (uint32_t)vol->bytes_per_sector * vol->sectors_per_cluster;
    uint32_t skip = offset / cluster_size;
    uint32_t off_in_cluster = offset % cluster_size;

    uint16_t cluster = file->first_cluster;
    for (uint32_t i = 0; i < skip; i++) {
        cluster = fat16_next_cluster(vol, cluster);
        if (cluster < 2 || fat16_is_eoc(cluster)) {
            return false;
        }
    }

    uint8_t sector[LOADER_SECTOR_SIZE];
    uint32_t remaining = len;
    while (remaining > 0) {
        if (cluster < 2 || fat16_is_eoc(cluster)) {
            return false;
        }
        uint64_t lba = fat16_cluster_to_lba(vol, cluster);
        uint32_t sector_idx = off_in_cluster / vol->bytes_per_sector;
        uint32_t sector_off = off_in_cluster % vol->bytes_per_sector;
        for (uint32_t s = sector_idx; s < vol->sectors_per_cluster && remaining > 0; s++) {
            if (!boot_block_read(lba + s, 1, sector)) {
                return false;
            }
            uint32_t copy = vol->bytes_per_sector - sector_off;
            if (copy > remaining) copy = remaining;
            loader_memcpy(dst, sector + sector_off, copy);
            dst += copy;
            remaining -= copy;
            sector_off = 0;
        }
        off_in_cluster = 0;
        if (remaining > 0) {
            cluster = fat16_next_cluster(vol, cluster);
        }
    }
    return true;
}

// -------------------------------------------------------------------------
// ELF loader (ELF64 little-endian).

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

#define PT_LOAD 1u

static bool load_kernel_elf(const fat16_volume_t *vol, const fat16_dirent_t *file, boot_info_t *bi,
                            uint64_t *out_entry)
{
    uint8_t ehdr_buf[sizeof(Elf64_Ehdr)];
    if (!fat16_read_file_at(vol, file, 0, sizeof(ehdr_buf), ehdr_buf)) {
        return false;
    }

    Elf64_Ehdr ehdr;
    loader_memcpy(&ehdr, ehdr_buf, sizeof(ehdr));
    if (ehdr.e_ident[0] != 0x7F || ehdr.e_ident[1] != 'E' || ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F') {
        return false;
    }
    if (ehdr.e_ident[4] != 2 || ehdr.e_ident[5] != 1) { // ELF64, little-endian
        return false;
    }
    if (ehdr.e_machine != 0xB7) { // EM_AARCH64
        return false;
    }
    if (ehdr.e_phentsize < sizeof(Elf64_Phdr) || ehdr.e_phnum == 0) {
        return false;
    }

    uint64_t base_vaddr = UINT64_MAX;
    uint64_t base_paddr = UINT64_MAX;
    uint64_t min_paddr = UINT64_MAX;
    uint64_t max_paddr = 0;

    for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
        uint32_t ph_off = (uint32_t)(ehdr.e_phoff + (uint64_t)i * ehdr.e_phentsize);
        uint8_t ph_buf[sizeof(Elf64_Phdr)];
        if (!fat16_read_file_at(vol, file, ph_off, sizeof(ph_buf), ph_buf)) {
            return false;
        }
        Elf64_Phdr ph;
        loader_memcpy(&ph, ph_buf, sizeof(ph));
        if (ph.p_type != PT_LOAD || ph.p_memsz == 0) {
            continue;
        }
        if (ph.p_vaddr < base_vaddr) base_vaddr = ph.p_vaddr;
        if (ph.p_paddr != 0 && ph.p_paddr < base_paddr) base_paddr = ph.p_paddr;
    }

    if (base_vaddr == UINT64_MAX) {
        return false;
    }
    if (base_paddr == UINT64_MAX) {
        base_paddr = LOADER_KERNEL_PHYS_BASE;
    }

    for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
        uint32_t ph_off = (uint32_t)(ehdr.e_phoff + (uint64_t)i * ehdr.e_phentsize);
        uint8_t ph_buf[sizeof(Elf64_Phdr)];
        if (!fat16_read_file_at(vol, file, ph_off, sizeof(ph_buf), ph_buf)) {
            return false;
        }
        Elf64_Phdr ph;
        loader_memcpy(&ph, ph_buf, sizeof(ph));
        if (ph.p_type != PT_LOAD || ph.p_memsz == 0) {
            continue;
        }
        uint64_t dest_paddr = ph.p_paddr ? ph.p_paddr : (base_paddr + (ph.p_vaddr - base_vaddr));
        if (ph.p_filesz > 0) {
            if (!fat16_read_file_at(vol, file, (uint32_t)ph.p_offset, (uint32_t)ph.p_filesz, (uint8_t *)(uintptr_t)dest_paddr)) {
                return false;
            }
        }
        if (ph.p_memsz > ph.p_filesz) {
            loader_memset((uint8_t *)(uintptr_t)(dest_paddr + ph.p_filesz), 0, (size_t)(ph.p_memsz - ph.p_filesz));
        }
        if (dest_paddr < min_paddr) min_paddr = dest_paddr;
        uint64_t end = dest_paddr + ph.p_memsz;
        if (end > max_paddr) max_paddr = end;
    }

    if (min_paddr == UINT64_MAX || max_paddr <= min_paddr) {
        return false;
    }

    bi->kernel_phys_base = min_paddr;
    bi->kernel_size = max_paddr - min_paddr;
    bi->kernel_entry_offset = (ehdr.e_entry >= base_vaddr) ? (ehdr.e_entry - base_vaddr) : 0;

    *out_entry = ehdr.e_entry;
    return true;
}

// -------------------------------------------------------------------------
// DTB helpers.

static uint32_t dtb_total_size(const void *dtb)
{
    if (!dtb) return 0;
    const uint8_t *p = (const uint8_t *)dtb;
    if (read_be32(p) != 0xD00DFEED) {
        return 0;
    }
    return read_be32(p + 4);
}

// -------------------------------------------------------------------------
// Loader entry.

void loader_main(uint64_t dtb_pa)
{
    (void)dtb_pa;
    loader_puts("loader: boot\n");

    uint64_t part_lba = gpt_find_partition_lba();
    if (part_lba == 0) {
        loader_panic("no GPT partition found");
    }

    fat16_volume_t vol;
    if (!fat16_parse(part_lba, &vol)) {
        loader_panic("FAT16 parse failed");
    }

    const char *paths[] = {
        "KERNEL.ELF",
        "KERNEL8.ELF",
        NULL
    };

    fat16_dirent_t kernel_entry;
    bool found = false;
    for (int i = 0; paths[i]; i++) {
        if (fat16_find_path(&vol, paths[i], &kernel_entry)) {
            found = true;
            break;
        }
    }
    if (!found) {
        loader_panic("kernel not found");
    }
    if (kernel_entry.attr & 0x10) {
        loader_panic("kernel is directory");
    }

    boot_info_t *bi = &boot_info;
    loader_memset(bi, 0, sizeof(*bi));

    uint64_t entry_va = 0;
    if (!load_kernel_elf(&vol, &kernel_entry, bi, &entry_va)) {
        loader_panic("ELF load failed");
    }
    uint64_t loader_end = (uint64_t)(uintptr_t)__boot_stack_top;
    if (bi->kernel_phys_base < loader_end) {
        loader_panic("kernel overlaps loader");
    }
    if ((bi->kernel_phys_base & 0xFFFu) != 0) {
        loader_panic("kernel not page aligned");
    }
    if ((entry_va & 0x3u) != 0) {
        loader_panic("entry not aligned");
    }

    const void *dtb_ptr = boot_dtb_ptr();
    uint32_t dtb_sz = dtb_total_size(dtb_ptr);
    if (dtb_ptr && dtb_sz > 0) {
        uint64_t dtb_pa_local = (uint64_t)(uintptr_t)dtb_ptr;
        bi->dtb_ptr = (uint64_t)CAPAZ_HH_BASE + dtb_pa_local;
        bi->dtb_size = dtb_sz;
    }

    // Pass boot_info as a high-half VA to the kernel.
    uint64_t bi_pa = (uint64_t)(uintptr_t)bi;
    uint64_t bi_va = (uint64_t)CAPAZ_HH_BASE + bi_pa;

    loader_puts("loader: jump\n");
    loader_puts("loader: kernel_pa="); loader_puthex64(bi->kernel_phys_base); loader_puts("\n");
    loader_puts("loader: entry="); loader_puthex64(entry_va); loader_puts("\n");

    __asm__ volatile("dsb sy; isb");

    void (*kernel_entry_fn)(const boot_info_t *) = (void (*)(const boot_info_t *))(uintptr_t)entry_va;
    kernel_entry_fn((const boot_info_t *)(uintptr_t)bi_va);

    loader_panic("kernel returned");
}
