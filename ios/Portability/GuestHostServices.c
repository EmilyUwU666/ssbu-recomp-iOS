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

int ssbu_ios_host_service(SSBUIOSHostService service, Ctx *c) {
    if (!c) return -1;
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

static const FnEntry functions[] = {
    {0x100, h_memcpy}, {0x110, h_memmove}, {0x120, h_memset}, {0x130, h_memcmp},
    {0x140, h_memchr}, {0x150, h_tick}, {0x160, h_frequency}, {0x170, h_ns_to_tick},
    {0x180, h_tick_to_ns}, {0x190, h_sleep}, {0x1a0, h_yield}
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
    {"memmove", 0x110}, {"memset", 0x120}
};
const SSBUIOSGuestModule ssbu_ios_host_services_module = {
    "ios-host-services", SSBU_IOS_HOST_SERVICES_BASE, SSBU_IOS_HOST_SERVICES_BASE,
    0x1000, functions, sizeof functions/sizeof functions[0], exports, sizeof exports/sizeof exports[0]
};
const SSBUIOSModuleSymbols ssbu_ios_host_services_symbols = {"ios-host-services", NULL, 0, NULL};
