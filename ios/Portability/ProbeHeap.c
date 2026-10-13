/* Original bounded-heap/provider/concurrent-worker fixtures. */
#define SSBU_GUEST_ADDRESS_TRANSLATION 1
#include "GuestHeap.h"
#include "GuestHostServices.h"
#include "Probe.h"
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>

#define BASE UINT64_C(0x7a00000000)
#define HEAP UINT64_C(0x7b00000000)
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Heap check failed at %d: %s\n", __LINE__, #x); failed = 1; goto done; } } while (0)

typedef struct {
    SSBUIOSGuestAddressSpace *space;
    pthread_mutex_t mutex;
    uint64_t live[4], handoff;
    int failed;
} Pool;
typedef struct { Pool *pool; unsigned index; } Job;
static void *heap_worker(void *opaque) {
    Job *job = opaque; Pool *pool = job->pool; unsigned index = job->index;
    int failed = 0;
    uint64_t address = 99;
    if (ssbu_guest_bound_pointer(HEAP, 1) || ssbu_guest_heap_allocate(64, 0, &address) != -1 ||
        address != 99 || ssbu_guest_address_space_bind(pool->space) ||
        ssbu_guest_errno_bind(BASE + 2112 + index * 4)) { failed = 1; goto done; }
    if (!index && ssbu_guest_heap_free(pool->handoff)) { failed = 1; goto done; }
    wr32(BASE + 2112 + index * 4, 100 + index);
    for (unsigned round = 0; round < 128; ++round) {
        Ctx c = {0}; c.x[0] = 64;
        if (ssbu_ios_host_service(SSBU_HOST_MALLOC, &c) || !c.x[0] || (c.x[0] & 15)) { failed = 1; break; }
        pthread_mutex_lock(&pool->mutex);
        for (unsigned i = 0; i < 4; ++i)
            if (pool->live[i] && c.x[0] < pool->live[i] + 64 && pool->live[i] < c.x[0] + 64) failed = 1;
        pool->live[index] = c.x[0];
        pthread_mutex_unlock(&pool->mutex);
        uint64_t pattern = ((uint64_t)(index + 1) << 32) | round;
        for (unsigned i = 0; i < 8; ++i) wr64(c.x[0] + i * 8, pattern);
        sched_yield();
        for (unsigned i = 0; i < 8; ++i) if (rd64(c.x[0] + i * 8) != pattern) failed = 1;
        if (rd32(BASE + 2112 + index * 4) != 100 + index) failed = 1;
        pthread_mutex_lock(&pool->mutex);
        if (ssbu_ios_host_service(SSBU_HOST_FREE, &c)) failed = 1;
        pool->live[index] = 0;
        pthread_mutex_unlock(&pool->mutex);
        if (failed) break;
    }
done:
    pthread_mutex_lock(&pool->mutex); pool->failed |= failed; pthread_mutex_unlock(&pool->mutex);
    if (!(index & 1)) ssbu_guest_memory_unbind();
    return NULL; /* Other workers exercise the binding-key destructor. */
}
static void heap_caller(Ctx *c) {
    uint64_t lr = c->x[30];
    c->x[0] = 24; c->x[30] = BASE + 0x108; host_import(c, 0);
    uint64_t pointer = c->x[0]; wr64(pointer, UINT64_C(0x123456789abcdef0));
    c->x[1] = 80; c->x[30] = BASE + 0x10c; host_import(c, 2);
    c->x[12] = c->x[0]; c->x[13] = rd64(c->x[0]);
    c->x[30] = BASE + 0x110; host_import(c, 3);
    c->x[0] = 4; c->x[1] = 8; c->x[30] = BASE + 0x114; host_import(c, 1);
    c->x[14] = c->x[0]; c->x[15] = rd64(c->x[0]);
    c->x[30] = BASE + 0x118; host_import(c, 3);
    c->x[30] = lr;
}
static int rejected(SSBUIOSHostService service, Ctx *c) {
    Ctx before; memcpy(&before, c, sizeof before);
    return ssbu_ios_host_service(service, c) == -1 && !memcmp(c, &before, sizeof before);
}
int ssbu_guest_heap_selftest(void) {
    SSBUIOSGuestMemory arenas[2] = {{0}};
    SSBUIOSGuestAddressSpace *space = NULL;
    SSBUIOSGuestRuntime *runtime = NULL;
    const FnEntry functions[] = {{0x100, heap_caller}};
    SSBUIOSGuestModule modules[] = {{"heap-caller", BASE, BASE, 4096, functions, 1, NULL, 0}, ssbu_ios_host_services_module};
    const DynSym imports[] = {{"malloc", 0, 0}, {"calloc", 0, 0}, {"realloc", 0, 0}, {"free", 0, 0}};
    SSBUIOSModuleSymbols tables[] = {{"heap-caller", imports, 4, NULL}, ssbu_ios_host_services_symbols};
    SSBUIOSModuleRegistry registry = {0};
    Pool pool = {0}; pthread_mutex_init(&pool.mutex, NULL);
    pthread_t threads[4]; Job jobs[4]; unsigned started = 0;
    int failed = 0;
    CHECK(ssbu_ios_modules_init(&registry, modules, 2) == 0);
    CHECK(ssbu_ios_runtime_create(&runtime, &registry, tables, 2) == 0);
    CHECK(ssbu_guest_memory_open(&arenas[0], BASE, 4096) == 0);
    CHECK(ssbu_guest_memory_open(&arenas[1], HEAP, 4096) == 0);
    unsigned char seed[16]; memset(seed, 0xab, sizeof seed);
    CHECK(ssbu_guest_memory_write(&arenas[1], HEAP, seed, sizeof seed) == 0);
    CHECK(ssbu_guest_address_space_create(&space, arenas, 2) == 0);
    CHECK(ssbu_guest_address_space_bind(space) == 0 && ssbu_guest_errno_bind(BASE + 2048) == 0);
    Ctx c = {0}; c.x[0] = 16; wr32(BASE + 2048, 123);
    CHECK(rejected(SSBU_HOST_MALLOC, &c) && rd32(BASE + 2048) == 123);
    CHECK(ssbu_guest_address_space_heap(space, HEAP, 4096) == -1); /* Bound workers. */
    ssbu_guest_memory_unbind();
    CHECK(ssbu_guest_address_space_heap(space, HEAP + 1, 4096) == -1);
    CHECK(ssbu_guest_address_space_heap(space, HEAP, 4095) == -1);
    CHECK(ssbu_guest_address_space_heap(space, HEAP + 4080, 32) == -1);
    CHECK(ssbu_guest_address_space_heap(space, HEAP, 4096) == 0);
    CHECK(ssbu_guest_address_space_heap(space, HEAP, 4096) == -1);
    CHECK(ssbu_guest_address_space_bind(space) == 0 && ssbu_ios_runtime_bind(runtime) == 0);
    CHECK(!memcmp(ssbu_guest_bound_pointer(HEAP, 16), seed, 16));
    CHECK(rejected(SSBU_HOST_MALLOC, &c)); /* Errno configuration never inherits. */
    CHECK(ssbu_guest_errno_bind(BASE + 2048) == 0 && ssbu_guest_errno_bind(HEAP) == -1);
    uint64_t location = 0; CHECK(ssbu_guest_errno_location(&location) == 0 && location == BASE + 2048);
    c.x[30] = BASE + 0x900; c.in_import = "heap-outer"; c.import_from = 42;
    CHECK(ssbu_ios_runtime_call(&c, BASE + 0x100) == 0);
    CHECK(c.x[12] == HEAP && c.x[13] == UINT64_C(0x123456789abcdef0) && c.x[14] == HEAP && c.x[15] == 0);
    CHECK(c.x[30] == BASE + 0x900 && !strcmp(c.in_import, "heap-outer") && c.import_from == 42);
    CHECK(rd32(BASE + 2048) == 123);
    uint64_t a = 0, b = 0, d = 0;
    CHECK(ssbu_guest_heap_allocate(1, 0, &a) == 0 && a == HEAP);
    CHECK(ssbu_guest_heap_allocate(17, 0, &b) == 0 && b == HEAP + 16);
    CHECK(ssbu_guest_heap_allocate(1, 0, &d) == 0 && d == HEAP + 48);
    CHECK(ssbu_guest_heap_free(b + 1) == -1 && ssbu_guest_heap_free(b) == 0 && ssbu_guest_heap_free(b) == -1);
    uint64_t e = 0, f = 0;
    CHECK(ssbu_guest_heap_allocate(16, 0, &e) == 0 && e == HEAP + 16);
    CHECK(ssbu_guest_heap_allocate(16, 0, &f) == 0 && f == HEAP + 32);
    CHECK(ssbu_guest_heap_free(a) == 0 && ssbu_guest_heap_free(d) == 0 &&
          ssbu_guest_heap_free(e) == 0 && ssbu_guest_heap_free(f) == 0);
    CHECK(ssbu_guest_heap_allocate(32, 0, &a) == 0 && ssbu_guest_heap_allocate(32, 0, &b) == 0);
    unsigned char pattern[32]; for (unsigned i = 0; i < 32; ++i) pattern[i] = (unsigned char)i;
    memcpy(ssbu_guest_bound_pointer(a, 32), pattern, 32);
    CHECK(ssbu_guest_heap_reallocate(a, 96, &d) == 0 && d == HEAP + 64 && ssbu_guest_heap_free(a) == -1);
    CHECK(!memcmp(ssbu_guest_bound_pointer(d, 32), pattern, 32));
    CHECK(ssbu_guest_heap_reallocate(d, 128, &e) == 0 && e == d);
    CHECK(ssbu_guest_heap_reallocate(d, 17, &e) == 0 && e == d);
    CHECK(ssbu_guest_heap_allocate(48, 0, &f) == 0 && f == HEAP + 96);
    CHECK(ssbu_guest_heap_free(b) == 0);
    CHECK(ssbu_guest_heap_reallocate(d, 48, &e) == 0 && e == HEAP); /* New block precedes old link. */
    CHECK(!memcmp(ssbu_guest_bound_pointer(e, 17), pattern, 17) && ssbu_guest_heap_free(d) == -1);
    CHECK(ssbu_guest_heap_free(e) == 0 && ssbu_guest_heap_free(f) == 0);
    CHECK(ssbu_guest_heap_allocate(64, 0, &a) == 0 && ssbu_guest_heap_allocate(4032, 0, &b) == 0);
    wr64(a, UINT64_C(0xfeedface)); d = 99;
    CHECK(ssbu_guest_heap_reallocate(a, 128, &d) == 1 && d == 99 && rd64(a) == UINT64_C(0xfeedface));
    c.x[0] = a; c.x[1] = SIZE_MAX;
    CHECK(ssbu_ios_host_service(SSBU_HOST_REALLOC, &c) == 0 && !c.x[0] && rd32(BASE + 2048) == SSBU_IOS_GUEST_ENOMEM);
    CHECK(rd64(a) == UINT64_C(0xfeedface));
    c.x[0] = a + 1; c.x[1] = 8;
    CHECK(rejected(SSBU_HOST_REALLOC, &c) && rejected(SSBU_HOST_FREE, &c));
    c.x[0] = a; c.x[1] = 0;
    CHECK(ssbu_ios_host_service(SSBU_HOST_REALLOC, &c) == 0 && !c.x[0] && ssbu_guest_heap_free(a) == -1);
    CHECK(ssbu_guest_heap_free(b) == 0);
    c.x[0] = UINT64_MAX; c.x[1] = 2;
    CHECK(ssbu_ios_host_service(SSBU_HOST_CALLOC, &c) == 0 && !c.x[0] && rd32(BASE + 2048) == SSBU_IOS_GUEST_ENOMEM);
    wr32(BASE + 2048, 123); c.x[0] = 8; c.x[1] = 8;
    CHECK(ssbu_ios_host_service(SSBU_HOST_CALLOC, &c) == 0 && c.x[0] == HEAP && rd32(BASE + 2048) == 123);
    for (unsigned i = 0; i < 64; ++i) CHECK(rd8(c.x[0] + i) == 0);
    CHECK(ssbu_ios_host_service(SSBU_HOST_FREE, &c) == 0 && c.x[0] == HEAP);
    CHECK(rejected(SSBU_HOST_FREE, &c));
    c.x[0] = 0; c.x[1] = UINT64_MAX;
    CHECK(ssbu_ios_host_service(SSBU_HOST_CALLOC, &c) == 0 && !c.x[0] && rd32(BASE + 2048) == 123);
    CHECK(ssbu_ios_host_service(SSBU_HOST_MALLOC, &c) == 0 && !c.x[0]);
    c.x[0] = 0; c.x[1] = 4096;
    CHECK(ssbu_ios_host_service(SSBU_HOST_REALLOC, &c) == 0 && c.x[0] == HEAP);
    CHECK(ssbu_ios_host_service(SSBU_HOST_FREE, &c) == 0);
    pool.space = space; CHECK(ssbu_guest_heap_allocate(64, 0, &pool.handoff) == 0);
    for (; started < 4; ++started) {
        jobs[started] = (Job){&pool, started};
        CHECK(pthread_create(&threads[started], NULL, heap_worker, &jobs[started]) == 0);
    }
    for (unsigned i = 0; i < started; ++i) pthread_join(threads[i], NULL);
    started = 0;
    CHECK(!pool.failed && rd32(BASE + 2048) == 123 && ssbu_guest_heap_free(pool.handoff) == -1);
    CHECK(ssbu_guest_heap_allocate(4096, 0, &a) == 0 && a == HEAP && ssbu_guest_heap_free(a) == 0);
    CHECK(ssbu_guest_address_space_close(&space) == -1);
    ssbu_ios_runtime_unbind(); ssbu_guest_memory_unbind();
    c.x[0] = 0; Ctx before; memcpy(&before, &c, sizeof c);
    CHECK(ssbu_ios_host_service(SSBU_HOST_FREE, &c) == 0 && !memcmp(&c, &before, sizeof c));
    CHECK(ssbu_guest_heap_allocate(1, 0, &a) == -1);
done:
    for (unsigned i = 0; i < started; ++i) pthread_join(threads[i], NULL);
    ssbu_ios_runtime_unbind(); ssbu_guest_memory_unbind();
    if (ssbu_ios_runtime_close(&runtime)) failed = 1;
    if (ssbu_guest_address_space_close(&space)) failed = 1;
    for (unsigned i = 0; i < 2; ++i) ssbu_guest_memory_close(&arenas[i]);
    pthread_mutex_destroy(&pool.mutex);
    return failed;
}
