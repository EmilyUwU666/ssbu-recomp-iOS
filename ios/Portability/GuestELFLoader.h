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

typedef struct {
    /* Return 0 and a GUEST VA on success, 1 if missing, -1 on error.
     * A missing weak import becomes zero; strong imports fail. */
    int (*resolve)(void *context, const char *name, uint64_t *guest_address);
    /* DT_NEEDED is accepted only if this returns 0. This verifies a registered
     * dependency; it does not load libraries or execute initializers. */
    int (*dependency)(void *context, const char *name);
    void *context;
    /* Optional explicit provider selection for default-visible GLOBAL/WEAK
     * definitions. 0 selected, 1 no preference, -1 error. No preference keeps
     * image-local behavior. Local/hidden/internal/protected/SHN_ABS definitions
     * remain image-local. A returned value is a guest VA, never a host pointer.
     */
    int (*preempt)(void *context, const char *name, uint64_t *guest_address);
} SSBUIOSGuestLinker;

/* Same atomic publication contract, with bounded DT_HASH/DT_SYMTAB/DT_STRTAB
 * plus eager RELA and JMPREL import relocation. Supports RELATIVE, ABS64,
 * GLOB_DAT and JUMP_SLOT; all three symbolic kinds use S+A. No lazy binding,
 * automatic symbol interposition, GNU-hash-only images, TLS/IFUNC/versioning.
 * Defined symbols use this image's load bias (SHN_ABS stays absolute).
 * Loader inputs and registry remain immutable; callback side effects are not
 * rolled back. Caller must prepare dependencies and statically link code.
 */
int ssbu_guest_elf64_load_linked(const unsigned char *file, size_t file_size,
                                 uint64_t load_bias,
                                 const SSBUIOSGuestLinker *linker,
                                 SSBUIOSGuestMemory *arena,
                                 uint64_t *guest_entry);

#endif
