#ifndef SSBU_IOS_GUEST_HOST_SERVICES_H
#define SSBU_IOS_GUEST_HOST_SERVICES_H
#include "GuestRuntime.h"

#define SSBU_IOS_HOST_SERVICES_BASE UINT64_C(0x7f00000000)
#define SSBU_IOS_TICK_HZ UINT64_C(19200000)
typedef enum {
    SSBU_HOST_MEMCPY, SSBU_HOST_MEMMOVE, SSBU_HOST_MEMSET,
    SSBU_HOST_MEMCMP, SSBU_HOST_MEMCHR, SSBU_HOST_GET_TICK,
    SSBU_HOST_TICK_FREQUENCY, SSBU_HOST_NS_TO_TICK, SSBU_HOST_TICK_TO_NS,
    SSBU_HOST_SLEEP, SSBU_HOST_YIELD
} SSBUIOSHostService;

/* Checked Ctx service ABI. Invalid pointers, unsupported IDs or conversion
 * overflow fail before modifying memory/registers. Whole nonzero memory
 * ranges must fit their bound arenas; memcpy overlap is rejected.
 * Zero-byte calls do not dereference pointers. Sleep/yield preserve registers.
 * Timing uses host monotonic time, not a full console clock/scheduler model.
 */
int ssbu_ios_host_service(SSBUIOSHostService service, Ctx *context);

/* Optional static provider module using reserved GUEST call addresses.
 * No host/native pointer is exposed. Include both descriptors when assembling
 * a registry/runtime; collisions with SDK exports are rejected, not overridden.
 * The virtual call interval is not a data mapping and need not be adopted by
 * an address space. Memory-using handlers require a worker memory binding.
 */
extern const SSBUIOSGuestModule ssbu_ios_host_services_module;
extern const SSBUIOSModuleSymbols ssbu_ios_host_services_symbols;
#endif
