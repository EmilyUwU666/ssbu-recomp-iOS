#define _POSIX_C_SOURCE 200809L
#include "GuestHostServices.h"
#include "GuestAddressSpace.h"

#include <errno.h>
#include <limits.h>
#include <sched.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

static int64_t signed_bits(uint64_t bits) {
    int64_t result; memcpy(&result, &bits, sizeof result); return result;
}
static uint64_t unsigned_bits(int64_t value) {
    uint64_t result; memcpy(&result, &value, sizeof result); return result;
}
static int convert_signed(uint64_t input, int to_tick, uint64_t *out) {
    __int128 value = signed_bits(input);
    value = to_tick ? value * SSBU_IOS_TICK_HZ / 1000000000
                    : value * 1000000000 / SSBU_IOS_TICK_HZ;
    if (value < INT64_MIN || value > INT64_MAX) return -1;
    *out = unsigned_bits((int64_t)value); return 0;
}

static int extended_string_service(SSBUIOSHostService service, Ctx *c) {
    unsigned char *destination, *source;
    size_t destination_size, source_size;
    uint64_t destination_va = c->x[0], source_va = c->x[1];
    if (ssbu_guest_bound_span(destination_va, &destination, &destination_size)) return -1;
    unsigned char *destination_end = memchr(destination, 0, destination_size);
    if (!destination_end) return -1;
    size_t destination_length = (size_t)(destination_end - destination);
    int concatenate = service == SSBU_HOST_STRCAT || service == SSBU_HOST_STRNCAT;
    int bounded = service == SSBU_HOST_STRNCAT;
    uint64_t limit = c->x[2];
    if (bounded && limit > SIZE_MAX) return -1;
    if (bounded && !limit) return 0;
    if (ssbu_guest_bound_span(source_va, &source, &source_size)) return -1;
    size_t scan_size = source_size;
    if (bounded && limit < scan_size) scan_size = (size_t)limit;
    unsigned char *source_end = memchr(source, 0, scan_size);
    if (!source_end && (!bounded || scan_size < limit)) return -1;
    size_t source_length = source_end ? (size_t)(source_end - source) : scan_size;
    if (concatenate) {
        /* Subtraction bounds the sum and reserves space for the final NUL. */
        if (source_length > destination_size - destination_length - 1) return -1;
        size_t write_size = destination_length + source_length + 1;
        size_t read_size = source_length + (source_end != NULL);
        if (destination_va < source_va + read_size &&
            source_va < destination_va + write_size) return -1;
        memcpy(destination_end, source, source_length);
        destination_end[source_length] = 0;
        return 0;
    }
    /* Both complete C strings were checked in their own bound arenas. */
    char *match;
    switch (service) {
    case SSBU_HOST_STRSTR:
        match = strstr((char *)destination, (char *)source); break;
    case SSBU_HOST_STRPBRK:
        match = strpbrk((char *)destination, (char *)source); break;
    case SSBU_HOST_STRSPN:
        c->x[0] = strspn((char *)destination, (char *)source); return 0;
    case SSBU_HOST_STRCSPN:
        c->x[0] = strcspn((char *)destination, (char *)source); return 0;
    default: return -1;
    }
    c->x[0] = match ? destination_va + (size_t)(match - (char *)destination) : 0;
    return 0;
}

static int string_service(SSBUIOSHostService service, Ctx *c) {
    uint64_t a_va = c->x[0], b_va = c->x[1];
    uint64_t limit = service == SSBU_HOST_STRNLEN ? c->x[1] : c->x[2];
    int bounded = service == SSBU_HOST_STRNLEN || service == SSBU_HOST_STRNCMP ||
                  service == SSBU_HOST_STRNCPY;
    if (bounded && limit > SIZE_MAX) return -1;
    if (bounded && !limit) {
        if (service != SSBU_HOST_STRNCPY) c->x[0] = 0;
        return 0;
    }
    unsigned char *a, *b;
    size_t available, other;
    /* Copies scan the source first, then validate their complete destination. */
    uint64_t scan_va = service == SSBU_HOST_STRCPY || service == SSBU_HOST_STRNCPY ? b_va : a_va;
    if (ssbu_guest_bound_span(scan_va, &a, &available)) return -1;
    if (service == SSBU_HOST_STRCMP || service == SSBU_HOST_STRNCMP) {
        if (ssbu_guest_bound_span(b_va, &b, &other)) return -1;
        size_t count = available < other ? available : other;
        if (bounded && limit < count) count = (size_t)limit;
        for (size_t i = 0; i < count; ++i) {
            if (a[i] != b[i] || !a[i]) {
                c->x[0] = unsigned_bits((int64_t)a[i] - b[i]); return 0;
            }
        }
        if (bounded && count == limit) { c->x[0] = 0; return 0; }
        return -1;
    }
    if (service == SSBU_HOST_STRCHR || service == SSBU_HOST_STRRCHR) {
        uint64_t found = 0;
        for (size_t i = 0; i < available; ++i) {
            if (a[i] == (unsigned char)b_va) {
                found = a_va + i;
                if (service == SSBU_HOST_STRCHR) { c->x[0] = found; return 0; }
            }
            if (!a[i]) { c->x[0] = found; return 0; }
        }
        return -1;
    }
    size_t scan = available;
    if (bounded && limit < scan) scan = (size_t)limit;
    unsigned char *end = memchr(a, 0, scan);
    if (!end && (!bounded || scan < limit)) return -1;
    size_t length = end ? (size_t)(end - a) : scan;
    if (service == SSBU_HOST_STRLEN || service == SSBU_HOST_STRNLEN) {
        c->x[0] = length; return 0;
    }
    size_t read_count = length + (end != NULL);
    size_t write_count = service == SSBU_HOST_STRNCPY ? (size_t)limit : read_count;
    b = ssbu_guest_bound_pointer(a_va, write_count);
    if (!b) return -1;
    /* Both intervals were validated; their additions cannot overflow. */
    if (a_va < b_va + read_count && b_va < a_va + write_count) return -1;
    memcpy(b, a, read_count);
    if (write_count > read_count) memset(b + read_count, 0, write_count - read_count);
    return 0;
}

