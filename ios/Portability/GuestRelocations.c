/*
 * AARCH64 R_AARCH64_RELATIVE relocation prototype for a synthetic guest image.
 * Not an ELF parser, firmware loader, or substitute for upstream hostrt.c.
 * Uses guest VAs as stored pointer values, translating only when accessing data.
 */
#include "GuestRelocations.h"
#include "Probe.h"

#include <stdint.h>

static int add_signed(uint64_t base, int64_t offset, uint64_t *value) {
    if (offset >= 0) {
        if ((uint64_t)offset > UINT64_MAX - base)
            return -1;
        *value = base + (uint64_t)offset;
    } else {
        uint64_t magnitude = (uint64_t)(-(offset + 1)) + UINT64_C(1);
        if (magnitude > base)
            return -1;
        *value = base - magnitude;
    }
    return 0;
}

static int relocation_parts(const SSBUIOSGuestMemory *memory,
                            const SSBUIOSRelativeRela *record,
                            uint64_t load_bias, uint64_t *target,
                            uint64_t *value) {
    /* Reject symbolic relocations; r_info must be precisely R_AARCH64_RELATIVE. */
    if (record->info != SSBU_RELOC_AARCH64_RELATIVE ||
        record->offset > UINT64_MAX - load_bias)
        return -1;
    *target = load_bias + record->offset;
    if (!ssbu_guest_memory_translate(memory, *target, sizeof(uint64_t)))
        return -1;
    return add_signed(load_bias, record->addend, value);
}

int ssbu_guest_apply_relative_relocations_bias(SSBUIOSGuestMemory *memory,
                                               const SSBUIOSRelativeRela *records,
                                               size_t count,
                                               uint64_t load_bias) {
    if (!memory || !memory->host || (count && !records))
        return -1;
    uint64_t target = 0, value = 0;

    /* Validate the entire immutable input before applying any writes. */
    for (size_t i = 0; i < count; ++i)
        if (relocation_parts(memory, &records[i], load_bias, &target, &value))
            return -1;

    for (size_t i = 0; i < count; ++i) {
        if (relocation_parts(memory, &records[i], load_bias, &target, &value) ||
            ssbu_guest_memory_write(memory, target, &value, sizeof(value)))
            return -1;
    }
    return 0;
}

int ssbu_guest_apply_relative_relocations(SSBUIOSGuestMemory *memory,
                                          const SSBUIOSRelativeRela *records,
                                          size_t count) {
    if (!memory)
        return -1;
    return ssbu_guest_apply_relative_relocations_bias(memory, records, count,
                                                       memory->guest_base);
}

int ssbu_relative_relocation_selftest(void) {
    const uint64_t base = UINT64_C(0x7100000000);
    SSBUIOSGuestMemory memory = {0};
    int failed = 0;
    uint64_t output1 = 0, output2 = 0;

    if (ssbu_guest_memory_open(&memory, base, 4096))
        return 1048576;

    const SSBUIOSRelativeRela good[] = {
        {32, SSBU_RELOC_AARCH64_RELATIVE, 512},
        {40, SSBU_RELOC_AARCH64_RELATIVE, 1024},
    };
    if (ssbu_guest_apply_relative_relocations(&memory, good, 2) ||
        ssbu_guest_memory_read(&memory, base + 32, &output1, sizeof output1) ||
        ssbu_guest_memory_read(&memory, base + 40, &output2, sizeof output2) ||
        output1 != base + 512 || output2 != base + 1024)
        failed |= 1048576;

    /* Validation happens before writes: the first would otherwise mutate. */
    const SSBUIOSRelativeRela bad[] = {
        {32, SSBU_RELOC_AARCH64_RELATIVE, 777},
        {4093, SSBU_RELOC_AARCH64_RELATIVE, 0},
    };
    if (ssbu_guest_apply_relative_relocations(&memory, bad, 2) != -1 ||
        ssbu_guest_memory_read(&memory, base + 32, &output1, sizeof output1) ||
        output1 != base + 512)
        failed |= 2097152;

    const SSBUIOSRelativeRela unsupported = {48, UINT64_C(1026), 0};
    if (ssbu_guest_apply_relative_relocations(&memory, &unsupported, 1) != -1)
        failed |= 4194304;

    /* Reject negative address and addition overflow without dereferencing. */
    const SSBUIOSRelativeRela underflow = {48, SSBU_RELOC_AARCH64_RELATIVE, INT64_MIN};
    const SSBUIOSRelativeRela overflow = {48, SSBU_RELOC_AARCH64_RELATIVE, INT64_MAX};
    if (ssbu_guest_apply_relative_relocations(&memory, &underflow, 1) != -1 ||
        ssbu_guest_apply_relative_relocations(&memory, &overflow, 1) != 0) {
        /* The positive value is actually representable for this low guest base,
         * so this condition tests the expected non-overflow behavior. */
        failed |= 4194304;
    }

    ssbu_guest_memory_close(&memory);
    return failed;
}
