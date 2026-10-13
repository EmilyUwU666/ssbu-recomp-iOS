/* Host-side CI companion. Excluded from the iOS app target. */
#include <stdio.h>
#include "Probe.h"
#include "GuestRuntime.h"

int main(int argc, char **argv) {
    /* Fatal-path modes are host-only; ProbeCli.c is excluded from the app. */
    if (argc == 2) {
        if (!strcmp(argv[1], "--trap-test")) guest_trap("original diagnostic fixture", 0x1234);
        else if (!strcmp(argv[1], "--unimpl-test")) guest_unimpl(0x2345, 0xffffffff, "original diagnostic fixture");
        else if (!strcmp(argv[1], "--svc-test")) guest_svc(NULL, 0x42);
        else return 2;
        return 99; /* A fatal handler returning normally is an error. */
    }
    int failures = ssbu_portability_selftest() | ssbu_guest_memory_selftest() | ssbu_translated_memory_selftest() | ssbu_relative_relocation_selftest() | ssbu_guest_elf_loader_selftest() | ssbu_guest_module_import_selftest() | ssbu_guest_address_space_selftest() | ssbu_guest_runtime_selftest() | ssbu_guest_host_services_selftest() | ssbu_guest_provider_preferences_selftest() | ssbu_guest_heap_selftest();
    if (failures) {
        fprintf(stderr, "Portability checks failed (bitmask 0x%x)\n", failures);
        return 1;
    }
    puts("PASS: runtime helpers, synthetic dispatch, and checked guest relocations, synthetic ELF loading, and static module imports, multi-arena worker lifetimes and generated call/import scopes, and host memory/string/timing services and explicit provider preferences pass");
    return 0;
}
