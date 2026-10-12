/*
 * First portable integration checkpoint.
 * Compile upstream runtime/runtime.h using the host (including iOS ARM64),
 * and run deterministic checks on helper functions used by the lifter.
 * No generated game code, keys, or game assets are embedded here.
 */
#include "Probe.h"
#include "../../runtime/runtime.h"

int ssbu_portability_selftest(void) {
    int failures = 0;
    Ctx ctx = {0};

    if (sizeof(V128) != 16 || sizeof(ctx.v) != 32 * 16)
        failures |= 1;

    if (adds64(&ctx, UINT64_MAX, 1, 0) != 0 ||
        ctx.C != 1 || ctx.Z != 1 || ctx.N != 0 || ctx.V != 0)
        failures |= 2;

    if (ror32(0x12345678U, 8) != 0x78123456U ||
        bswap32(0x12345678U) != 0x78563412U)
        failures |= 4;

    uint64_t slot = 0;
    wr32((uint64_t)(uintptr_t)&slot, 0x12345678U);
    if (rd32((uint64_t)(uintptr_t)&slot) != 0x12345678U)
        failures |= 8;

    V128 v = {{0}};
    vset(&v, 2, 1, 0xAABBCCDDU);
    if (vget(&v, 2, 1) != 0xAABBCCDDU)
        failures |= 16;

    if (sdiv32((uint32_t)-7, 3) != (uint32_t)-2 ||
        umulh(UINT64_MAX, 2) != 1)
        failures |= 32;

    if (f2u32(-1.0) != 0 || f2u32(9.9) != 9)
        failures |= 64;

    return failures;
}