int ssbu_ios_host_service(SSBUIOSHostService service, Ctx *c) {
    if (!c) return -1;
    if (service >= SSBU_HOST_STRLEN && service <= SSBU_HOST_STRRCHR)
        return string_service(service, c);
    if (service >= SSBU_HOST_STRCAT && service <= SSBU_HOST_STRCSPN)
        return extended_string_service(service, c);
    if (service >= SSBU_HOST_MEMCPY && service <= SSBU_HOST_MEMCHR) {
        uint64_t count = c->x[2];
        if (count > SIZE_MAX) return -1;
        if (!count) {
            if (service == SSBU_HOST_MEMCMP || service == SSBU_HOST_MEMCHR) c->x[0] = 0;
            return 0;
        }
        unsigned char *a = ssbu_guest_bound_pointer(c->x[0], (size_t)count);
        unsigned char *b = NULL;
        if (!a) return -1;
        if (service == SSBU_HOST_MEMCPY || service == SSBU_HOST_MEMMOVE || service == SSBU_HOST_MEMCMP) {
            b = ssbu_guest_bound_pointer(c->x[1], (size_t)count);
            if (!b) return -1;
        }
        switch (service) {
        case SSBU_HOST_MEMCPY:
            if (c->x[0] < c->x[1] + count && c->x[1] < c->x[0] + count) return -1;
            memcpy(a, b, (size_t)count); break;
        case SSBU_HOST_MEMMOVE: memmove(a, b, (size_t)count); break;
        case SSBU_HOST_MEMSET: memset(a, (unsigned char)c->x[1], (size_t)count); break;
        case SSBU_HOST_MEMCMP: c->x[0] = unsigned_bits(memcmp(a, b, (size_t)count)); break;
        case SSBU_HOST_MEMCHR: {
            unsigned char *found = memchr(a, (unsigned char)c->x[1], (size_t)count);
            c->x[0] = found ? c->x[0] + (uint64_t)(found-a) : 0;
            break;
        }
        default: return -1;
        }
        return 0;
    }
    switch (service) {
    case SSBU_HOST_GET_TICK: {
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) || now.tv_sec < 0 ||
            now.tv_nsec < 0 || now.tv_nsec >= 1000000000) return -1;
        __uint128_t ticks = (__uint128_t)(uint64_t)now.tv_sec * SSBU_IOS_TICK_HZ +
                            (uint64_t)now.tv_nsec * SSBU_IOS_TICK_HZ / 1000000000;
        if (ticks > UINT64_MAX) return -1;
        c->x[0] = (uint64_t)ticks; return 0;
    }
    case SSBU_HOST_TICK_FREQUENCY: c->x[0] = SSBU_IOS_TICK_HZ; return 0;
    case SSBU_HOST_NS_TO_TICK:
    case SSBU_HOST_TICK_TO_NS: {
        uint64_t result;
        if (convert_signed(c->x[0], service == SSBU_HOST_NS_TO_TICK, &result)) return -1;
        c->x[0] = result; return 0;
    }
    case SSBU_HOST_SLEEP: {
        int64_t ns = signed_bits(c->x[0]);
        if (ns <= 0) return sched_yield() ? -1 : 0;
        struct timespec wait = {(time_t)(ns / 1000000000), (long)(ns % 1000000000)};
        if (wait.tv_sec < 0 || (uint64_t)wait.tv_sec != (uint64_t)(ns / 1000000000)) return -1;
        struct timespec remaining;
        while (nanosleep(&wait, &remaining)) {
            if (errno != EINTR) return -1;
            wait = remaining;
        }
        return 0;
    }
    case SSBU_HOST_YIELD: return sched_yield() ? -1 : 0;
    default: return -1;
    }
}

