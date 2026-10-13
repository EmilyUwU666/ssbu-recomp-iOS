#ifndef SSBU_IOS_GUEST_ADDRESS_SPACE_H
#define SSBU_IOS_GUEST_ADDRESS_SPACE_H

#include "GuestMemory.h"
typedef struct SSBUIOSGuestAddressSpace SSBUIOSGuestAddressSpace;

/* Validate and adopt 1..256 distinct mappings, sorted internally by guest VA.
 * On success ownership MOVES: every input descriptor is cleared. No arena
 * mappings/descriptors may be aliased or used after transfer. On error inputs
 * and *result remain unchanged (initially *result=NULL). Rejects guest/host
 * overlap, empty regions and address overflow. */
int ssbu_guest_address_space_create(SSBUIOSGuestAddressSpace **result,
                                    SSBUIOSGuestMemory *arenas, size_t count);

/* Each worker binds explicitly; per-thread bindings do not inherit. Rejects
 * an already-bound thread. Pins owned mappings until memory_unbind or thread
 * exit (pthread-key destructor). Caller keeps handle alive during bind attempt. */
int ssbu_guest_address_space_bind(SSBUIOSGuestAddressSpace *space);

/* Returns -1 while any thread is bound, leaving handle/mappings unchanged.
 * Otherwise releases mappings and clears *space (NULL is idempotent). Owner
 * must stop new bind attempts before destruction and synchronize access to
 * the owning handle. This is not lock-free publication/reclamation. */
int ssbu_guest_address_space_close(SSBUIOSGuestAddressSpace **space);

/* Checked lookup in THIS THREAD's binding. Entire access must fit one arena;
 * crossing even adjacent arenas, gaps or overflow fails. Returned pointer is
 * borrowed only until unbind; never share/cache it beyond that scope. Returns
 * NULL for a bad address; runtime's host_pointer wrapper remains fail-fast. */
void *ssbu_guest_bound_pointer(uint64_t guest_address, size_t length);

/* Nonempty remainder of a single bound arena, with the same borrowed lifetime.
 * Rejects one-past-end and leaves outputs unchanged on failure. */
int ssbu_guest_bound_span(uint64_t address, unsigned char **host, size_t *length);

/* Attach a nonzero, 4-byte-aligned guest errno cell to this worker's current
 * memory binding. The cell must fit an arena; failure preserves the previous
 * location. No initialization/allocation occurs. Unbind/thread exit discards
 * the location; other workers do not inherit it. Callers provide distinct cells
 * or synchronize shared cells. Getter leaves output unchanged on failure. */
int ssbu_guest_errno_bind(uint64_t address);
int ssbu_guest_errno_location(uint64_t *address);

/* Balanced scopes for host services retaining mapped pointers across normal
 * guest callbacks. Memory unbind attempts inside any scope are ignored; new
 * binds already reject replacement. Owned mappings remain pinned. Legacy
 * callers still own the backing mapping and must not close it directly.
 * Non-local exits/thread termination within callbacks are unsupported. */
int ssbu_guest_memory_guard_begin(void);
void ssbu_guest_memory_guard_end(void);

#endif
