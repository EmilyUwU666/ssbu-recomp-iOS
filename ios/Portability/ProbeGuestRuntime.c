#define SSBU_GUEST_ADDRESS_TRANSLATION 1
#include "GuestRuntime.h"
#include "GuestAddressSpace.h"
#include "Probe.h"
#include "../tests/aot_fixture/synthetic_runtime.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "Runtime bridge check failed at line %d: %s\n", __LINE__, #x); \
    failed = 1; goto done; } } while (0)
static int errors_failed;

static void error_probe(Ctx *c) {
    Ctx before = *c;
    if (ssbu_ios_runtime_import(c, -1) != -1 ||
        ssbu_ios_runtime_import(c, 4) != -1 ||
        ssbu_ios_runtime_import(c, 2) != -1 || /* data export, not callable */
        ssbu_ios_runtime_import(c, 3) != -1 || /* required missing function */
        ssbu_ios_runtime_call(c, 0) != -1 ||
        ssbu_ios_runtime_call(c, SSBU_BRIDGE_MAIN + 0x101) != -1 ||
        memcmp(c, &before, sizeof before)) errors_failed = 1;
    if (ssbu_ios_runtime_import(c, 1) || c->x[0] != before.x[0] + 2 ||
        ssbu_ios_runtime_import(c, 1) || c->x[0] != before.x[0] + 4)
        errors_failed = 1;
    ssbu_ios_runtime_unbind(); /* ignored during a live call frame */
    if (ssbu_ios_runtime_call(c, SSBU_BRIDGE_MAIN + 0x300) ||
        c->x[0] != before.x[0] + 6) errors_failed = 1;
}

typedef struct {
    SSBUIOSGuestRuntime *runtime;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int ready, release, failed;
} RuntimeWorker;
static void *worker(void *opaque) {
    RuntimeWorker *w = opaque;
    Ctx c = {0}; c.x[0] = 10;
    if (ssbu_ios_runtime_call(&c, SSBU_BRIDGE_MAIN + 0x300) != -1 ||
        ssbu_ios_runtime_bind(w->runtime) ||
        ssbu_ios_runtime_bind(w->runtime) != -1 ||
        ssbu_ios_runtime_call(&c, SSBU_BRIDGE_MAIN + 0x300) || c.x[0] != 12 ||
        ssbu_ios_runtime_import(&c, 0) != -1) w->failed = 1;
    pthread_mutex_lock(&w->mutex);
    w->ready = 1; pthread_cond_signal(&w->cond);
    while (!w->release) pthread_cond_wait(&w->cond, &w->mutex);
    pthread_mutex_unlock(&w->mutex);
    return NULL; /* Runtime binding released by pthread destructor. */
}

