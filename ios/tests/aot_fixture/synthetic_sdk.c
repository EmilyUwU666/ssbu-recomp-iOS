/* Original provider module, separately statically linked into the iOS app. */
#define SSBU_GUEST_ADDRESS_TRANSLATION 1
#include "synthetic_sdk.h"

static void synthetic_sdk_add_seven(Ctx *context) {
    context->x[0] += 7;
}

/* Exercise imported data in the provider arena and output in the caller arena. */
static void synthetic_sdk_update_data(Ctx *context) {
    uint32_t value = rd32(context->x[2]) + (uint32_t)context->x[0];
    wr32(context->x[2], value);
    wr32(context->x[3], value * 2);
    context->x[0] = value;
}

/* Module-local offsets are rebased by GuestModules, not host code addresses. */
const FnEntry fn_table_synthetic_sdk[] = {
    {0x100, synthetic_sdk_add_seven}, {0x200, synthetic_sdk_update_data}
};
const size_t fn_table_synthetic_sdk_count = 2;
