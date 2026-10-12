/*
 * Original, synthetic fixture only. Mimics the shape of lifted C functions,
 * with an arm64 guest context and typed memory helpers. NO Nintendo code.
 */
#include "runtime.h"

void ssbu_synthetic_lifted_fn(Ctx *c) {
    uint64_t guest_ptr = c->x[0];
    uint32_t before = rd32(guest_ptr);
    wr32(guest_ptr, before + 1u);
    c->x[0] = before + 1u;
}
