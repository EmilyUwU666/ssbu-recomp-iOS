#ifndef SSBU_IOS_GUEST_RUNTIME_H
#define SSBU_IOS_GUEST_RUNTIME_H

#include "GuestModules.h"
typedef struct SSBUIOSGuestRuntime SSBUIOSGuestRuntime;
typedef struct {
    const char *module_name;
    const DynSym *symbols; /* Module-local generated symbol indices, not global. */
    size_t symbol_count;
    /* Optional byte-per-index opt-in for preemptible function definitions.
     * 1 permits a named preference, 0 preserves module-local definition.
     * NULL preserves all defined symbols (legacy behavior). Borrowed/immutable.
     * Undefined symbols always use registry resolution. Generated metadata
     * sets this only for callable GLOBAL/WEAK default-visible definitions.
     */
    const unsigned char *preemptible;
} SSBUIOSModuleSymbols;

/* Exactly one table per registered module (empty tables are allowed).
 * Normalizes by module name; rejects missing/duplicate tables, bad symbols
 * and defined-symbol offsets outside that module. Borrows registry/descriptors
 * and symbol/name storage, which must remain immutable/alive until close.
 * Success writes *result; failure leaves it untouched (initially NULL). */
int ssbu_ios_runtime_create(SSBUIOSGuestRuntime **result,
                            const SSBUIOSModuleRegistry *registry,
                            const SSBUIOSModuleSymbols *tables, size_t count);
/* Per-thread scope; no inherited bindings. Pins the runtime until explicit
 * unbind or thread exit. Memory address-space binding is a separate scope. */
int ssbu_ios_runtime_bind(SSBUIOSGuestRuntime *runtime);
void ssbu_ios_runtime_unbind(void);
/* Refuses close while bound. Stop new bind attempts and synchronize the owner
 * handle before destruction. Borrowed registry storage can then be released. */
int ssbu_ios_runtime_close(SSBUIOSGuestRuntime **runtime);

/* Checked variants: -1 for missing binding/address, bad index or unsupported
 * call. Import indices refer to the CURRENT EXECUTING module; outside an
 * entered runtime call, an import cannot be inferred and fails. Failed lookup
 * does not change guest registers/diagnostics. Calls preserve active module
 * scope and nested in_import/import_from metadata on normal return.
 * Generated void call_indirect/host_import wrappers abort on errors.
 * Unbind attempted within a running call is ignored. Defined DynSym.value
 * fields must be module-relative VAs. Without optional preemptibility metadata,
 * defined symbols stay local even if a preference exists for their name.
 * Calling missing weak/null functions is an error, not a no-op.
 * Optional service providers use registry modules; full SDK services,
 * unwinding and non-local exits remain unsupported. */
int ssbu_ios_runtime_call(Ctx *context, uint64_t guest_address);
int ssbu_ios_runtime_import(Ctx *context, int symbol_index);

#endif
