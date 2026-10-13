/* Per-worker translation. Owned spaces keep their arenas alive while bound;
 * legacy single-arena bindings remain explicitly borrowed. */
#include "GuestAddressSpace.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

struct SSBUIOSGuestAddressSpace {
    SSBUIOSGuestMemory *regions;
    size_t count, bindings;
    pthread_mutex_t mutex;
};
typedef struct {
    SSBUIOSGuestAddressSpace *space;
    SSBUIOSGuestMemory borrowed;
    uint64_t errno_address;
} ThreadBinding;

static pthread_key_t binding_key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;
static int key_error = -1;

static void release_binding(void *opaque) {
    ThreadBinding *binding = opaque;
    if (!binding) return;
    if (binding->space) {
        pthread_mutex_lock(&binding->space->mutex);
        --binding->space->bindings;
        pthread_mutex_unlock(&binding->space->mutex);
    }
    free(binding);
}
static void create_key(void) {
    key_error = pthread_key_create(&binding_key, release_binding);
}
static int key_ready(void) {
    return pthread_once(&key_once, create_key) || key_error;
}
static int compare_regions(const void *a, const void *b) {
    uint64_t x = ((const SSBUIOSGuestMemory *)a)->guest_base;
    uint64_t y = ((const SSBUIOSGuestMemory *)b)->guest_base;
    return (x > y) - (x < y);
}
static int valid_region(const SSBUIOSGuestMemory *region) {
    return region && region->host && region->length &&
        (uint64_t)region->length <= UINT64_MAX - region->guest_base &&
        region->length <= UINTPTR_MAX - (uintptr_t)region->host;
}

int ssbu_guest_address_space_create(SSBUIOSGuestAddressSpace **out,
                                    SSBUIOSGuestMemory *arenas, size_t count) {
    if (!out || *out || !arenas || !count || count > 256) return -1;
    for (size_t i = 0; i < count; ++i) {
        if (!valid_region(&arenas[i])) return -1;
        for (size_t j = 0; j < i; ++j) {
            uintptr_t a = (uintptr_t)arenas[i].host, b = (uintptr_t)arenas[j].host;
            if (a < b + arenas[j].length && b < a + arenas[i].length) return -1;
        }
    }
    SSBUIOSGuestAddressSpace *space = calloc(1, sizeof *space);
    if (!space) return -1;
    space->regions = calloc(count, sizeof *space->regions);
    if (!space->regions) { free(space); return -1; }
    for (size_t i = 0; i < count; ++i) space->regions[i] = arenas[i];
    space->count = count;
    qsort(space->regions, count, sizeof *space->regions, compare_regions);
    for (size_t i = 1; i < count; ++i) {
        const SSBUIOSGuestMemory *a = &space->regions[i-1], *b = &space->regions[i];
        if (a->guest_base + a->length > b->guest_base) {
            free(space->regions); free(space); return -1;
        }
    }
    if (pthread_mutex_init(&space->mutex, NULL)) {
        free(space->regions); free(space); return -1;
    }
    for (size_t i = 0; i < count; ++i) arenas[i] = (SSBUIOSGuestMemory){0};
    *out = space;
    return 0;
}

int ssbu_guest_address_space_bind(SSBUIOSGuestAddressSpace *space) {
    if (!space || key_ready() || pthread_getspecific(binding_key)) return -1;
    ThreadBinding *binding = calloc(1, sizeof *binding);
    if (!binding) return -1;
    pthread_mutex_lock(&space->mutex);
    if (space->bindings == SIZE_MAX) {
        pthread_mutex_unlock(&space->mutex); free(binding); return -1;
    }
    ++space->bindings;
    pthread_mutex_unlock(&space->mutex);
    binding->space = space;
    if (pthread_setspecific(binding_key, binding)) {
        release_binding(binding); return -1;
    }
    return 0;
}

int ssbu_guest_address_space_close(SSBUIOSGuestAddressSpace **owner) {
    if (!owner) return -1;
    SSBUIOSGuestAddressSpace *space = *owner;
    if (!space) return 0;
    pthread_mutex_lock(&space->mutex);
    if (space->bindings) { pthread_mutex_unlock(&space->mutex); return -1; }
    pthread_mutex_unlock(&space->mutex);
    pthread_mutex_destroy(&space->mutex);
    for (size_t i = 0; i < space->count; ++i)
        ssbu_guest_memory_close(&space->regions[i]);
    free(space->regions); free(space); *owner = NULL;
    return 0;
}

int ssbu_guest_memory_bind(const SSBUIOSGuestMemory *memory) {
    if (!valid_region(memory) || key_ready() || pthread_getspecific(binding_key))
        return -1;
    ThreadBinding *binding = calloc(1, sizeof *binding);
    if (!binding) return -1;
    binding->borrowed = *memory;
    if (pthread_setspecific(binding_key, binding)) { free(binding); return -1; }
    return 0;
}

void ssbu_guest_memory_unbind(void) {
    if (key_ready()) return;
    ThreadBinding *binding = pthread_getspecific(binding_key);
    if (!binding) return;
    if (!pthread_setspecific(binding_key, NULL)) release_binding(binding);
}

static const SSBUIOSGuestMemory *bound_region(uint64_t address) {
    if (key_ready()) return NULL;
    const ThreadBinding *binding = pthread_getspecific(binding_key);
    if (!binding) return NULL;
    if (!binding->space)
        return &binding->borrowed;
    const SSBUIOSGuestAddressSpace *space = binding->space;
    /* Immutable table, pinned by this thread; find last region start <= VA. */
    size_t low = 0, high = space->count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (space->regions[mid].guest_base <= address) low = mid + 1;
        else high = mid;
    }
    if (!low) return NULL;
    return &space->regions[low-1];
}

void *ssbu_guest_bound_pointer(uint64_t address, size_t length) {
    return ssbu_guest_memory_translate(bound_region(address), address, length);
}

int ssbu_guest_bound_span(uint64_t address, unsigned char **host, size_t *length) {
    if (!host || !length) return -1;
    const SSBUIOSGuestMemory *region = bound_region(address);
    unsigned char *pointer = ssbu_guest_memory_translate(region, address, 1);
    if (!pointer) return -1;
    *host = pointer;
    *length = region->length - (size_t)(address - region->guest_base);
    return 0;
}

int ssbu_guest_errno_bind(uint64_t address) {
    if (!address || (address & 3) || !ssbu_guest_bound_pointer(address, 4)) return -1;
    ThreadBinding *binding = pthread_getspecific(binding_key);
    binding->errno_address = address;
    return 0;
}

int ssbu_guest_errno_location(uint64_t *address) {
    if (!address || key_ready()) return -1;
    const ThreadBinding *binding = pthread_getspecific(binding_key);
    if (!binding || !binding->errno_address) return -1;
    *address = binding->errno_address;
    return 0;
}

void *ssbu_guest_host_pointer(uint64_t address, size_t length) {
    void *host = ssbu_guest_bound_pointer(address, length);
    if (!host) {
        fprintf(stderr, "SSBU guest address 0x%llx out of bounds (length %zu)\n",
                (unsigned long long)address, length);
        abort();
    }
    return host;
}
