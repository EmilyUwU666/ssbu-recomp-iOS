/* Original service ABI and guest-pointer safety tests. */
#define SSBU_GUEST_ADDRESS_TRANSLATION 1
#include "GuestHostServices.h"
#include "GuestAddressSpace.h"
#include "Probe.h"
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define BASE UINT64_C(0x7800000000)
#define DATA UINT64_C(0x7900000000)
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "Host-service check failed at line %d: %s\n", __LINE__, #x); \
    failed = 1; goto done; } } while (0)

static void caller(Ctx *c) {
    uint64_t lr = c->x[30];
    c->x[30] = BASE + 0x108;
    host_import(c, 0); /* memcpy: guest data in different arenas */
    c->x[4] = c->x[0];
    c->x[30] = BASE + 0x10c;
    host_import(c, 1); c->x[5] = c->x[0];
    c->x[30] = BASE + 0x110;
    host_import(c, 2); c->x[6] = c->x[0];
    c->x[0] = DATA + 256;
    c->x[30] = BASE + 0x114;
    host_import(c, 3); c->x[7] = c->x[0];
    c->x[0] = DATA + 384; c->x[1] = DATA + 256;
    c->x[30] = BASE + 0x118;
    host_import(c, 4); c->x[8] = c->x[0];
    c->x[30] = BASE + 0x11c;
    host_import(c, 5); c->x[9] = c->x[0];
    c->x[0] = DATA + 640; c->x[1] = DATA + 680; c->x[2] = 0;
    c->x[30] = BASE + 0x120;
    host_import(c, 6); c->x[10] = c->x[0];
    c->x[30] = BASE + 0x124;
    host_import(c, 7); c->x[11] = c->x[0];
    c->x[30] = lr;
}

static int unchanged_error(SSBUIOSHostService service, Ctx *c) {
    Ctx before; memcpy(&before, c, sizeof before);
    return ssbu_ios_host_service(service, c) == -1 && !memcmp(c, &before, sizeof before);
}

