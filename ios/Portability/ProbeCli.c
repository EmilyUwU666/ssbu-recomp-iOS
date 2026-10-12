/* Host-side CI companion. Excluded from the iOS app target. */
#include <stdio.h>
#include "Probe.h"

int main(void) {
    int failures = ssbu_portability_selftest() | ssbu_guest_memory_selftest();
    if (failures) {
        fprintf(stderr, "Portability checks failed (bitmask 0x%x)\n", failures);
        return 1;
    }
    puts("PASS: runtime helpers and relocatable guest-memory checks pass");
    return 0;
}
