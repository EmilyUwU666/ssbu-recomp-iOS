#define SSBU_GUEST_ADDRESS_TRANSLATION 1
#include "GuestHostServices.h"
#include "GuestAddressSpace.h"
#include "Probe.h"
#include <stdio.h>
#include <string.h>

#define MAIN UINT64_C(0x7a00000000)
#define SDK UINT64_C(0x7b00000000)
#define MARKER UINT64_C(0x1234567890)
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "Provider check failed at line %d: %s\n", __LINE__, #x); \
    failed = 1; goto done; } } while (0)

static void caller(Ctx *c) { c->x[30] = MAIN + 0x108; host_import(c, 0); }
static void sdk_caller(Ctx *c) { c->x[30] = SDK + 0x208; host_import(c, 0); }
static void sdk_memcpy_fixture(Ctx *c) { ++c->x[8]; c->x[0] = MARKER; }
static int call(const SSBUIOSModuleRegistry *registry, const SSBUIOSModuleSymbols *tables,
                 Ctx *context, uint64_t address) {
    SSBUIOSGuestRuntime *runtime = NULL;
    if (ssbu_ios_runtime_create(&runtime, registry, tables, 3)) return -1;
    int status = ssbu_ios_runtime_bind(runtime);
    if (!status) status = ssbu_ios_runtime_call(context, address);
    ssbu_ios_runtime_unbind();
    if (ssbu_ios_runtime_close(&runtime)) status = -1;
    return status;
}