int ssbu_guest_host_services_selftest(void) {
    SSBUIOSGuestMemory arenas[2] = {{0}};
    SSBUIOSGuestAddressSpace *space = NULL;
    SSBUIOSGuestRuntime *runtime = NULL;
    const FnEntry functions[] = {{0x100, caller}};
    SSBUIOSGuestModule modules[] = {
        {"host-caller", BASE, BASE, 4096, functions, 1, NULL, 0},
        ssbu_ios_host_services_module
    };
    const DynSym imports[] = {
        {"memcpy", 0, 0}, {"_ZN2nn2os22GetSystemTickFrequencyEv", 0, 0},
        {"_ZN2nn2os13GetSystemTickEv", 0, 0}, {"strlen", 0, 0},
        {"strcat", 0, 0}, {"strstr", 0, 0}, {"strtoull", 0, 0}, {"__errno_location", 0, 0}
    };
    SSBUIOSModuleSymbols tables[] = {
        {"host-caller", imports, 8, NULL}, ssbu_ios_host_services_symbols
    };
    SSBUIOSModuleRegistry registry = {0};
    unsigned char original[32];
    for (unsigned i = 0; i < sizeof original; ++i) original[i] = (unsigned char)i;
    int failed = 0;
    CHECK(ssbu_ios_modules_init(&registry, modules, 2) == 0);
    CHECK(ssbu_ios_runtime_create(&runtime, &registry, tables, 2) == 0);
    CHECK(ssbu_guest_memory_open(&arenas[0], BASE, 4096) == 0);
    CHECK(ssbu_guest_memory_open(&arenas[1], DATA, 4096) == 0);
    CHECK(ssbu_guest_memory_write(&arenas[0], BASE + 64, original, sizeof original) == 0);
    CHECK(ssbu_guest_address_space_create(&space, arenas, 2) == 0);
    CHECK(ssbu_guest_address_space_bind(space) == 0 && ssbu_ios_runtime_bind(runtime) == 0);
    memcpy(ssbu_guest_bound_pointer(DATA + 256, 5), "abba", 5);
    memcpy(ssbu_guest_bound_pointer(DATA + 384, 2), "x", 2);
    memcpy(ssbu_guest_bound_pointer(DATA + 640, 13), "0xffffsuffix", 13);
    CHECK(ssbu_guest_errno_bind(DATA + 700) == 0);
    wr32(DATA + 700, 123);
    Ctx c = {0};
    c.x[0] = DATA + 64; c.x[1] = BASE + 64; c.x[2] = 16;
    c.x[30] = BASE + 0x900; c.in_import = "service-outer"; c.import_from = 42;
    CHECK(ssbu_ios_runtime_call(&c, BASE + 0x100) == 0);
    CHECK(c.x[4] == DATA + 64 && c.x[5] == SSBU_IOS_TICK_HZ && c.x[6] > 0);
    CHECK(c.x[7] == 4);
    CHECK(c.x[8] == DATA + 384 && c.x[9] == DATA + 385);
    CHECK(c.x[10] == 0xffff && c.x[11] == DATA + 700 && rd64(DATA + 680) == DATA + 646);
    CHECK(rd32(DATA + 700) == 123);
    CHECK(!memcmp(ssbu_guest_bound_pointer(DATA + 384, 6), "xabba", 6));
    CHECK(!memcmp(ssbu_guest_bound_pointer(DATA + 64, 16), original, 16));
    CHECK(!strcmp(c.in_import, "service-outer") && c.import_from == 42 && c.x[30] == BASE + 0x900);

    uint64_t address = 0;
    uint64_t errno_adapter = 0;
    CHECK(ssbu_ios_modules_resolve(&registry, "__errno", &address) == 0 &&
          ssbu_ios_modules_resolve(&registry, "__errno_location", &errno_adapter) == 0 &&
          address == errno_adapter);
    CHECK(ssbu_ios_modules_resolve(&registry, "memset", &address) == 0 &&
          address >= SSBU_IOS_HOST_SERVICES_BASE);
    c.x[0] = DATA + 128; c.x[1] = 0x123456ab; c.x[2] = 16;
    CHECK(ssbu_ios_runtime_call(&c, address) == 0 && c.x[0] == DATA + 128);
    for (unsigned i = 0; i < 16; ++i) CHECK(rd8(DATA + 128 + i) == 0xab);
    c.x[0] = DATA + 64; c.x[1] = 9; c.x[2] = 16;
    CHECK(ssbu_ios_host_service(SSBU_HOST_MEMCHR, &c) == 0 && c.x[0] == DATA + 73);
    c.x[0] = DATA + 64; c.x[1] = 0xff; c.x[2] = 16;
    CHECK(ssbu_ios_host_service(SSBU_HOST_MEMCHR, &c) == 0 && c.x[0] == 0);
    c.x[0] = BASE + 64; c.x[1] = DATA + 64; c.x[2] = 16;
    CHECK(ssbu_ios_host_service(SSBU_HOST_MEMCMP, &c) == 0 && c.x[0] == 0);
    c.x[0] = DATA + 64; c.x[1] = DATA + 128; c.x[2] = 1;
    CHECK(ssbu_ios_host_service(SSBU_HOST_MEMCMP, &c) == 0 && c.x[0] > INT64_MAX);

    /* Both ranges validate before mutation; memcpy overlap is undefined C,
     * so checked service rejects it rather than invoking the host's UB. */
    c.x[0] = DATA + 64; c.x[1] = BASE + 4093; c.x[2] = 16;
    CHECK(unchanged_error(SSBU_HOST_MEMCPY, &c));
    CHECK(!memcmp(ssbu_guest_bound_pointer(DATA + 64, 16), original, 16));
    c.x[0] = BASE + 68; c.x[1] = BASE + 64; c.x[2] = 16;
    CHECK(unchanged_error(SSBU_HOST_MEMCPY, &c));
    CHECK(!memcmp(ssbu_guest_bound_pointer(BASE + 64, 32), original, 32));
    CHECK(ssbu_ios_host_service(SSBU_HOST_MEMMOVE, &c) == 0 && c.x[0] == BASE + 68);
    CHECK(!memcmp(ssbu_guest_bound_pointer(BASE + 68, 16), original, 16));
    c.x[0] = DATA + 4090; c.x[1] = 0xff; c.x[2] = 16;
    CHECK(unchanged_error(SSBU_HOST_MEMSET, &c));
    CHECK(rd8(DATA + 4090) == 0);
    c.x[0] = UINT64_MAX; c.x[1] = 0; c.x[2] = 8;
    CHECK(unchanged_error(SSBU_HOST_MEMCHR, &c));
    c.x[0] = DATA + 64; c.x[1] = BASE + 64; c.x[2] = UINT64_MAX;
    CHECK(unchanged_error(SSBU_HOST_MEMMOVE, &c));
    c.x[0] = UINT64_MAX; c.x[1] = UINT64_MAX; c.x[2] = 0;
    CHECK(ssbu_ios_host_service(SSBU_HOST_MEMCPY, &c) == 0 && c.x[0] == UINT64_MAX);
    CHECK(ssbu_ios_host_service(SSBU_HOST_MEMCMP, &c) == 0 && c.x[0] == 0);
    CHECK(unchanged_error((SSBUIOSHostService)99, &c));
    CHECK(ssbu_ios_host_service(SSBU_HOST_MEMSET, NULL) == -1);

    /* Strings at the last byte, unsigned comparisons, bounded early stops,
     * zero-count invalid addresses, copy padding, and atomic rejection. */
    wr8(BASE + 4095, 'z');
    c.x[0] = BASE + 4095;
    CHECK(unchanged_error(SSBU_HOST_STRLEN, &c));
    c.x[1] = 1;
    CHECK(ssbu_ios_host_service(SSBU_HOST_STRNLEN, &c) == 0 && c.x[0] == 1);
    c.x[0] = BASE + 4095; c.x[1] = 2;
    CHECK(unchanged_error(SSBU_HOST_STRNLEN, &c));
    c.x[0] = DATA + 256; c.x[1] = UINT64_MAX;
    CHECK(ssbu_ios_host_service(SSBU_HOST_STRNLEN, &c) == 0 && c.x[0] == 4);
    c.x[0] = DATA + 256; c.x[1] = 'b';
    CHECK(ssbu_ios_host_service(SSBU_HOST_STRCHR, &c) == 0 && c.x[0] == DATA + 257);
    c.x[0] = DATA + 256;
    CHECK(ssbu_ios_host_service(SSBU_HOST_STRRCHR, &c) == 0 && c.x[0] == DATA + 258);
    c.x[0] = DATA + 256; c.x[1] = 0;
    CHECK(ssbu_ios_host_service(SSBU_HOST_STRRCHR, &c) == 0 && c.x[0] == DATA + 260);
    c.x[0] = DATA + 256; c.x[1] = 'x';
    CHECK(ssbu_ios_host_service(SSBU_HOST_STRCHR, &c) == 0 && c.x[0] == 0);
    c.x[0] = DATA + 300; c.x[1] = DATA + 256;
    CHECK(ssbu_ios_host_service(SSBU_HOST_STRCPY, &c) == 0 && c.x[0] == DATA + 300);
    c.x[1] = DATA + 256;
    CHECK(ssbu_ios_host_service(SSBU_HOST_STRCMP, &c) == 0 && c.x[0] == 0);
    wr8(DATA + 4095, 0xff);
    c.x[0] = BASE + 4095; c.x[1] = DATA + 4095;
    CHECK(ssbu_ios_host_service(SSBU_HOST_STRCMP, &c) == 0 && c.x[0] > INT64_MAX);
    wr8(DATA + 4095, 'z');
    c.x[0] = BASE + 4095; c.x[2] = 1;
    CHECK(ssbu_ios_host_service(SSBU_HOST_STRNCMP, &c) == 0 && c.x[0] == 0);
    c.x[0] = BASE + 4095; c.x[2] = 2;
    CHECK(unchanged_error(SSBU_HOST_STRNCMP, &c));
    c.x[0] = DATA + 320; c.x[1] = DATA + 256; c.x[2] = 8;
    memset(ssbu_guest_bound_pointer(DATA + 320, 8), 0xff, 8);
    CHECK(ssbu_ios_host_service(SSBU_HOST_STRNCPY, &c) == 0 && c.x[0] == DATA + 320);
    CHECK(!memcmp(ssbu_guest_bound_pointer(DATA + 320, 8), "abba\0\0\0", 8));
    c.x[0] = DATA + 340; c.x[1] = BASE + 4095; c.x[2] = 1;
    CHECK(ssbu_ios_host_service(SSBU_HOST_STRNCPY, &c) == 0 && rd8(DATA + 340) == 'z');
    c.x[0] = DATA + 257; c.x[1] = DATA + 256;
    CHECK(unchanged_error(SSBU_HOST_STRCPY, &c));
    CHECK(!memcmp(ssbu_guest_bound_pointer(DATA + 256, 5), "abba", 5));
    c.x[0] = DATA + 4095; c.x[1] = DATA + 256;
    CHECK(unchanged_error(SSBU_HOST_STRCPY, &c) && rd8(DATA + 4095) == 'z');
    c.x[0] = DATA + 340; c.x[1] = BASE + 4095; c.x[2] = 2;
    CHECK(unchanged_error(SSBU_HOST_STRNCPY, &c) && rd8(DATA + 340) == 'z');
    c.x[0] = UINT64_MAX; c.x[1] = UINT64_MAX; c.x[2] = 0;
    CHECK(ssbu_ios_host_service(SSBU_HOST_STRNCPY, &c) == 0 && c.x[0] == UINT64_MAX);
    CHECK(ssbu_ios_host_service(SSBU_HOST_STRNCMP, &c) == 0 && c.x[0] == 0);
    c.x[0] = UINT64_MAX; c.x[1] = 0;
    CHECK(ssbu_ios_host_service(SSBU_HOST_STRNLEN, &c) == 0 && c.x[0] == 0);
    unsigned char *span = (unsigned char *)(uintptr_t)1;
    size_t span_length = 123;
    CHECK(ssbu_guest_bound_span(BASE + 4096, &span, &span_length) == -1 &&
          span == (unsigned char *)(uintptr_t)1 && span_length == 123);
    CHECK(ssbu_guest_bound_span(BASE + 4095, &span, &span_length) == 0 && span_length == 1);

    /* Concatenation adds a NUL even when the count truncates a nonterminated
     * source. It validates the existing destination and rejects all overlap. */
    c.x[0] = DATA + 384; c.x[1] = BASE + 4095; c.x[2] = 1;
    CHECK(ssbu_ios_host_service(SSBU_HOST_STRNCAT, &c) == 0 && c.x[0] == DATA + 384);
    CHECK(!memcmp(ssbu_guest_bound_pointer(DATA + 384, 7), "xabbaz", 7));
    c.x[1] = UINT64_MAX; c.x[2] = 0;
    CHECK(ssbu_ios_host_service(SSBU_HOST_STRNCAT, &c) == 0 && c.x[0] == DATA + 384);
    c.x[1] = DATA + 385;
    CHECK(unchanged_error(SSBU_HOST_STRCAT, &c));
    CHECK(!memcmp(ssbu_guest_bound_pointer(DATA + 384, 7), "xabbaz", 7));
    c.x[1] = BASE + 4095; c.x[2] = 2;
    CHECK(unchanged_error(SSBU_HOST_STRNCAT, &c));
    CHECK(!memcmp(ssbu_guest_bound_pointer(DATA + 384, 7), "xabbaz", 7));
    CHECK(unchanged_error(SSBU_HOST_STRCAT, &c));
    c.x[0] = DATA + 4095; c.x[1] = DATA + 256; c.x[2] = 0;
    CHECK(unchanged_error(SSBU_HOST_STRNCAT, &c) && rd8(DATA + 4095) == 'z');
    wr8(DATA + 4093, 'q'); wr8(DATA + 4094, 0); wr8(DATA + 4095, 0xa5);
    c.x[0] = DATA + 4093; c.x[1] = BASE + 4095; c.x[2] = 1;
    CHECK(ssbu_ios_host_service(SSBU_HOST_STRNCAT, &c) == 0 && c.x[0] == DATA + 4093);
    CHECK(rd8(DATA + 4093) == 'q' && rd8(DATA + 4094) == 'z' && rd8(DATA + 4095) == 0);
    c.x[1] = DATA + 256; c.x[2] = UINT64_MAX;
    CHECK(unchanged_error(SSBU_HOST_STRCAT, &c));
    CHECK(unchanged_error(SSBU_HOST_STRNCAT, &c));
    CHECK(rd8(DATA + 4094) == 'z' && rd8(DATA + 4095) == 0);
    /* A short terminated source is valid even when the limit is very large. */
    c.x[0] = DATA + 384; c.x[1] = DATA + 256;
    CHECK(ssbu_ios_host_service(SSBU_HOST_STRNCAT, &c) == 0);
    CHECK(!memcmp(ssbu_guest_bound_pointer(DATA + 384, 11), "xabbazabba", 11));

    /* Search/set operands stay guest addresses; empty and high-byte sets use
     * the host C byte semantics only after full arena-bounded validation. */
    memcpy(ssbu_guest_bound_pointer(BASE + 512, 3), "az", 3);
    const struct { SSBUIOSHostService id; uint64_t source; uint64_t result; } searches[] = {
        {SSBU_HOST_STRSTR, DATA + 256, DATA + 385},
        {SSBU_HOST_STRSTR, DATA + 260, DATA + 384},
        {SSBU_HOST_STRSTR, BASE + 4093, 0},
        {SSBU_HOST_STRPBRK, BASE + 512, DATA + 385},
        {SSBU_HOST_STRSPN, BASE + 512, 0},
        {SSBU_HOST_STRCSPN, BASE + 512, 1},
        {SSBU_HOST_STRPBRK, DATA + 260, 0},
        {SSBU_HOST_STRSPN, DATA + 260, 0},
        {SSBU_HOST_STRCSPN, DATA + 260, 10}
    };
    /* An absent needle in the same arena as the final unterminated byte. */
    wr8(BASE + 4093, 'q'); wr8(BASE + 4094, 0);
    for (size_t i = 0; i < sizeof searches / sizeof searches[0]; ++i) {
        c.x[0] = DATA + 384; c.x[1] = searches[i].source;
        CHECK(ssbu_ios_host_service(searches[i].id, &c) == 0 && c.x[0] == searches[i].result);
    }
    wr8(BASE + 520, 0xff); wr8(BASE + 521, 0xff); wr8(BASE + 522, 'a'); wr8(BASE + 523, 0);
    wr8(DATA + 520, 0xff); wr8(DATA + 521, 0);
    c.x[0] = BASE + 520; c.x[1] = DATA + 520;
    CHECK(ssbu_ios_host_service(SSBU_HOST_STRSPN, &c) == 0 && c.x[0] == 2);
    c.x[0] = BASE + 520;
    CHECK(ssbu_ios_host_service(SSBU_HOST_STRPBRK, &c) == 0 && c.x[0] == BASE + 520);
    const SSBUIOSHostService scans[] = {SSBU_HOST_STRSTR, SSBU_HOST_STRPBRK,
                                      SSBU_HOST_STRSPN, SSBU_HOST_STRCSPN};
    for (size_t i = 0; i < sizeof scans / sizeof scans[0]; ++i) {
        c.x[0] = DATA + 384; c.x[1] = BASE + 4095;
        CHECK(unchanged_error(scans[i], &c));
        c.x[0] = BASE + 4095; c.x[1] = DATA + 260;
        CHECK(unchanged_error(scans[i], &c));
        c.x[0] = DATA + 260; c.x[1] = DATA + 260;
        CHECK(ssbu_ios_host_service(scans[i], &c) == 0 &&
              c.x[0] == (scans[i] == SSBU_HOST_STRSTR ? DATA + 260 : 0));
    }

    /* Differential C-locale parsing against host libc, including limits,
     * overflow end pointers, no conversion and prefixes across all bases. */
    const char *numbers[] = {"", " \t-", "xyz", "0", "09", "0x", "0xG", "0x1f!",
        "-1", "+42tail", " \t\n\r\v\f-077end", "9223372036854775807",
        "9223372036854775808", "-9223372036854775808", "-9223372036854775809",
        "18446744073709551615", "18446744073709551616", "-18446744073709551615",
        "-18446744073709551616", "zzZZz", "0123456789abcdefABCDEFz"};
    const SSBUIOSHostService parsers[] = {SSBU_HOST_STRTOL, SSBU_HOST_STRTOLL,
                                        SSBU_HOST_STRTOUL, SSBU_HOST_STRTOULL};
    for (size_t p = 0; p < sizeof parsers / sizeof parsers[0]; ++p) {
        for (int base = 0; base <= 36; ++base) {
            if (base == 1) continue;
            for (size_t n = 0; n < sizeof numbers / sizeof numbers[0]; ++n) {
                memcpy(ssbu_guest_bound_pointer(BASE + 768, strlen(numbers[n]) + 1),
                       numbers[n], strlen(numbers[n]) + 1);
                char *host_end;
                errno = 0;
                uint64_t expected = p >= 2 ? strtoull(numbers[n], &host_end, base) :
                                            (uint64_t)strtoll(numbers[n], &host_end, base);
                uint32_t expected_errno = errno == ERANGE ? SSBU_IOS_GUEST_ERANGE : 123;
                wr32(DATA + 700, 123); wr64(DATA + 680, UINT64_MAX);
                c.x[0] = BASE + 768; c.x[1] = DATA + 680; c.x[2] = (unsigned)base;
                CHECK(ssbu_ios_host_service(parsers[p], &c) == 0 && c.x[0] == expected);
                CHECK(rd64(DATA + 680) == BASE + 768 + (size_t)(host_end - numbers[n]));
                CHECK(rd32(DATA + 700) == expected_errno);
            }
        }
    }
    CHECK(ssbu_guest_errno_bind(DATA + 4094) == -1);
    CHECK(ssbu_guest_errno_bind(0) == -1);
    uint64_t errno_location = 0;
    CHECK(ssbu_guest_errno_location(&errno_location) == 0 && errno_location == DATA + 700);
    const int invalid_bases[] = {-1, 1, 37};
    for (size_t i = 0; i < sizeof invalid_bases / sizeof invalid_bases[0]; ++i) {
        c.x[0] = BASE + 768; c.x[1] = DATA + 680; c.x[2] = (uint32_t)invalid_bases[i];
        wr64(DATA + 680, 99); wr32(DATA + 700, 123);
        CHECK(unchanged_error(SSBU_HOST_STRTOL, &c) && rd64(DATA + 680) == 99 && rd32(DATA + 700) == 123);
    }
    c.x[2] = 10; c.x[1] = DATA + 4092;
    CHECK(unchanged_error(SSBU_HOST_STRTOULL, &c));
    c.x[1] = DATA + 700;
    CHECK(unchanged_error(SSBU_HOST_STRTOULL, &c) && rd32(DATA + 700) == 123);
    c.x[1] = BASE + 768;
    CHECK(unchanged_error(SSBU_HOST_STRTOL, &c));
    c.x[0] = BASE + 4095; c.x[1] = DATA + 680;
    CHECK(unchanged_error(SSBU_HOST_STRTOL, &c) && rd64(DATA + 680) == 99);
    CHECK(ssbu_guest_errno_bind(BASE + 768) == 0);
    c.x[0] = BASE + 768;
    CHECK(unchanged_error(SSBU_HOST_STRTOL, &c));
    CHECK(ssbu_guest_errno_bind(DATA + 700) == 0);
    /* Int-returning atoi is 32-bit; long and long long are guest LP64. */
    memcpy(ssbu_guest_bound_pointer(BASE + 768, 12), "-2147483648", 12);
    c.x[0] = BASE + 768;
    CHECK(ssbu_ios_host_service(SSBU_HOST_ATOI, &c) == 0 && c.x[0] == (uint64_t)(int64_t)INT32_MIN);
    memcpy(ssbu_guest_bound_pointer(BASE + 768, 11), "2147483648", 11);
    c.x[0] = BASE + 768;
    CHECK(unchanged_error(SSBU_HOST_ATOI, &c));
    CHECK(ssbu_ios_host_service(SSBU_HOST_ATOL, &c) == 0 && c.x[0] == UINT64_C(2147483648));
    memcpy(ssbu_guest_bound_pointer(BASE + 768, 21), "-9223372036854775808", 21);
    c.x[0] = BASE + 768;
    CHECK(ssbu_ios_host_service(SSBU_HOST_ATOLL, &c) == 0 && c.x[0] == (uint64_t)INT64_MIN);
    memcpy(ssbu_guest_bound_pointer(BASE + 768, 21), "18446744073709551616", 21);
    c.x[0] = BASE + 768; c.x[1] = 0; c.x[2] = 10;
    CHECK(ssbu_ios_host_service(SSBU_HOST_STRTOULL, &c) == 0 && c.x[0] == UINT64_MAX &&
          rd32(DATA + 700) == SSBU_IOS_GUEST_ERANGE);
    c.x[0] = BASE + 768;
    CHECK(unchanged_error(SSBU_HOST_ATOL, &c) && rd32(DATA + 700) == SSBU_IOS_GUEST_ERANGE);

    /* Exact signed conversions, including truncation toward zero. */
    c.x[0] = 1000000000;
    CHECK(ssbu_ios_host_service(SSBU_HOST_NS_TO_TICK, &c) == 0 && c.x[0] == SSBU_IOS_TICK_HZ);
    CHECK(ssbu_ios_host_service(SSBU_HOST_TICK_TO_NS, &c) == 0 && c.x[0] == 1000000000);
    c.x[0] = (uint64_t)(int64_t)-1000;
    CHECK(ssbu_ios_host_service(SSBU_HOST_NS_TO_TICK, &c) == 0 && c.x[0] == (uint64_t)(int64_t)-19);
    CHECK(ssbu_ios_host_service(SSBU_HOST_TICK_TO_NS, &c) == 0 && c.x[0] == (uint64_t)(int64_t)-989);
    c.x[0] = INT64_MAX;
    CHECK(unchanged_error(SSBU_HOST_TICK_TO_NS, &c));
    c.x[0] = (uint64_t)INT64_MIN;
    CHECK(unchanged_error(SSBU_HOST_TICK_TO_NS, &c));
    CHECK(ssbu_ios_host_service(SSBU_HOST_GET_TICK, &c) == 0);
    uint64_t before = c.x[0];
    c.x[0] = 2000000; /* 2 ms; lower elapsed bound only, no scheduling ceiling. */
    Ctx sleep_before; memcpy(&sleep_before, &c, sizeof c);
    CHECK(ssbu_ios_host_service(SSBU_HOST_SLEEP, &c) == 0 && !memcmp(&sleep_before, &c, sizeof c));
    CHECK(ssbu_ios_host_service(SSBU_HOST_GET_TICK, &c) == 0 && c.x[0] >= before &&
          c.x[0] - before >= 38399);
    c.x[0] = (uint64_t)(int64_t)-1;
    CHECK(ssbu_ios_host_service(SSBU_HOST_SLEEP, &c) == 0 && c.x[0] == UINT64_MAX);
    CHECK(ssbu_ios_host_service(SSBU_HOST_YIELD, &c) == 0 && c.x[0] == UINT64_MAX);
    ssbu_guest_memory_unbind();
    c.x[0] = DATA + 64; c.x[1] = BASE + 64; c.x[2] = 8;
    CHECK(unchanged_error(SSBU_HOST_MEMCPY, &c));
    CHECK(unchanged_error(SSBU_HOST_ERRNO, &c));
    errno_location = 42;
    CHECK(ssbu_guest_errno_location(&errno_location) == -1 && errno_location == 42);
    CHECK(ssbu_guest_address_space_bind(space) == 0);
    c.x[0] = BASE + 768; c.x[1] = DATA + 680; c.x[2] = 10;
    CHECK(unchanged_error(SSBU_HOST_STRTOULL, &c)); /* Rebind never inherits errno. */
    CHECK(ssbu_guest_errno_bind(DATA + 700) == 0);
done:
    ssbu_ios_runtime_unbind();
    ssbu_guest_memory_unbind();
    if (ssbu_ios_runtime_close(&runtime)) failed = 1;
    if (ssbu_guest_address_space_close(&space)) failed = 1;
    for (unsigned i = 0; i < 2; ++i) ssbu_guest_memory_close(&arenas[i]);
    return failed;
}
