#ifndef SSBU_IOS_GUEST_HOST_SERVICES_H
#define SSBU_IOS_GUEST_HOST_SERVICES_H
#include "GuestRuntime.h"

#define SSBU_IOS_HOST_SERVICES_BASE UINT64_C(0x7f00000000)
#define SSBU_IOS_TICK_HZ UINT64_C(19200000)
/* Explicit newlib-style guest ABI; never substitute the host's ERANGE value. */
#define SSBU_IOS_GUEST_ERANGE 34
#define SSBU_IOS_GUEST_ENOMEM 12
#define SSBU_IOS_SORT_SCRATCH_LIMIT ((size_t)64 * 1024 * 1024)
typedef enum {
    SSBU_HOST_MEMCPY, SSBU_HOST_MEMMOVE, SSBU_HOST_MEMSET,
    SSBU_HOST_MEMCMP, SSBU_HOST_MEMCHR, SSBU_HOST_GET_TICK,
    SSBU_HOST_TICK_FREQUENCY, SSBU_HOST_NS_TO_TICK, SSBU_HOST_TICK_TO_NS,
    SSBU_HOST_SLEEP, SSBU_HOST_YIELD,
    SSBU_HOST_STRLEN, SSBU_HOST_STRNLEN, SSBU_HOST_STRCMP, SSBU_HOST_STRNCMP,
    SSBU_HOST_STRCPY, SSBU_HOST_STRNCPY, SSBU_HOST_STRCHR, SSBU_HOST_STRRCHR,
    SSBU_HOST_STRCAT, SSBU_HOST_STRNCAT, SSBU_HOST_STRSTR,
    SSBU_HOST_STRPBRK, SSBU_HOST_STRSPN, SSBU_HOST_STRCSPN,
    SSBU_HOST_ERRNO, SSBU_HOST_ATOI, SSBU_HOST_ATOL, SSBU_HOST_ATOLL,
    SSBU_HOST_STRTOL, SSBU_HOST_STRTOLL, SSBU_HOST_STRTOUL, SSBU_HOST_STRTOULL,
    SSBU_HOST_QSORT, SSBU_HOST_BSEARCH,
    SSBU_HOST_MALLOC, SSBU_HOST_CALLOC, SSBU_HOST_REALLOC, SSBU_HOST_FREE
} SSBUIOSHostService;

/* Checked Ctx service ABI. Invalid pointers, unsupported IDs or timing conversion
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
 * Integer parsing uses ASCII/C-locale whitespace and base 0 or 2..36, with
 * 32-bit int and 64-bit long/long long. Strto* requires a bound guest errno
 * cell; range overflow saturates, writes guest ERANGE, and remains a successful
 * checked call. Success/no conversion leaves errno unchanged. Invalid bases,
 * pointers, overlapping output cells or unterminated strings fail unchanged.
 * Atoi/atol/atoll reject overflow (undefined in the C API) without errno writes.
 * Qsort/bsearch use checked static guest comparator callbacks with copied Ctx
 * registers. Zero count does not access pointers/callbacks; nonzero count needs
 * positive element size and a whole mapped array. Bsearch validates one key
 * byte; the comparator is responsible for checking its actual key structure.
 * Qsort scratch (payload plus two index arrays) is limited to 64 MiB. Sorting
 * commits after comparisons; changed input is rejected without service writes.
 * Comparators must not modify the array, must return consistently, and cannot
 * exit non-locally. Callback side effects cannot be rolled back. Caller owns
 * array synchronization. Qsort preserves registers; bsearch changes only x0.
 * Allocating services require an explicitly configured owned guest heap and
 * bound errno cell outside it. Exhaustion/size overflow returns guest NULL and
 * writes guest ENOMEM. Successful allocation preserves errno. Allocations are
 * 16-byte aligned; zero size returns NULL, realloc(p,0) frees p. Failed realloc
 * preserves the old allocation/data. Free(NULL) is a pointer-free no-op; other
 * frees require exact live allocation bases and preserve all registers.
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
