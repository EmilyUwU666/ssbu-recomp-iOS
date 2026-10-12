#ifndef SSBU_IOS_GUEST_MODULES_H
#define SSBU_IOS_GUEST_MODULES_H

#include "GuestDispatch.h"

/* Original static AOT descriptors. FnEntry.addr and export.offset are ELF
 * virtual offsets, rebased at lookup; host function pointers are NEVER stored
 * in guest relocation slots. Each module has an exclusive guest VA interval.
 * Tables are immutable, sorted, and borrowed for the registry's lifetime.
 * All exports must be unique (weak export interposition is not supported).
 */
typedef struct { const char *name; uint64_t offset; } SSBUIOSGuestExport;
typedef struct {
    const char *name;
    uint64_t load_bias, guest_start, guest_size;
    const FnEntry *functions;
    size_t function_count;
    const SSBUIOSGuestExport *exports;
    size_t export_count;
} SSBUIOSGuestModule;
typedef struct {
    const SSBUIOSGuestModule *modules;
    size_t count;
} SSBUIOSModuleRegistry;

/* Validate before publishing; on error result remains untouched. No dynamic
 * registration/unload. Caller keeps descriptors alive and unchanged. */
int ssbu_ios_modules_init(SSBUIOSModuleRegistry *result,
                          const SSBUIOSGuestModule *modules, size_t count);
/* Resolver callback protocol: 0 found, 1 absent, -1 invalid/error.
 * Only success writes guest_address. Exact, case-sensitive names. */
int ssbu_ios_modules_resolve(void *registry, const char *name,
                             uint64_t *guest_address);
int ssbu_ios_modules_dependency(void *registry, const char *name);
/* Dispatch only statically linked Ctx functions, at rebased guest VAs. */
int ssbu_ios_modules_dispatch(const SSBUIOSModuleRegistry *registry,
                              Ctx *context, uint64_t guest_address);

#endif
