#ifndef SSBU_IOS_GUEST_RELOCATIONS_H
#define SSBU_IOS_GUEST_RELOCATIONS_H

#include "GuestMemory.h"

/* ELF AArch64 RELATIVE relocation (r_type = 1027). */
#define SSBU_RELOC_AARCH64_RELATIVE UINT64_C(1027)

typedef struct {
    uint64_t offset;   /* Offset from memory->guest_base of 8-byte target. */
    uint64_t info;     /* ELF64_Rela r_info (relative relocations have sym=0). */
    int64_t addend;    /* Guest virtual address = guest_base + addend. */
} SSBUIOSRelativeRela;

/* Returns 0 on success, -1 for bad arguments/unsupported relocation.
 * Prevalidates ALL records so an invalid table leaves the mapping untouched.
 * Caller must guarantee exclusive access to the memory arena for the duration.
 */
int ssbu_guest_apply_relative_relocations(SSBUIOSGuestMemory *memory,
                                          const SSBUIOSRelativeRela *records,
                                          size_t count);

#endif
