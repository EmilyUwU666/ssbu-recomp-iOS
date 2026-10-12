/*
 * Opt-in backing region for generated-game memory accessors in runtime/runtime.h.
 * Not thread/lifetime-safe against closing a mapping while guest workers run.
 * Only synthetic testing uses this today. Never reserves guest addresses with MAP_FIXED.
 */
#include "GuestMemory.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static _Atomic(const SSBUIOSGuestMemory *) g_memory = ATOMIC_VAR_INIT(NULL);

int ssbu_guest_memory_bind(const SSBUIOSGuestMemory *memory) {
    if (!memory || !memory->host || !memory->length)
        return -1;
    atomic_store_explicit(&g_memory, memory, memory_order_release);
    return 0;
}

void ssbu_guest_memory_unbind(void) {
    atomic_store_explicit(&g_memory, NULL, memory_order_release);
}

void *ssbu_guest_host_pointer(uint64_t guest_addr, size_t length) {
    const SSBUIOSGuestMemory *memory = atomic_load_explicit(&g_memory, memory_order_acquire);
    void *host = ssbu_guest_memory_translate(memory, guest_addr, length);
    if (!host) {
        fprintf(stderr, "SSBU guest address 0x%llx out of bounds (length %zu)\n",
                (unsigned long long)guest_addr, length);
        abort();
    }
    return host;
}
