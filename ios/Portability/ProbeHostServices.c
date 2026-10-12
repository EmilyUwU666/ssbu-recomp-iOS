/* Original service ABI and guest-pointer safety tests. */
#define SSBU_GUEST_ADDRESS_TRANSLATION 1
#include "GuestHostServices.h"
#include "GuestAddressSpace.h"
#include "Probe.h"
#include <stdint.h>
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
        {"_ZN2nn2os13GetSystemTickEv", 0, 0}
    };
    SSBUIOSModuleSymbols tables[] = {
        {"host-caller", imports, 3}, ssbu_ios_host_services_symbols
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
    Ctx c = {0};
    c.x[0] = DATA + 64; c.x[1] = BASE + 64; c.x[2] = 16;
    c.x[30] = BASE + 0x900; c.in_import = "service-outer"; c.import_from = 42;
    CHECK(ssbu_ios_runtime_call(&c, BASE + 0x100) == 0);
    CHECK(c.x[4] == DATA + 64 && c.x[5] == SSBU_IOS_TICK_HZ && c.x[6] > 0);
    CHECK(!memcmp(ssbu_guest_bound_pointer(DATA + 64, 16), original, 16));
    CHECK(!strcmp(c.in_import, "service-outer") && c.import_from == 42 && c.x[30] == BASE + 0x900);

    uint64_t address = 0;
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
done:
    ssbu_ios_runtime_unbind();
    ssbu_guest_memory_unbind();
    if (ssbu_ios_runtime_close(&runtime)) failed = 1;
    if (ssbu_guest_address_space_close(&space)) failed = 1;
    for (unsigned i = 0; i < 2; ++i) ssbu_guest_memory_close(&arenas[i]);
    return failed;
}