int ssbu_guest_provider_preferences_selftest(void) {
    const FnEntry main_functions[] = {{0x100, caller}};
    const FnEntry sdk_functions[] = {{0x100, sdk_memcpy_fixture}, {0x200, sdk_caller}};
    const SSBUIOSGuestExport sdk_exports[] = {{"memcpy", 0x100}};
    SSBUIOSGuestModule modules[] = {
        {"preferred-main", MAIN, MAIN, 4096, main_functions, 1, NULL, 0},
        {"preferred-sdk", SDK, SDK, 4096, sdk_functions, 2, sdk_exports, 1},
        ssbu_ios_host_services_module
    };
    const DynSym imported[] = {{"memcpy", 0, 0}};
    const DynSym defined[] = {{"memcpy", 0x100, 1}};
    const unsigned char eligible[] = {1}, local[] = {0};
    SSBUIOSModuleSymbols tables[] = {
        {"preferred-main", imported, 1, NULL}, {"preferred-sdk", defined, 1, eligible},
        ssbu_ios_host_services_symbols
    };
    const SSBUIOSProviderPreference host[] = {{"memcpy", "ios-host-services"}};
    const SSBUIOSProviderPreference sdk[] = {{"memcpy", "preferred-sdk"}};
    SSBUIOSModuleRegistry registry = {0};
    SSBUIOSGuestMemory arenas[2] = {{0}};
    SSBUIOSGuestAddressSpace *space = NULL;
    SSBUIOSGuestRuntime *invalid_runtime = NULL;
    unsigned char original[16];
    for (unsigned i = 0; i < 16; ++i) original[i] = (unsigned char)(i + 1);
    uint64_t resolved = 123;
    int failed = 0;
    CHECK(ssbu_ios_modules_init(&registry, modules, 3) == -1 && !registry.modules);
    const SSBUIOSProviderPreference absent[] = {{"memcpy", "absent"}};
    CHECK(ssbu_ios_modules_init_preferred(&registry, modules, 3, absent, 1) == -1 && !registry.modules);
    const SSBUIOSProviderPreference bad_name[] = {{"MEMCPY", "ios-host-services"}};
    CHECK(ssbu_ios_modules_init_preferred(&registry, modules, 3, bad_name, 1) == -1);
    const SSBUIOSProviderPreference duplicate[] = {{"memcpy", "ios-host-services"}, {"memcpy", "preferred-sdk"}};
    CHECK(ssbu_ios_modules_init_preferred(&registry, modules, 3, duplicate, 2) == -1);
    const SSBUIOSProviderPreference unsorted[] = {{"memset", "ios-host-services"}, {"memcpy", "ios-host-services"}};
    CHECK(ssbu_ios_modules_init_preferred(&registry, modules, 3, unsorted, 2) == -1);
    modules[1].name = NULL;
    CHECK(ssbu_ios_modules_init_preferred(&registry, modules, 3, host, 1) == -1);
    modules[1].name = "preferred-sdk";
    const SSBUIOSGuestExport more[] = {{"memcpy", 0x100}, {"memset", 0x100}};
    modules[1].exports = more; modules[1].export_count = 2;
    CHECK(ssbu_ios_modules_init_preferred(&registry, modules, 3, host, 1) == -1);
    modules[1].exports = sdk_exports; modules[1].export_count = 1;
    CHECK(ssbu_ios_modules_init_preferred(&registry, modules, 3, host, 1) == 0);
    const unsigned char bad_flag[] = {2};
    tables[1].preemptible = bad_flag;
    CHECK(ssbu_ios_runtime_create(&invalid_runtime, &registry, tables, 3) == -1 && !invalid_runtime);
    tables[1].preemptible = eligible;
    tables[0].preemptible = eligible; /* an undefined symbol cannot opt in */
    CHECK(ssbu_ios_runtime_create(&invalid_runtime, &registry, tables, 3) == -1 && !invalid_runtime);
    tables[0].preemptible = NULL;
    CHECK(ssbu_ios_modules_resolve(&registry, "memcpy", &resolved) == 0 &&
          resolved == SSBU_IOS_HOST_SERVICES_BASE + 0x100);
    CHECK(ssbu_ios_modules_preferred(&registry, "memset", &resolved) == 1 &&
          resolved == SSBU_IOS_HOST_SERVICES_BASE + 0x100);
    SSBUIOSModuleRegistry before = registry;
    CHECK(ssbu_ios_modules_init_preferred(&registry, modules, 3, absent, 1) == -1 &&
          registry.modules == before.modules && registry.preferences == before.preferences);
    CHECK(ssbu_guest_memory_open(&arenas[0], MAIN, 4096) == 0);
    CHECK(ssbu_guest_memory_open(&arenas[1], SDK, 4096) == 0);
    CHECK(ssbu_guest_memory_write(&arenas[0], MAIN + 64, original, 16) == 0);
    CHECK(ssbu_guest_address_space_create(&space, arenas, 2) == 0);
    CHECK(ssbu_guest_address_space_bind(space) == 0);
    Ctx c = {0}; c.x[0] = SDK + 64; c.x[1] = MAIN + 64; c.x[2] = 16;
    CHECK(call(&registry, tables, &c, MAIN + 0x100) == 0 && c.x[0] == SDK + 64 && !c.x[8]);
    CHECK(!memcmp(ssbu_guest_bound_pointer(SDK + 64, 16), original, 16));
    /* Defined SDK imports opt in only through preserved eligibility metadata. */
    c.x[0] = SDK + 128;
    CHECK(call(&registry, tables, &c, SDK + 0x200) == 0 && c.x[0] == SDK + 128 && !c.x[8]);
    CHECK(!memcmp(ssbu_guest_bound_pointer(SDK + 128, 16), original, 16));
    tables[1].preemptible = local; c.x[0] = SDK + 256;
    CHECK(call(&registry, tables, &c, SDK + 0x200) == 0 && c.x[0] == MARKER && c.x[8] == 1);
    CHECK(rd32(SDK + 256) == 0);
    tables[1].preemptible = NULL; c.x[0] = SDK + 256;
    CHECK(call(&registry, tables, &c, SDK + 0x200) == 0 && c.x[0] == MARKER && c.x[8] == 2);
    tables[1].preemptible = eligible;
    CHECK(ssbu_ios_modules_init_preferred(&registry, modules, 3, sdk, 1) == 0);
    c.x[0] = SDK + 256;
    CHECK(call(&registry, tables, &c, MAIN + 0x100) == 0 && c.x[0] == MARKER && c.x[8] == 3);
    /* Registration order does not choose the winner. */
    SSBUIOSGuestModule swap = modules[1]; modules[1] = modules[2]; modules[2] = swap;
    CHECK(ssbu_ios_modules_init_preferred(&registry, modules, 3, host, 1) == 0);
    CHECK(ssbu_ios_modules_resolve(&registry, "memcpy", &resolved) == 0 &&
          resolved == SSBU_IOS_HOST_SERVICES_BASE + 0x100);
done:
    ssbu_ios_runtime_unbind(); ssbu_guest_memory_unbind();
    if (ssbu_ios_runtime_close(&invalid_runtime)) failed = 1;
    if (ssbu_guest_address_space_close(&space)) failed = 1;
    for (unsigned i = 0; i < 2; ++i) ssbu_guest_memory_close(&arenas[i]);
    return failed;
}