#define ADAPTER(name, id) static void name(Ctx *c) { \
    if (ssbu_ios_host_service(id, c)) guest_trap("invalid iOS host service arguments", \
                                                c ? c->fn : 0); }
ADAPTER(h_memcpy, SSBU_HOST_MEMCPY)
ADAPTER(h_memmove, SSBU_HOST_MEMMOVE)
ADAPTER(h_memset, SSBU_HOST_MEMSET)
ADAPTER(h_memcmp, SSBU_HOST_MEMCMP)
ADAPTER(h_memchr, SSBU_HOST_MEMCHR)
ADAPTER(h_tick, SSBU_HOST_GET_TICK)
ADAPTER(h_frequency, SSBU_HOST_TICK_FREQUENCY)
ADAPTER(h_ns_to_tick, SSBU_HOST_NS_TO_TICK)
ADAPTER(h_tick_to_ns, SSBU_HOST_TICK_TO_NS)
ADAPTER(h_sleep, SSBU_HOST_SLEEP)
ADAPTER(h_yield, SSBU_HOST_YIELD)
ADAPTER(h_strlen, SSBU_HOST_STRLEN)
ADAPTER(h_strnlen, SSBU_HOST_STRNLEN)
ADAPTER(h_strcmp, SSBU_HOST_STRCMP)
ADAPTER(h_strncmp, SSBU_HOST_STRNCMP)
ADAPTER(h_strcpy, SSBU_HOST_STRCPY)
ADAPTER(h_strncpy, SSBU_HOST_STRNCPY)
ADAPTER(h_strchr, SSBU_HOST_STRCHR)
ADAPTER(h_strrchr, SSBU_HOST_STRRCHR)
ADAPTER(h_strcat, SSBU_HOST_STRCAT)
ADAPTER(h_strncat, SSBU_HOST_STRNCAT)
ADAPTER(h_strstr, SSBU_HOST_STRSTR)
ADAPTER(h_strpbrk, SSBU_HOST_STRPBRK)
ADAPTER(h_strspn, SSBU_HOST_STRSPN)
ADAPTER(h_strcspn, SSBU_HOST_STRCSPN)

static const FnEntry functions[] = {
    {0x100, h_memcpy}, {0x110, h_memmove}, {0x120, h_memset}, {0x130, h_memcmp},
    {0x140, h_memchr}, {0x150, h_tick}, {0x160, h_frequency}, {0x170, h_ns_to_tick},
    {0x180, h_tick_to_ns}, {0x190, h_sleep}, {0x1a0, h_yield},
    {0x1b0, h_strlen}, {0x1c0, h_strnlen}, {0x1d0, h_strcmp}, {0x1e0, h_strncmp},
    {0x1f0, h_strcpy}, {0x200, h_strncpy}, {0x210, h_strchr}, {0x220, h_strrchr},
    {0x230, h_strcat}, {0x240, h_strncat}, {0x250, h_strstr}, {0x260, h_strpbrk},
    {0x270, h_strspn}, {0x280, h_strcspn}
};
/* Exact upstream nn::os import names, byte-sorted for registry lookup. */
static const SSBUIOSGuestExport exports[] = {
    {"_ZN2nn2os11SleepThreadENS_8TimeSpanE", 0x190},
    {"_ZN2nn2os11YieldThreadEv", 0x1a0},
    {"_ZN2nn2os13ConvertToTickENS_8TimeSpanE", 0x170},
    {"_ZN2nn2os13GetSystemTickEv", 0x150},
    {"_ZN2nn2os17ConvertToTimeSpanENS0_4TickE", 0x180},
    {"_ZN2nn2os22GetSystemTickFrequencyEv", 0x160},
    {"memchr", 0x140}, {"memcmp", 0x130}, {"memcpy", 0x100},
    {"memmove", 0x110}, {"memset", 0x120},
    {"strcat", 0x230}, {"strchr", 0x210}, {"strcmp", 0x1d0}, {"strcpy", 0x1f0},
    {"strcspn", 0x280}, {"strlen", 0x1b0}, {"strncat", 0x240},
    {"strncmp", 0x1e0}, {"strncpy", 0x200}, {"strnlen", 0x1c0},
    {"strpbrk", 0x260}, {"strrchr", 0x220}, {"strspn", 0x270}, {"strstr", 0x250}
};
const SSBUIOSGuestModule ssbu_ios_host_services_module = {
    "ios-host-services", SSBU_IOS_HOST_SERVICES_BASE, SSBU_IOS_HOST_SERVICES_BASE,
    0x1000, functions, sizeof functions/sizeof functions[0], exports, sizeof exports/sizeof exports[0]
};
const SSBUIOSModuleSymbols ssbu_ios_host_services_symbols = {"ios-host-services", NULL, 0, NULL};
