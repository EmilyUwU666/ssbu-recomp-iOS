/*
 * Entirely hand-authored ELF64/AArch64 fixture. No Nintendo binaries, firmware,
 * executable guest machine code, files, or secrets. Tests only image staging.
 */
#include "GuestELFLoader.h"
#include "Probe.h"
#include "GuestDispatch.h"
#include "../tests/aot_fixture/SyntheticAddresses.h"

#include <stdint.h>
#include <string.h>

static void w16(unsigned char *p, uint16_t x) {
    p[0] = (unsigned char)x;
    p[1] = (unsigned char)(x >> 8);
}
static void w32(unsigned char *p, uint32_t x) {
    for (int i = 0; i < 4; ++i) p[i] = (unsigned char)(x >> (8*i));
}
static void w64(unsigned char *p, uint64_t x) {
    for (int i = 0; i < 8; ++i) p[i] = (unsigned char)(x >> (8*i));
}
static void build_test_elf(unsigned char *data, size_t size) {
    memset(data, 0, size);
    data[0] = 0x7f; data[1] = 'E'; data[2] = 'L'; data[3] = 'F';
    data[4] = 2; data[5] = 1; data[6] = 1;
    w16(data + 16, 3);    /* ET_DYN */
    w16(data + 18, 183);  /* EM_AARCH64 */
    w32(data + 20, 1);
    w64(data + 24, 0x1000);  /* Entry matches the statically linked synthetic C fixture. */
    w64(data + 32, 64);      /* 2 program headers, phentsize 56 */
    w16(data + 52, 64);
    w16(data + 54, 56);
    w16(data + 56, 2);

    unsigned char *ph = data + 64;
    w32(ph, 1);            /* PT_LOAD */
    w32(ph + 4, 5);        /* R|X only synthetic bytes, never executed */
    w64(ph + 8, 0x200);    /* file payload at 0x200 */
    w64(ph + 16, 0x1000);  /* nonzero minimum vaddr exercises load bias */
    w64(ph + 32, 0x200);   /* file bytes */
    w64(ph + 40, 0x1000);  /* BSS extension (arena is 4k) */
    w64(ph + 48, 1);

    ph += 56;
    w32(ph, 2);            /* PT_DYNAMIC inside the PT_LOAD */
    w32(ph + 4, 4);
    w64(ph + 8, 0x300);
    w64(ph + 16, 0x1100);
    w64(ph + 32, 4 * 16);
    w64(ph + 40, 4 * 16);
    w64(ph + 48, 1);

    data[0x240] = 0x42;       /* regular loaded file byte */
    /* DT_RELA -> vaddr 0x1160, one RELATIVE entry. */
    w64(data + 0x300, 7);
    w64(data + 0x308, 0x1160);
    w64(data + 0x310, 8);
    w64(data + 0x318, 24);
    w64(data + 0x320, 9);
    w64(data + 0x328, 24);
    /* DT_NULL at 0x330, initialized to zero */
    w64(data + 0x360, 0x1400);  /* r_offset in BSS */
    w64(data + 0x368, 1027);    /* R_AARCH64_RELATIVE */
    w64(data + 0x370, 0x1500);  /* r_addend */
}

int ssbu_guest_elf_loader_selftest(void) {
    const uint64_t bias = UINT64_C(0x7100000000);
    unsigned char bytes[2048];
    SSBUIOSGuestMemory memory = {0};
    uint64_t entry = 0, pointer = 0;
    int failed = 0;
    build_test_elf(bytes, sizeof bytes);

    if (ssbu_guest_elf64_load(bytes, sizeof bytes, bias,
                              &memory, &entry))
        return 1 << 23;
    unsigned char copied = 0xff, bss = 0xff;
    if (entry != SSBU_SYNTHETIC_INC_ADDR || memory.guest_base != bias + 0x1000 ||
        memory.length != 0x1000 ||
        ssbu_guest_memory_read(&memory, bias + 0x1040, &copied, 1) ||
        ssbu_guest_memory_read(&memory, bias + 0x1480, &bss, 1) ||
        ssbu_guest_memory_read(&memory, bias + 0x1400,
                               &pointer, sizeof pointer) ||
        copied != 0x42 || bss != 0 || pointer != bias + 0x1500)
        failed |= 1 << 23;

    /* End-to-end synthetic image staging -> AOT guest-function dispatch:
       the ELF bytes are data only; entry resolves to a separately signed C
       function through the static table, NOT executed in guest pages. */
    uint32_t before = 9, after = 0;
    const uint64_t target = bias + UINT64_C(0x1450);
    Ctx context = {0};
    context.x[0] = target;
    if (ssbu_guest_memory_write(&memory, target, &before, sizeof before) ||
        ssbu_guest_memory_bind(&memory)) {
        failed |= 1 << 26;
    } else {
        if (ssbu_ios_dispatch_guest(&context, entry) != 0 ||
            ssbu_guest_memory_read(&memory, target, &after, sizeof after) ||
            context.x[0] != 10 || after != 10)
            failed |= 1 << 26;
        ssbu_guest_memory_unbind();
    }
    ssbu_guest_memory_close(&memory);

    /* Unsupported import relocations: failure must leave arena empty. */
    w64(bytes + 0x368, 1026);
    entry = 123;
    if (ssbu_guest_elf64_load(bytes, sizeof bytes, bias,
                              &memory, &entry) != -1 ||
        memory.host || entry != 123)
        failed |= 1 << 24;
    w64(bytes + 0x368, 1027);

    /* Invalid segment bounds, conflicting segments, wrong architecture. */
    w64(bytes + 64 + 32, UINT64_C(0xffffffffffffffff));
    if (ssbu_guest_elf64_load(bytes, sizeof bytes, bias,
                              &memory, &entry) != -1 || memory.host)
        failed |= 1 << 25;
    w64(bytes + 64 + 32, 0x200);
    w16(bytes + 18, 62); /* x86_64 */
    if (ssbu_guest_elf64_load(bytes, sizeof bytes, bias,
                              &memory, &entry) != -1 || memory.host)
        failed |= 1 << 25;
    w16(bytes + 18, 183);

    /* The program header table cannot extend beyond the supplied file. */
    w64(bytes + 32, sizeof bytes - 10);
    if (ssbu_guest_elf64_load(bytes, sizeof bytes, bias,
                              &memory, &entry) != -1 || memory.host)
        failed |= 1 << 27;
    w64(bytes + 32, 64);

    /* A second overlapping PT_LOAD must not silently replace the first. */
    w32(bytes + 64 + 56, 1);
    if (ssbu_guest_elf64_load(bytes, sizeof bytes, bias,
                              &memory, &entry) != -1 || memory.host)
        failed |= 1 << 28;
    w32(bytes + 64 + 56, 2);

    /* Dynamic imports are not supported by this loader: reject DT_NEEDED. */
    w64(bytes + 0x300, 1);
    if (ssbu_guest_elf64_load(bytes, sizeof bytes, bias,
                              &memory, &entry) != -1 || memory.host)
        failed |= 1 << 29;
    w64(bytes + 0x300, 7);

    /* DT_RELA table must point to file-backed load data, not BSS. */
    w64(bytes + 0x308, 0x1400);
    if (ssbu_guest_elf64_load(bytes, sizeof bytes, bias,
                              &memory, &entry) != -1 || memory.host)
        failed |= 1 << 30;
    w64(bytes + 0x308, 0x1160);

    return failed;
}
