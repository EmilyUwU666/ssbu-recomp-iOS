/*
 * Builds the actual generated-code helpers in runtime/runtime.h with iOS
 * guest-address translation enabled (the normal Windows translation is OFF).
 * This is synthetic input only; no Nintendo game code is present.
 */
#define SSBU_GUEST_ADDRESS_TRANSLATION 1
#include "../../runtime/runtime.h"

#include "GuestMemory.h"
#include "Probe.h"
#include "GuestDispatch.h"
#include "../tests/aot_fixture/SyntheticAddresses.h"

/* Reference diagnostics for synthetic LDXR/STXR tests.
 * The complete runtime supplies this symbol in runtime/hostrt.c. */
static unsigned g_stx_diagnostics = 0;
void stx_diag(Ctx *ctx, uint64_t address, uint64_t expected, uint64_t found, uint64_t value, int bits) {
    (void)ctx; (void)address; (void)expected; (void)found; (void)value; (void)bits;
    ++g_stx_diagnostics;
}

int ssbu_translated_memory_selftest(void) {
    const uint64_t base = UINT64_C(0x7100000000);
    SSBUIOSGuestMemory memory = {0};
    int failed = 0;

    if (ssbu_guest_memory_open(&memory, base, 4096))
        return 2048;

    if (ssbu_guest_memory_bind(&memory)) {
        ssbu_guest_memory_close(&memory);
        return 2048;
    }

    wr64(base + 64, UINT64_C(0x123456789ABCDEF0));
    if (rd64(base + 64) != UINT64_C(0x123456789ABCDEF0))
        failed |= 2048;

    /* Ensure the host-backed storage, NOT the guest's virtual address,
       actually receives the translated store. */
    uint64_t back = 0;
    if (ssbu_guest_memory_read(&memory, base + 64, &back, sizeof back) ||
        back != UINT64_C(0x123456789ABCDEF0))
        failed |= 2048;

    wr8(base + 128, UINT8_C(0xAB));
    wr16(base + 130, UINT16_C(0xBEEF));
    wr32(base + 136, UINT32_C(0xABCDEF12));
    if (rd8(base + 128) != 0xAB ||
        rd16(base + 130) != 0xBEEF ||
        rd32(base + 136) != UINT32_C(0xABCDEF12))
        failed |= 4096;

    V128 sent = {{0}};
    sent.d[0] = UINT64_C(0x1122334455667788);
    sent.d[1] = UINT64_C(0x99AABBCCDDEEFF00);
    wrv(base + 256, &sent, sizeof sent);
    V128 received = rdv(base + 256, sizeof sent);
    if (received.d[0] != sent.d[0] || received.d[1] != sent.d[1])
        failed |= 8192;

    /* Exercise the advanced SIMD interleaved structure loads and stores.
       The helpers in runtime/neon.h historically also cast raw guest pointers. */
    Ctx simd = {0};
    simd.v[0].b[0] = 0x12;
    simd.v[1].b[0] = 0x34;
    v_stn(&simd, base + 512, 0, 2, 0, 0);
    if (rd8(base + 512) != 0x12 || rd8(base + 513) != 0x34)
        failed |= 32768;
    wr8(base + 512, 0x56);
    wr8(base + 513, 0x78);
    v_ldn(&simd, base + 512, 2, 2, 0, 0);
    if (simd.v[2].b[0] != 0x56 || simd.v[3].b[0] != 0x78)
        failed |= 32768;

    /* Verify the guest-address-to-host-pointer bridge covers atomic STXR. */
    Ctx atomic = {0};
    atomic.ex_addr = base + 768;
    atomic.ex_val = UINT64_C(10);
    wr64(base + 768, UINT64_C(10));
    if (stx64(&atomic, base + 768, UINT64_C(11)) != 0 ||
        rd64(base + 768) != 11 || g_stx_diagnostics != 0)
        failed |= 65536;

    /* The fixture comes from a separate, statically linked target in Xcode.
       Dispatch an AArch64-style Ctx to synthetic recompiled-style functions.
       A successful return requires the fixture's real function table and a
       guest virtual address correctly translated through the memory binder. */
    Ctx lifted = {0};
    lifted.x[0] = base + 1024;
    wr32(base + 1024, 20);
    if (ssbu_ios_dispatch_guest(&lifted, SSBU_SYNTHETIC_INC_ADDR) != 0 ||
        rd32(base + 1024) != 21 || lifted.x[0] != 21 ||
        lifted.fn != SSBU_SYNTHETIC_INC_ADDR)
        failed |= 131072;

    lifted.x[0] = base + 1024;
    if (ssbu_ios_dispatch_guest(&lifted, SSBU_SYNTHETIC_DOUBLE_ADDR) != 0 ||
        rd32(base + 1024) != 42 || lifted.x[0] != 42 ||
        lifted.fn != SSBU_SYNTHETIC_DOUBLE_ADDR)
        failed |= 262144;

    lifted.x[0] = 7;
    if (ssbu_ios_dispatch_guest(&lifted, base + 0x1234567) != -1 ||
        lifted.x[0] != 7)
        failed |= 524288;

    /* Reject invalid addresses using the checked API, avoiding a crash test. */
    if (ssbu_guest_memory_translate(&memory, base + 4092, 8) != NULL ||
        ssbu_guest_memory_translate(&memory, base - 4, 8) != NULL)
        failed |= 16384;

    ssbu_guest_memory_unbind();
    ssbu_guest_memory_close(&memory);
    return failed;
}
