/*
 * Builds the actual generated-code helpers in runtime/runtime.h with iOS
 * guest-address translation enabled (the normal Windows translation is OFF).
 * This is synthetic input only; no Nintendo game code is present.
 */
#define SSBU_GUEST_ADDRESS_TRANSLATION 1
#include "../../runtime/runtime.h"

#include "GuestMemory.h"
#include "Probe.h"

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

    /* Reject invalid addresses using the checked API, avoiding a crash test. */
    if (ssbu_guest_memory_translate(&memory, base + 4092, 8) != NULL ||
        ssbu_guest_memory_translate(&memory, base - 4, 8) != NULL)
        failed |= 16384;

    ssbu_guest_memory_unbind();
    ssbu_guest_memory_close(&memory);
    return failed;
}
