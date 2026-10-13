#ifndef SSBU_IOS_GUEST_HEAP_H
#define SSBU_IOS_GUEST_HEAP_H
#include "GuestAddressSpace.h"

/* Reserve ONE explicit, nonzero, 16-byte-aligned heap interval inside a single
 * owned arena. Length must be a positive multiple of 16. Configure once before
 * binding any workers. Caller reserves this interval exclusively for the heap,
 * separate from module data, stacks, TLS and guest errno cells. No guest bytes
 * are changed by configuration. Heap state/mapping lifetime belongs to space. */
int ssbu_guest_address_space_heap(SSBUIOSGuestAddressSpace *space,
                                  uint64_t address, size_t length);

/* Uses this worker's bound owned space. Allocations are shared across workers,
 * aligned to 16 bytes, and tracked by exact guest base with host-only metadata.
 * Return 0 on success, 1 on exhaustion/size overflow, -1 on invalid setup or
 * pointer. Result outputs remain unchanged on failure. Zero-sized allocation
 * returns zero; realloc(p,0) frees p and returns zero. Realloc failure preserves
 * the old allocation/data. Free(NULL) is a no-op, including outside a binding.
 * The allocator mutex protects metadata and its own copy/zero operations only;
 * callers synchronize concurrent accesses to their allocated guest data.
 * Arena translation is not allocation-level/use-after-free protection. */
int ssbu_guest_heap_allocate(size_t size, int clear, uint64_t *address);
int ssbu_guest_heap_reallocate(uint64_t old_address, size_t size, uint64_t *address);
int ssbu_guest_heap_free(uint64_t address);
#endif
