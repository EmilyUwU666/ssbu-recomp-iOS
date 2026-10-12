/*
 * Restricted ELF64/AArch64 ET_DYN image staging for future iOS AOT host.
 * No native code is executed or dynamically loaded from this buffer.
 * Statically linked modules can resolve a restricted, eager ELF import subset.
 */
#include "GuestELFLoader.h"
#include "GuestRelocations.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    ELF_HEADER = 64, ELF_PHDR = 56, ELF_DYN = 16, ELF_RELA = 24,
    ELF_ET_DYN = 3, ELF_EM_AARCH64 = 183,
    ELF_PT_LOAD = 1, ELF_PT_DYNAMIC = 2, ELF_PT_INTERP = 3, ELF_PT_TLS = 7,
    ELF_PF_X = 1, ELF_DT_NULL = 0, ELF_DT_NEEDED = 1,
    ELF_DT_PLTRELSZ = 2, ELF_DT_HASH = 4, ELF_DT_STRTAB = 5,
    ELF_DT_SYMTAB = 6, ELF_DT_RELA = 7, ELF_DT_RELASZ = 8,
    ELF_DT_RELAENT = 9, ELF_DT_STRSZ = 10, ELF_DT_SYMENT = 11,
    ELF_DT_REL = 17, ELF_DT_RELSZ = 18, ELF_DT_RELENT = 19,
    ELF_DT_PLTREL = 20, ELF_DT_JMPREL = 23,
    ELF_DT_RELRSZ = 35, ELF_DT_RELR = 36, ELF_DT_RELRENT = 37,
    ELF_DT_ANDROID_REL = 0x6000000f, ELF_DT_ANDROID_RELSZ = 0x60000010,
    ELF_DT_ANDROID_RELA = 0x60000011, ELF_DT_ANDROID_RELASZ = 0x60000012,
    ELF_DT_VERSYM = 0x6ffffff0, ELF_DT_VERDEF = 0x6ffffffc,
    ELF_DT_VERNEED = 0x6ffffffe
};

#define MAX_GUEST_IMAGE (UINT64_C(512) * 1024 * 1024)
#define MAX_RELA_BYTES (UINT64_C(64) * 1024 * 1024)
#define MAX_PROGRAM_HEADERS 256

typedef struct {
    uint64_t vaddr, file_offset, filesz, memsz, flags;
} LoadSegment;

