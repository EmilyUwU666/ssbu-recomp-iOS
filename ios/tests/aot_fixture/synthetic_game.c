/*
 * Original, synthetic fixture only. Mimics the shape of lifted C functions,
 * with an arm64 guest context and typed memory helpers. NO Nintendo code.
 */
#define SSBU_GUEST_ADDRESS_TRANSLATION 1
#include "runtime.h"
#include "SyntheticAddresses.h"

void ssbu_synthetic_lifted_fn(Ctx *c) {
    uint64_t guest_ptr = c->x[0];
    uint32_t before = rd32(guest_ptr);
    wr32(guest_ptr, before + 1u);
    c->x[0] = before + 1u;
}

/* Second lifted-style function to exercise the static dispatch table. */
void ssbu_synthetic_lifted_double(Ctx *c) {
    uint64_t guest_ptr = c->x[0];
    uint32_t before = rd32(guest_ptr);
    wr32(guest_ptr, before * 2u);
    c->x[0] = before * 2u;
}

/* Statically linked function table, mimicking recomp_eh.py's generated table.
 * This module is ORIGINAL test code and not based on Nintendo binaries.
 */
const FnEntry fn_table[] = {
    {SSBU_SYNTHETIC_INC_ADDR, ssbu_synthetic_lifted_fn},
    {SSBU_SYNTHETIC_DOUBLE_ADDR, ssbu_synthetic_lifted_double},
};
const int fn_table_count = (int)(sizeof fn_table / sizeof fn_table[0]);
