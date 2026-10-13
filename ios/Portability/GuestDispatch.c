/*
 * First iOS-native, no-dlopen guest-function dispatcher.
 * Validates the linked table and invokes the matching Ctx function.
 * Missing addresses are returned as errors; no executable memory is loaded.
 */
#include "GuestDispatch.h"

int ssbu_ios_dispatch_guest(Ctx *context, uint64_t guest_address) {
    if (!context || !guest_address || fn_table_count <= 0)
        return -1;

    int low = 0;
    int high = fn_table_count - 1;
    while (low <= high) {
        int middle = low + (high - low) / 2;
        const FnEntry *entry = &fn_table[middle];
        if (entry->addr == guest_address) {
            if (!entry->fn)
                return -1;
            context->fn = guest_address;
            entry->fn(context);
            return 0;
        }
        if (entry->addr < guest_address)
            low = middle + 1;
        else
            high = middle - 1;
    }
    return -1;
}