static int range_ok(size_t limit, uint64_t start, uint64_t length) {
    return start <= (uint64_t)limit && length <= (uint64_t)limit - start;
}
static uint16_t u16(const unsigned char *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t u32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t u64(const unsigned char *p) {
    return (uint64_t)u32(p) | ((uint64_t)u32(p + 4) << 32);
}
static int64_t s64(const unsigned char *p) {
    uint64_t b = u64(p);
    int64_t result;
    memcpy(&result, &b, sizeof result);
    return result;
}

/* Dynamic/RELA tables must actually be present in the file-backed part of
 * a PT_LOAD segment, not just in its BSS or an unmapped hole. */
static int file_backed(const LoadSegment *segments, size_t count,
                       uint64_t address, uint64_t size) {
    for (size_t i = 0; i < count; ++i) {
        if (address >= segments[i].vaddr &&
            address - segments[i].vaddr <= segments[i].filesz &&
            size <= segments[i].filesz - (address - segments[i].vaddr))
            return 1;
    }
    return 0;
}

/* Relocations are planned into private storage before any slot is changed.
 * This also prevents slots which alias metadata from changing later parsing. */
typedef struct { uint64_t target, value; } RelocationWrite;
typedef struct {
    uint64_t value[24];
    uint32_t seen;
    const unsigned char *symbols, *strings;
    uint64_t symbol_count, string_size;
} DynamicTables;

static int segment_contains(const LoadSegment *segments, size_t count,
                            uint64_t address, uint64_t bytes) {
    for (size_t i = 0; i < count; ++i) {
        if (address >= segments[i].vaddr &&
            address - segments[i].vaddr < segments[i].memsz &&
            bytes <= segments[i].memsz - (address - segments[i].vaddr))
            return 1;
    }
    return 0;
}

static const unsigned char *table_pointer(const LoadSegment *segments,
                                          size_t nsegments,
                                          const SSBUIOSGuestMemory *image,
                                          uint64_t bias, uint64_t offset,
                                          uint64_t bytes) {
    if (!bytes || bytes > MAX_RELA_BYTES || offset > UINT64_MAX - bias ||
        !file_backed(segments, nsegments, offset, bytes)) return NULL;
    return ssbu_guest_memory_translate(image, bias + offset, (size_t)bytes);
}

static int add_signed(uint64_t base, int64_t addend, uint64_t *out) {
    if (addend >= 0) {
        if ((uint64_t)addend > UINT64_MAX - base) return -1;
        *out = base + (uint64_t)addend;
    } else {
        uint64_t magnitude = (uint64_t)(-(addend + 1)) + 1;
        if (magnitude > base) return -1;
        *out = base - magnitude;
    }
    return 0;
}

static const char *dynamic_string(const DynamicTables *d, uint64_t offset) {
    if (!d->strings || offset >= d->string_size) return NULL;
    const char *name = (const char *)d->strings + offset;
    if (!*name || !memchr(name, 0, (size_t)(d->string_size - offset)))
        return NULL;
    return name;
}

static int resolve_symbol(const DynamicTables *d, uint32_t index,
                          const SSBUIOSGuestLinker *linker,
                          const LoadSegment *segments, size_t nsegments,
                          uint64_t bias, uint64_t *value) {
    if (!d->symbols || !index || index >= d->symbol_count) return -1;
    const unsigned char *sym = d->symbols + (size_t)index * 24;
    unsigned binding = sym[4] >> 4, type = sym[4] & 15;
    uint16_t section = u16(sym + 6);
    const char *name = dynamic_string(d, u32(sym));
    /* Only NOTYPE/OBJECT/FUNC, base PCS, standard visibility. Reject TLS,
     * GNU_IFUNC, variant PCS, special bindings and unsupported st_other bits. */
    if (!name || binding > 2 || type > 2 || (sym[5] & ~3u)) return -1;
    if (!section) {
        if (!binding || (sym[5] & 3)) return -1;
        int status = linker && linker->resolve
            ? linker->resolve(linker->context, name, value) : 1;
        if (!status) return 0;
        if (status == 1 && binding == 2) { *value = 0; return 0; }
        return -1;
    }
    if (section == 0xfff1) { *value = u64(sym + 8); return 0; } /* SHN_ABS */
    if (section >= 0xff00 ||
        !segment_contains(segments, nsegments, u64(sym + 8),
                          u64(sym + 16) ? u64(sym + 16) : 1) ||
        u64(sym + 8) > UINT64_MAX - bias) return -1;
    *value = bias + u64(sym + 8);
    if (binding && sym[5] == 0 && linker && linker->preempt) {
        uint64_t preferred = 0;
        int status = linker->preempt(linker->context, name, &preferred);
        if (status == 0) *value = preferred;
        else if (status != 1) return -1;
    }
    return 0;
}

static int parse_dynamic(const unsigned char *file, size_t file_size,
                         uint64_t phoff, unsigned phnum,
                         const LoadSegment *segments, size_t nsegments,
                         const SSBUIOSGuestMemory *image, uint64_t bias,
                         const SSBUIOSGuestLinker *linker,
                         RelocationWrite **writes_out, size_t *count_out) {
    uint64_t address = 0, bytes = 0;
    int has_dynamic = 0;
    for (unsigned i = 0; i < phnum; ++i) {
        const unsigned char *ph = file + phoff + (uint64_t)i * ELF_PHDR;
        uint32_t kind = u32(ph);
        if (kind == ELF_PT_INTERP || kind == ELF_PT_TLS) return -1;
        if (kind == ELF_PT_DYNAMIC) {
            if (has_dynamic || u64(ph + 32) > u64(ph + 40)) return -1;
            has_dynamic = 1;
            address = u64(ph + 16); bytes = u64(ph + 32);
            if (!range_ok(file_size, u64(ph + 8), bytes)) return -1;
            /* The dynamic PHDR must describe the same bytes as PT_LOAD. */
            int matched = 0;
            for (size_t j = 0; j < nsegments; ++j)
                if (address >= segments[j].vaddr &&
                    address - segments[j].vaddr <= segments[j].filesz &&
                    bytes <= segments[j].filesz - (address - segments[j].vaddr) &&
                    u64(ph + 8) == segments[j].file_offset + address - segments[j].vaddr)
                    matched = 1;
            if (!matched) return -1;
        }
    }
    if (!has_dynamic) return 0;
    if (!bytes || bytes % ELF_DYN) return -1;
    const unsigned char *dyn = table_pointer(segments, nsegments, image, bias,
                                             address, bytes);
    if (!dyn) return -1;
    DynamicTables d = {0};
    int saw_null = 0;
    const uint32_t supported = (1u << ELF_DT_PLTRELSZ) | (1u << ELF_DT_HASH) |
        (1u << ELF_DT_STRTAB) | (1u << ELF_DT_SYMTAB) | (1u << ELF_DT_RELA) |
        (1u << ELF_DT_RELASZ) | (1u << ELF_DT_RELAENT) | (1u << ELF_DT_STRSZ) |
        (1u << ELF_DT_SYMENT) | (1u << ELF_DT_PLTREL) | (1u << ELF_DT_JMPREL);
    for (size_t i = 0; i < bytes / ELF_DYN; ++i) {
        int64_t tag = s64(dyn + i * ELF_DYN);
        uint64_t val = u64(dyn + i * ELF_DYN + 8);
        if (tag == ELF_DT_NULL) { saw_null = 1; break; }
        if (tag == ELF_DT_NEEDED) {
            if (!linker || !linker->dependency) return -1;
            continue; /* Validated against the string table below. */
        }
        if (tag == ELF_DT_REL || tag == ELF_DT_RELSZ || tag == ELF_DT_RELENT ||
            tag == ELF_DT_RELR || tag == ELF_DT_RELRSZ || tag == ELF_DT_RELRENT ||
            tag == ELF_DT_ANDROID_REL || tag == ELF_DT_ANDROID_RELSZ ||
            tag == ELF_DT_ANDROID_RELA || tag == ELF_DT_ANDROID_RELASZ ||
            tag == ELF_DT_VERSYM || tag == ELF_DT_VERDEF || tag == ELF_DT_VERNEED)
            return -1;
        if (tag > 0 && tag < 24 && (supported & (1u << tag))) {
            uint32_t bit = 1u << tag;
            if (d.seen & bit) return -1;
            d.seen |= bit; d.value[tag] = val;
        }
    }
    if (!saw_null) return -1;
    const uint32_t string_bits = (1u << ELF_DT_STRTAB) | (1u << ELF_DT_STRSZ);
    if (d.seen & string_bits) {
        if ((d.seen & string_bits) != string_bits) return -1;
        d.string_size = d.value[ELF_DT_STRSZ];
        d.strings = table_pointer(segments, nsegments, image, bias,
                                  d.value[ELF_DT_STRTAB], d.string_size);
        if (!d.strings || d.strings[0] != 0) return -1;
    }
    const uint32_t symbol_bits = (1u << ELF_DT_SYMTAB) | (1u << ELF_DT_SYMENT) |
                                 (1u << ELF_DT_HASH);
    if (d.seen & symbol_bits) {
        if ((d.seen & symbol_bits) != symbol_bits || !d.strings ||
            d.value[ELF_DT_SYMENT] != 24) return -1;
        const unsigned char *hash = table_pointer(segments, nsegments, image, bias,
                                                   d.value[ELF_DT_HASH], 8);
        if (!hash) return -1;
        uint64_t buckets = u32(hash), chains = u32(hash + 4);
        if (!buckets || !chains || chains > MAX_RELA_BYTES / 24 ||
            !table_pointer(segments, nsegments, image, bias, d.value[ELF_DT_HASH],
                            (2 + buckets + chains) * 4)) return -1;
        d.symbol_count = chains;
        d.symbols = table_pointer(segments, nsegments, image, bias,
                                   d.value[ELF_DT_SYMTAB], chains * 24);
        if (!d.symbols) return -1;
        for (unsigned i = 0; i < 24; ++i) if (d.symbols[i]) return -1;
    }
    /* Verify DT_NEEDED without opening files, dlopen, or native ELF execution. */
    for (size_t i = 0; i < bytes / ELF_DYN; ++i) {
        int64_t tag = s64(dyn + i * ELF_DYN);
        if (tag == ELF_DT_NULL) break;
        if (tag == ELF_DT_NEEDED) {
            const char *name = dynamic_string(&d, u64(dyn + i * ELF_DYN + 8));
            if (!name || linker->dependency(linker->context, name)) return -1;
        }
    }
    const uint32_t rela_bits = (1u << ELF_DT_RELA) | (1u << ELF_DT_RELASZ);
    const uint32_t plt_bits = (1u << ELF_DT_JMPREL) | (1u << ELF_DT_PLTRELSZ) |
                              (1u << ELF_DT_PLTREL);
    if ((d.seen & (1u << ELF_DT_RELAENT)) &&
        d.value[ELF_DT_RELAENT] != ELF_RELA) return -1;
    if ((d.seen & rela_bits) && ((d.seen & rela_bits) != rela_bits ||
                               !(d.seen & (1u << ELF_DT_RELAENT)))) return -1;
    if ((d.seen & plt_bits) && ((d.seen & plt_bits) != plt_bits ||
                              d.value[ELF_DT_PLTREL] != ELF_DT_RELA)) return -1;
    uint64_t sizes[2] = {d.value[ELF_DT_RELASZ], d.value[ELF_DT_PLTRELSZ]};
    uint64_t offsets[2] = {d.value[ELF_DT_RELA], d.value[ELF_DT_JMPREL]};
    if (sizes[0] > MAX_RELA_BYTES || sizes[1] > MAX_RELA_BYTES - sizes[0] ||
        sizes[0] % ELF_RELA || sizes[1] % ELF_RELA) return -1;
    size_t total = (size_t)((sizes[0] + sizes[1]) / ELF_RELA);
    if (!total) return 0;
    RelocationWrite *writes = calloc(total, sizeof *writes);
    if (!writes) return -1;
    size_t next = 0;
    for (unsigned t = 0; t < 2; ++t) {
        if (!sizes[t]) continue;
        const unsigned char *table = table_pointer(segments, nsegments, image,
                                                    bias, offsets[t], sizes[t]);
        if (!table) goto fail;
        for (size_t i = 0; i < sizes[t] / ELF_RELA; ++i) {
            const unsigned char *rec = table + i * ELF_RELA;
            uint64_t offset = u64(rec), info = u64(rec + 8), value = 0;
            int64_t addend = s64(rec + 16);
            uint32_t type = (uint32_t)info, symbol = (uint32_t)(info >> 32);
            if (type == 0) continue; /* R_AARCH64_NONE */
            if (offset > UINT64_MAX - bias ||
                !segment_contains(segments, nsegments, offset, 8)) goto fail;
            if (type == SSBU_RELOC_AARCH64_RELATIVE) {
                if (symbol || add_signed(bias, addend, &value)) goto fail;
            } else if (type == 257 || type == 1025 || type == 1026) {
                if (resolve_symbol(&d, symbol, linker, segments, nsegments, bias,
                                    &value) || add_signed(value, addend, &value))
                    goto fail;
            } else goto fail;
            writes[next++] = (RelocationWrite){bias + offset, value};
        }
    }
    *writes_out = writes; *count_out = next;
    return 0;
fail:
    free(writes);
    return -1;
}

int ssbu_guest_elf64_load_linked(const unsigned char *file, size_t size,
                          uint64_t bias, const SSBUIOSGuestLinker *linker,
                          SSBUIOSGuestMemory *result,
                          uint64_t *entry_out) {
    if (!file || !result || result->host || !entry_out || size < ELF_HEADER)
        return -1;
    /* 64-bit little-endian ELF v1, AArch64 ET_DYN. ET_EXEC isn't
       supported until load-bias and guest reloc semantics are audited. */
    if (memcmp(file, "\x7f" "ELF", 4) || file[4] != 2 || file[5] != 1 ||
        file[6] != 1 || u16(file + 16) != ELF_ET_DYN ||
        u16(file + 18) != ELF_EM_AARCH64 || u32(file + 20) != 1 ||
        u16(file + 52) != ELF_HEADER || u16(file + 54) != ELF_PHDR)
        return -1;

    uint64_t phoff = u64(file + 32), entry = u64(file + 24);
    unsigned phnum = u16(file + 56);
    if (!phnum || phnum > MAX_PROGRAM_HEADERS ||
        !range_ok(size, phoff, (uint64_t)phnum * ELF_PHDR))
        return -1;

    LoadSegment segments[MAX_PROGRAM_HEADERS];
    size_t nsegments = 0;
    uint64_t lowest = UINT64_MAX, highest = 0;
    int entry_in_text = 0;
    for (unsigned i = 0; i < phnum; ++i) {
        const unsigned char *ph = file + phoff + (uint64_t)i * ELF_PHDR;
        if (u32(ph) != ELF_PT_LOAD)
            continue;
        uint64_t offset = u64(ph + 8), vaddr = u64(ph + 16);
        uint64_t filesz = u64(ph + 32), memsz = u64(ph + 40);
        if (memsz < filesz || !memsz || vaddr > UINT64_MAX - memsz ||
            !range_ok(size, offset, filesz))
            return -1;
        if ((u32(ph + 4) & ELF_PF_X) &&
            entry >= vaddr && entry - vaddr < filesz)
            entry_in_text = 1;
        segments[nsegments++] = (LoadSegment){
            vaddr, offset, filesz, memsz, u32(ph + 4)
        };
        if (vaddr < lowest) lowest = vaddr;
        if (vaddr + memsz > highest) highest = vaddr + memsz;
    }
    if (!nsegments || !entry_in_text || highest <= lowest ||
        highest - lowest > MAX_GUEST_IMAGE ||
        lowest > UINT64_MAX - bias || highest > UINT64_MAX - bias)
        return -1;
    for (size_t i = 0; i < nsegments; ++i) {
        for (size_t j = i + 1; j < nsegments; ++j) {
            uint64_t a0 = segments[i].vaddr, a1 = a0 + segments[i].memsz;
            uint64_t b0 = segments[j].vaddr, b1 = b0 + segments[j].memsz;
            if (a0 < b1 && b0 < a1)
                return -1;
        }
    }
    SSBUIOSGuestMemory candidate = {0};
    if (ssbu_guest_memory_open(&candidate, bias + lowest,
                               (size_t)(highest - lowest)))
        return -1;

    int okay = 0;
    RelocationWrite *writes = NULL;
    size_t write_count = 0;
    do {
        for (size_t i = 0; i < nsegments; ++i) {
            uint64_t addr = bias + segments[i].vaddr;
            if (ssbu_guest_memory_write(&candidate, addr,
                                        file + segments[i].file_offset,
                                        (size_t)segments[i].filesz))
                break;
            if (i == nsegments - 1)
                okay = 1;
        }
        if (!okay)
            break;
        okay = 0;
        if (parse_dynamic(file, size, phoff, phnum, segments, nsegments,
                          &candidate, bias, linker, &writes, &write_count))
            break;
        okay = 1;
        for (size_t i = 0; i < write_count; ++i) {
            unsigned char encoded[8];
            for (unsigned j = 0; j < 8; ++j)
                encoded[j] = (unsigned char)(writes[i].value >> (8*j));
            if (ssbu_guest_memory_write(&candidate, writes[i].target, encoded, 8)) {
                okay = 0; break;
            }
        }
    } while (0);

    free(writes);
    if (!okay) {
        ssbu_guest_memory_close(&candidate);
        return -1;
    }
    *result = candidate;
    *entry_out = bias + entry;
    return 0;
}

int ssbu_guest_elf64_load(const unsigned char *file, size_t size,
                          uint64_t bias, SSBUIOSGuestMemory *result,
                          uint64_t *entry_out) {
    return ssbu_guest_elf64_load_linked(file, size, bias, NULL, result, entry_out);
}
