/* Host-side CI companion. Excluded from the iOS app target. */
#include <stdio.h>
#include "Probe.h"

int main(void) {
    int failures = ssbu_portability_selftest() | ssbu_guest_memory_selftest() | ssbu_translated_memory_selftest() | ssbu_relative_relocation_selftest() | ssbu_guest_elf_loader_selftest() | ssbu_guest_module_import_selftest() | ssbu_guest_address_space_selftest() | ssbu_guest_runtime_selftest();
    if (failures) {
        fprintf(stderr, "Portability checks failed (bitmask 0x%x)\n", failures);
        return 1;
    }
    puts("PASS: runtime helpers, synthetic dispatch, and checked guest relocations, synthetic ELF loading, and static module imports, multi-arena worker lifetimes and generated call/import scopes pass");
    return 0;
}
