#ifndef SSBU_IOS_GUEST_MEMORY_H
#define SSBU_IOS_GUEST_MEMORY_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t guest_base;
    size_t length;
    unsigned char *host;
} SSBUIOSGuestMemory;

/*
 * Prototype: reserve anonymous read/write host memory at an arbitrary address,
 * without MAP_FIXED or executable permissions. The opt-in translation bridge
 * wires runtime.h's typed accessors, not all of the upstream runtime's casts.
 */
int ssbu_guest_memory_open(SSBUIOSGuestMemory *memory, uint64_t guest_base, size_t length);
void ssbu_guest_memory_close(SSBUIOSGuestMemory *memory);
void *ssbu_guest_memory_translate(const SSBUIOSGuestMemory *memory, uint64_t guest_address, size_t length);
int ssbu_guest_memory_write(SSBUIOSGuestMemory *memory, uint64_t guest_address, const void *src, size_t length);
int ssbu_guest_memory_read(const SSBUIOSGuestMemory *memory, uint64_t guest_address, void *dst, size_t length);

/* Borrowed single-arena binding, local to this thread. Rejects replacing an
 * existing binding; unbind first. Caller keeps mapping alive until unbind.
 * Workers bind explicitly (no inheritance). Use GuestAddressSpace.h for
 * owned multi-arena lifetime/pinning.
 */
int ssbu_guest_memory_bind(const SSBUIOSGuestMemory *memory);
void ssbu_guest_memory_unbind(void);

#endif
