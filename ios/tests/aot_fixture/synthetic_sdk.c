/* Original provider module, separately statically linked into the iOS app. */
#include "synthetic_sdk.h"

static void synthetic_sdk_add_seven(Ctx *context) {
    context->x[0] += 7;
}

/* Module-local offsets are rebased by GuestModules, not host code addresses. */
const FnEntry fn_table_synthetic_sdk[] = {{0x100, synthetic_sdk_add_seven}};
const size_t fn_table_synthetic_sdk_count = 1;
