/*
 * Restricted ELF64/AArch64 ET_DYN image staging for future iOS AOT host.
 * No native code is executed or dynamically loaded from this buffer.
 * Only PT_LOAD and a subset of DT_RELA are supported.
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
    ELF_DT_PLTRELSZ = 2, ELF_DT_RELA = 7, ELF_DT_RELASZ = 8,
    ELF_DT_RELAENT = 9, ELF_DT_REL = 17, ELF_DT_RELSZ = 18,
    ELF_DT_JMPREL = 23
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

static int parse_dynamic(const unsigned char *file, size_t file_size,
                         uint64_t phoff, unsigned phnum,
                         const LoadSegment *segments, size_t nsegments,
                         const SSBUIOSGuestMemory *image, uint64_t load_bias,
                         SSBUIOSRelativeRela **rela_out, size_t *count_out) {
    uint64_t dynamic_address = 0, dynamic_bytes = 0;
    int has_dynamic = 0;
    for (unsigned i = 0; i < phnum; ++i) {
        const unsigned char *ph = file + phoff + (uint64_t)i * ELF_PHDR;
        uint32_t kind = u32(ph);
        if (kind == ELF_PT_INTERP || kind == ELF_PT_TLS)
            return -1; /* Not an iOS native executable/firmware loader. */
        if (kind == ELF_PT_DYNAMIC) {
            if (has_dynamic || u64(ph + 32) > u64(ph + 40))
                return -1;
            has_dynamic = 1;
            dynamic_address = u64(ph + 16);
            dynamic_bytes = u64(ph + 32);
            if (!range_ok(file_size, u64(ph + 8), dynamic_bytes))
                return -1;
        }
    }
    if (!has_dynamic)
        return 0;
    if (!dynamic_bytes || dynamic_bytes % ELF_DYN ||
        !file_backed(segments, nsegments, dynamic_address, dynamic_bytes) ||
        dynamic_address > UINT64_MAX - load_bias)
        return -1;

    const unsigned char *dyn = ssbu_guest_memory_translate(
        image, load_bias + dynamic_address, (size_t)dynamic_bytes);
    if (!dyn)
        return -1;

    uint64_t rela = 0, relasz = 0, relaent = 0;
    int seen_rela = 0, seen_size = 0, seen_ent = 0, saw_null = 0;
    for (size_t i = 0; i < dynamic_bytes / ELF_DYN; ++i) {
        int64_t tag = s64(dyn + i * ELF_DYN);
        uint64_t value = u64(dyn + i * ELF_DYN + 8);
        if (tag == ELF_DT_NULL) { saw_null = 1; break; }
        if (tag == ELF_DT_NEEDED || tag == ELF_DT_PLTRELSZ ||
            tag == ELF_DT_REL || tag == ELF_DT_RELSZ ||
            tag == ELF_DT_JMPREL)
            return -1; /* No symbol imports, PLT or REL support yet. */
        if (tag == ELF_DT_RELA) {
            if (seen_rela++) return -1;
            rela = value;
        } else if (tag == ELF_DT_RELASZ) {
            if (seen_size++) return -1;
            relasz = value;
        } else if (tag == ELF_DT_RELAENT) {
            if (seen_ent++) return -1;
            relaent = value;
        }
    }
    if (!saw_null)
        return -1;
    if (!seen_rela && !seen_size && !seen_ent)
        return 0;
    if (!seen_rela || !seen_size || !seen_ent || relaent != ELF_RELA ||
        relasz > MAX_RELA_BYTES || relasz % ELF_RELA)
        return -1;
    if (!relasz)
        return 0;
    if (!file_backed(segments, nsegments, rela, relasz) ||
        rela > UINT64_MAX - load_bias)
        return -1;
    const unsigned char *table = ssbu_guest_memory_translate(
        image, load_bias + rela, (size_t)relasz);
    if (!table)
        return -1;

    size_t count = (size_t)(relasz / ELF_RELA);
    SSBUIOSRelativeRela *records = calloc(count, sizeof *records);
    if (!records)
        return -1;
    for (size_t i = 0; i < count; ++i) {
        const unsigned char *rec = table + i * ELF_RELA;
        records[i].offset = u64(rec);
        records[i].info = u64(rec + 8);
        records[i].addend = s64(rec + 16);
    }
    *rela_out = records;
    *count_out = count;
    return 0;
}

int ssbu_guest_elf64_load(const unsigned char *file, size_t size,
                          uint64_t bias, SSBUIOSGuestMemory *result,
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
    SSBUIOSRelativeRela *rela = NULL;
    size_t rela_count = 0;
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
                          &candidate, bias, &rela, &rela_count))
            break;
        if (ssbu_guest_apply_relative_relocations_bias(
                &candidate, rela, rela_count, bias))
            break;
        okay = 1;
    } while (0);

    free(rela);
    if (!okay) {
        ssbu_guest_memory_close(&candidate);
        return -1;
    }
    *result = candidate;
    *entry_out = bias + entry;
    return 0;
}
