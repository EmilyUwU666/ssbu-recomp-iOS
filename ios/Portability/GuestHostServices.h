#ifndef SSBU_IOS_GUEST_HOST_SERVICES_H
#define SSBU_IOS_GUEST_HOST_SERVICES_H
#include "GuestRuntime.h"

#define SSBU_IOS_HOST_SERVICES_BASE UINT64_C(0x7f00000000)
#define SSBU_IOS_TICK_HZ UINT64_C(19200000)
typedef enum {
    SSBU_HOST_MEMCPY, SSBU_HOST_MEMMOVE, SSBU_HOST_MEMSET,
    SSBU_HOST_MEMCMP, SSBU_HOST_MEMCHR, SSBU_HOST_GET_TICK,
    SSBU_HOST_TICK_FREQUENCY, SSBU_HOST_NS_TO_TICK, SSBU_HOST_TICK_TO_NS,
    SSBU_HOST_SLEEP, SSBU_HOST_YIELD,
    SSBU_HOST_STRLEN, SSBU_HOST_STRNLEN, SSBU_HOST_STRCMP, SSBU_HOST_STRNCMP,
    SSBU_HOST_STRCPY, SSBU_HOST_STRNCPY, SSBU_HOST_STRCHR, SSBU_HOST_STRRCHR,
    SSBU_HOST_STRCAT, SSBU_HOST_STRNCAT, SSBU_HOST_STRSTR,
    SSBU_HOST_STRPBRK, SSBU_HOST_STRSPN, SSBU_HOST_STRCSPN
} SSBUIOSHostService;

/* Checked Ctx service ABI. Invalid pointers, unsupported IDs or conversion
 * overflow fail before modifying memory/registers. Whole nonzero memory
 * ranges must fit their bound arenas; memcpy overlap is rejected.
 * Zero-byte calls do not dereference pointers. Sleep/yield preserve registers.
 * Timing uses host monotonic time, not a full console clock/scheduler model.
 * Strings never cross an arena. Bounded reads stop at NUL/count; comparisons
 * stop at the first differing byte. Copies validate before writing and reject
 * overlap. Callers must synchronize concurrent guest-memory modifications.
 * Concatenation/search/set scans require a terminated first operand. Search
 * and set operands must also terminate; strncat reads at most count source
 * bytes and always writes a terminator. At count zero it validates only the
 * destination string, with no source access.
 */
int ssbu_ios_host_service(SSBUIOSHostService service, Ctx *context);

/* Optional static provider module using reserved GUEST call addresses.
 * No host/native pointer is exposed. Include both descriptors when assembling
 * a registry/runtime. Collisions with SDK exports require explicit named
 * provider preferences; strict initialization never overrides automatically.
 * The virtual call interval is not a data mapping and need not be adopted by
 * an address space. Memory-using handlers require a worker memory binding.
 */
extern const SSBUIOSGuestModule ssbu_ios_host_services_module;
extern const SSBUIOSModuleSymbols ssbu_ios_host_services_symbols;
#endif
