/* Original multi-arena and pthread lifecycle tests. No game code/data. */
#define SSBU_GUEST_ADDRESS_TRANSLATION 1
#include "../../runtime/runtime.h"
#include "GuestAddressSpace.h"
#include "GuestHostServices.h"
#include "Probe.h"

#include <pthread.h>
#include <stdio.h>

#define BASE UINT64_C(0x7300000000)
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "Address-space check failed at line %d: %s\n", __LINE__, #x); \
    failed = 1; goto done; } } while (0)

typedef struct {
    SSBUIOSGuestAddressSpace *space;
    uint32_t expected;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int ready, release, failed;
} Worker;

static void *worker(void *opaque) {
    Worker *w = opaque;
    uint64_t location = 99;
    /* Binding the parent's thread never binds a newly created worker. */
    if (ssbu_guest_errno_location(&location) != -1 || location != 99 ||
        ssbu_guest_bound_pointer(BASE + 128, 4) != NULL ||
        ssbu_guest_address_space_bind(w->space)) {
        w->failed = 1;
    } else {
        if (rd32(BASE + 128) != w->expected) w->failed = 1;
        wr32(BASE + 128, w->expected + 1);
        if (ssbu_guest_errno_location(&location) != -1 ||
            ssbu_guest_errno_bind(BASE + 512) ||
            ssbu_guest_errno_location(&location) || location != BASE + 512) w->failed = 1;
        wr32(BASE + 512, w->expected);
        if (ssbu_guest_address_space_bind(w->space) != -1) w->failed = 1;
    }
    pthread_mutex_lock(&w->mutex);
    w->ready = 1;
    pthread_cond_signal(&w->cond);
    while (!w->release) pthread_cond_wait(&w->cond, &w->mutex);
    pthread_mutex_unlock(&w->mutex);
    /* Intentionally no unbind: the pthread-key destructor releases the pin. */
    return NULL;
}

static void release_worker(Worker *w, pthread_t thread, int started) {
    if (!started) return;
    pthread_mutex_lock(&w->mutex);
    w->release = 1;
    pthread_cond_signal(&w->cond);
    pthread_mutex_unlock(&w->mutex);
    pthread_join(thread, NULL);
}