int ssbu_guest_runtime_selftest(void) {
    SSBUIOSGuestMemory arenas[2] = {{0}};
    SSBUIOSGuestAddressSpace *space = NULL;
    SSBUIOSGuestRuntime *runtime = NULL;
    FnEntry functions[4];
    for (size_t i = 0; i < 3; ++i) functions[i] = fn_table_bridge_main[i];
    functions[3] = (FnEntry){0x400, error_probe};
    const SSBUIOSGuestExport main_exports[] = {{"main_callback", 0x200}};
    const SSBUIOSGuestExport sdk_exports[] = {{"sdk_bridge_update", 0x100}, {"sdk_data", 0x1800}};
    const SSBUIOSGuestModule modules[] = {
        {"bridge-main", SSBU_BRIDGE_MAIN, SSBU_BRIDGE_MAIN, 0x2000,
         functions, 4, main_exports, 1},
        {"bridge-sdk", SSBU_BRIDGE_SDK, SSBU_BRIDGE_SDK, 0x2000,
         fn_table_bridge_sdk, fn_table_bridge_sdk_count, sdk_exports, 2}
    };
    SSBUIOSModuleRegistry registry = {0};
    /* Deliberately reversed table order; both modules import DIFFERENT index 0. */
    SSBUIOSModuleSymbols tables[] = {
        {"bridge-sdk", dyn_syms_bridge_sdk, dyn_sym_count_bridge_sdk},
        {"bridge-main", dyn_syms_bridge_main, dyn_sym_count_bridge_main}
    };
    RuntimeWorker w = {.mutex = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER};
    pthread_t thread = {0}; int started = 0, failed = 0;
    uint64_t function_va = 0, data_va = 0;
    CHECK(ssbu_ios_modules_init(&registry, modules, 2) == 0);
    /* Creation validates module-table identity and symbol offsets atomically. */
    tables[0].module_name = "absent";
    CHECK(ssbu_ios_runtime_create(&runtime, &registry, tables, 2) == -1 && !runtime);
    tables[0].module_name = "bridge-main";
    CHECK(ssbu_ios_runtime_create(&runtime, &registry, tables, 2) == -1 && !runtime);
    tables[0].module_name = "bridge-sdk";
    const DynSym bad_symbol[] = {{"outside", UINT64_MAX, 1}};
    tables[0].symbols = bad_symbol; tables[0].symbol_count = 1;
    CHECK(ssbu_ios_runtime_create(&runtime, &registry, tables, 2) == -1 && !runtime);
    tables[0].symbols = dyn_syms_bridge_sdk;
    tables[0].symbol_count = dyn_sym_count_bridge_sdk;
    CHECK(ssbu_ios_runtime_create(&runtime, &registry, tables, 2) == 0);
    CHECK(ssbu_guest_memory_open(&arenas[0], SSBU_BRIDGE_MAIN, 0x2000) == 0);
    CHECK(ssbu_guest_memory_open(&arenas[1], SSBU_BRIDGE_SDK, 0x2000) == 0);
    CHECK(ssbu_ios_modules_resolve(&registry, "sdk_bridge_update", &function_va) == 0);
    CHECK(ssbu_ios_modules_resolve(&registry, "sdk_data", &data_va) == 0);
    CHECK(ssbu_guest_memory_write(&arenas[0], SSBU_BRIDGE_MAIN + 0x1100, &function_va, 8) == 0);
    CHECK(ssbu_guest_memory_write(&arenas[0], SSBU_BRIDGE_MAIN + 0x1108, &data_va, 8) == 0);
    const uint32_t twenty = 20;
    CHECK(ssbu_guest_memory_write(&arenas[1], data_va, &twenty, 4) == 0);
    CHECK(ssbu_guest_address_space_create(&space, arenas, 2) == 0);
    CHECK(ssbu_guest_address_space_bind(space) == 0);
    CHECK(ssbu_ios_runtime_bind(runtime) == 0);
    CHECK(ssbu_ios_runtime_bind(runtime) == -1);
    CHECK(ssbu_ios_runtime_close(&runtime) == -1 && runtime);
    Ctx c = {0};
    c.x[0] = 3; c.x[30] = SSBU_BRIDGE_MAIN + 0x900;
    c.in_import = "outer-sentinel"; c.import_from = 777;
    CHECK(ssbu_ios_runtime_call(&c, SSBU_BRIDGE_MAIN + 0x100) == 0);
    CHECK(c.x[0] == 48 && c.x[10] == 2 && c.x[11] == 2 && !c.x[12]);
    CHECK(rd32(data_va) == 47 && rd32(SSBU_BRIDGE_MAIN + 0x1800) == 96);
    CHECK(!strcmp(c.in_import, "outer-sentinel") && c.import_from == 777 &&
          c.x[30] == SSBU_BRIDGE_MAIN + 0x900);
    Ctx before = c;
    CHECK(ssbu_ios_runtime_import(&c, 0) == -1 && !memcmp(&c, &before, sizeof c));
    errors_failed = 0; c.x[0] = 5;
    CHECK(ssbu_ios_runtime_call(&c, SSBU_BRIDGE_MAIN + 0x400) == 0 && !errors_failed && c.x[0] == 11);
    CHECK(ssbu_ios_runtime_call(NULL, SSBU_BRIDGE_MAIN + 0x100) == -1);
    w.runtime = runtime;
    CHECK(pthread_create(&thread, NULL, worker, &w) == 0); started = 1;
    pthread_mutex_lock(&w.mutex);
    while (!w.ready) pthread_cond_wait(&w.cond, &w.mutex);
    int worker_failed = w.failed;
    pthread_mutex_unlock(&w.mutex);
    CHECK(!worker_failed);
    ssbu_ios_runtime_unbind();
    CHECK(ssbu_ios_runtime_close(&runtime) == -1 && runtime);
done:
    ssbu_ios_runtime_unbind();
    if (started) {
        pthread_mutex_lock(&w.mutex); w.release = 1;
        pthread_cond_signal(&w.cond); pthread_mutex_unlock(&w.mutex);
        pthread_join(thread, NULL);
    }
    ssbu_guest_memory_unbind();
    if (ssbu_ios_runtime_close(&runtime)) failed = 1;
    if (ssbu_guest_address_space_close(&space)) failed = 1;
    for (size_t i = 0; i < 2; ++i) ssbu_guest_memory_close(&arenas[i]);
    pthread_cond_destroy(&w.cond); pthread_mutex_destroy(&w.mutex);
    return failed;
}
