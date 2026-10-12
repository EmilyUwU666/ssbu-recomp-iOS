#ifndef SSBU_IOS_GUEST_ELF_LOADER_H
#define SSBU_IOS_GUEST_ELF_LOADER_H

#include "GuestMemory.h"

/*
 * Synthetic ELF64, little-endian AArch64 ET_DYN loader.
 * Host memory remains RW/non-executable. This does not interpret or execute
 * ELF machine code: game functions are separately AOT recompiled and signed.
 *
 * A success creates an arena and returns guest entry VA. Caller owns arena.
 * On failure, arena and entry VA are left untouched (caller supplies empty
 * arena); partially loaded temporary storage is released.
 */
int ssbu_guest_elf64_load(const unsigned char *file, size_t file_size,
                          uint64_t load_bias,
                          SSBUIOSGuestMemory *arena, uint64_t *guest_entry);

#endif