int ssbu_guest_address_space_selftest(void) {
    SSBUIOSGuestMemory arenas[3] = {{0}};
    SSBUIOSGuestAddressSpace *space = NULL, *other = NULL;
    Worker a = {.expected = 11, .mutex = PTHREAD_MUTEX_INITIALIZER,
                .cond = PTHREAD_COND_INITIALIZER};
    Worker b = {.expected = 21, .mutex = PTHREAD_MUTEX_INITIALIZER,
                .cond = PTHREAD_COND_INITIALIZER};
    pthread_t ta = {0}, tb = {0};
    int started_a = 0, started_b = 0, failed = 0;
    CHECK(ssbu_guest_bound_pointer(BASE, 4) == NULL);
    CHECK(ssbu_guest_memory_open(&arenas[0], BASE + 0x3000, 4096) == 0);
    CHECK(ssbu_guest_memory_open(&arenas[1], BASE, 4096) == 0);
    CHECK(ssbu_guest_memory_open(&arenas[2], BASE, 4096) == 0);
    const uint32_t ten = 10, eleven = 11, twenty_one = 21;
    CHECK(ssbu_guest_memory_write(&arenas[1], BASE, &ten, 4) == 0);
    CHECK(ssbu_guest_memory_write(&arenas[1], BASE + 128, &eleven, 4) == 0);
    CHECK(ssbu_guest_memory_write(&arenas[2], BASE + 128, &twenty_one, 4) == 0);
    /* Overlap errors do not steal or close any input mapping. */
    SSBUIOSGuestMemory invalid[] = {arenas[1], arenas[2]};
    CHECK(ssbu_guest_address_space_create(&space, invalid, 2) == -1 && !space &&
          invalid[0].host == arenas[1].host && invalid[1].host == arenas[2].host);
    invalid[1] = arenas[1]; invalid[1].guest_base = BASE + 0x3000;
    CHECK(ssbu_guest_address_space_create(&space, invalid, 2) == -1 && !space);
    invalid[1] = arenas[2]; invalid[1].guest_base = UINT64_MAX - 1;
    CHECK(ssbu_guest_address_space_create(&space, invalid, 2) == -1 && !space);
    invalid[1] = arenas[2]; invalid[1].length = 0;
    CHECK(ssbu_guest_address_space_create(&space, invalid, 2) == -1 && !space);
    CHECK(ssbu_guest_address_space_create(&space, arenas, 0) == -1 && !space);
    CHECK(ssbu_guest_address_space_create(&space, arenas, 257) == -1 && !space);
    /* Input order is deliberately descending; success moves ownership. */
    CHECK(ssbu_guest_address_space_create(&space, arenas, 2) == 0 && space &&
          !arenas[0].host && !arenas[1].host);
    CHECK(ssbu_guest_address_space_create(&other, &arenas[2], 1) == 0 &&
          !arenas[2].host);
    CHECK(ssbu_guest_address_space_bind(space) == 0);
    CHECK(ssbu_guest_errno_bind(BASE + 516) == 0);
    wr32(BASE + 516, 111);
    CHECK(rd32(BASE) == 10);
    wr32(BASE + 0x3000, 42);
    CHECK(rd32(BASE + 0x3000) == 42);
    CHECK(ssbu_guest_bound_pointer(BASE - 1, 1) == NULL);
    CHECK(ssbu_guest_bound_pointer(BASE + 4093, 8) == NULL);
    CHECK(ssbu_guest_bound_pointer(BASE + 0x2000, 1) == NULL);
    CHECK(ssbu_guest_bound_pointer(UINT64_MAX, 8) == NULL);
    CHECK(ssbu_guest_bound_pointer(BASE, SIZE_MAX) == NULL);
    CHECK(ssbu_guest_address_space_bind(other) == -1 && rd32(BASE) == 10);
    CHECK(ssbu_guest_address_space_close(&space) == -1 && space);
    a.space = space; b.space = other;
    CHECK(pthread_create(&ta, NULL, worker, &a) == 0); started_a = 1;
    CHECK(pthread_create(&tb, NULL, worker, &b) == 0); started_b = 1;
    pthread_mutex_lock(&a.mutex);
    while (!a.ready) pthread_cond_wait(&a.cond, &a.mutex);
    int a_failed = a.failed;
    pthread_mutex_unlock(&a.mutex);
    pthread_mutex_lock(&b.mutex);
    while (!b.ready) pthread_cond_wait(&b.cond, &b.mutex);
    int b_failed = b.failed;
    pthread_mutex_unlock(&b.mutex);
    CHECK(!a_failed && !b_failed);
    uint64_t parent_errno = 0;
    CHECK(ssbu_guest_errno_location(&parent_errno) == 0 && parent_errno == BASE + 516 &&
          rd32(parent_errno) == 111 && rd32(BASE + 512) == a.expected);
    CHECK(rd32(BASE) == 10 && rd32(BASE + 128) == 12);
    ssbu_guest_memory_unbind();
    CHECK(ssbu_guest_bound_pointer(BASE, 4) == NULL);
    /* Even after the owner unbinds, both worker scopes keep arenas alive. */
    CHECK(ssbu_guest_address_space_close(&space) == -1 && space);
    CHECK(ssbu_guest_address_space_close(&other) == -1 && other);
    release_worker(&a, ta, started_a); started_a = 0;
    release_worker(&b, tb, started_b); started_b = 0;
    CHECK(ssbu_guest_address_space_bind(other) == 0);
    CHECK(ssbu_guest_errno_location(&parent_errno) == -1 && rd32(BASE + 512) == b.expected);
    CHECK(rd32(BASE + 128) == 22); /* worker wrote the other space, same VA */
    ssbu_guest_memory_unbind();
    CHECK(ssbu_guest_address_space_close(&space) == 0 && !space);
    CHECK(ssbu_guest_address_space_close(&other) == 0 && !other);
    CHECK(ssbu_guest_address_space_close(&space) == 0);

    /* Adjacent arenas cannot turn one typed access into a split store. */
    CHECK(ssbu_guest_memory_open(&arenas[0], BASE, 4096) == 0);
    CHECK(ssbu_guest_memory_open(&arenas[1], BASE + 4096, 4096) == 0);
    CHECK(ssbu_guest_address_space_create(&space, arenas, 2) == 0);
    CHECK(ssbu_guest_address_space_bind(space) == 0);
    CHECK(ssbu_guest_bound_pointer(BASE + 4092, 8) == NULL);
    unsigned char *span;
    size_t remaining;
    CHECK(ssbu_guest_bound_span(BASE + 4095, &span, &remaining) == 0 && remaining == 1);
    wr8(BASE + 4095, 'x');
    Ctx string_context = {0}; string_context.x[0] = BASE + 4095;
    CHECK(ssbu_ios_host_service(SSBU_HOST_STRLEN, &string_context) == -1 &&
          string_context.x[0] == BASE + 4095);
    string_context.x[1] = BASE + 1024;
    CHECK(ssbu_ios_host_service(SSBU_HOST_STRSTR, &string_context) == -1);
    string_context.x[0] = BASE + 1024; string_context.x[1] = BASE + 4095;
    string_context.x[2] = 2;
    CHECK(ssbu_ios_host_service(SSBU_HOST_STRNCAT, &string_context) == -1 && rd8(BASE + 1024) == 0);
    string_context.x[2] = 1;
    CHECK(ssbu_ios_host_service(SSBU_HOST_STRNCAT, &string_context) == 0 &&
          rd8(BASE + 1024) == 'x' && rd8(BASE + 1025) == 0);
    CHECK(ssbu_guest_bound_span(BASE + 4096, &span, &remaining) == 0 && remaining == 4096);
    wr32(BASE + 4096, 99);
    CHECK(rd32(BASE + 4096) == 99);
    ssbu_guest_memory_unbind();
    CHECK(ssbu_guest_address_space_close(&space) == 0);
    /* Legacy borrowed single mapping also refuses implicit replacement. */
    CHECK(ssbu_guest_memory_open(&arenas[0], BASE, 4096) == 0);
    CHECK(ssbu_guest_memory_bind(&arenas[0]) == 0);
    CHECK(ssbu_guest_errno_bind(BASE + 512) == 0);
    CHECK(ssbu_guest_errno_location(&parent_errno) == 0 && parent_errno == BASE + 512);
    CHECK(ssbu_guest_bound_span(BASE + 4095, &span, &remaining) == 0 && remaining == 1);
    CHECK(ssbu_guest_memory_bind(&arenas[0]) == -1);
done:
    ssbu_guest_memory_unbind();
    release_worker(&a, ta, started_a);
    release_worker(&b, tb, started_b);
    if (ssbu_guest_address_space_close(&space)) failed = 1;
    if (ssbu_guest_address_space_close(&other)) failed = 1;
    for (size_t i = 0; i < 3; ++i) ssbu_guest_memory_close(&arenas[i]);
    pthread_cond_destroy(&a.cond); pthread_cond_destroy(&b.cond);
    pthread_mutex_destroy(&a.mutex); pthread_mutex_destroy(&b.mutex);
    return failed;
}
