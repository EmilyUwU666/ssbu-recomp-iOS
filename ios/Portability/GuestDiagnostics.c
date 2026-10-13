/* Explicit iOS stop diagnostics for generated traps/untranslated instructions.
 * These do not pretend to implement kernel SVC or guest exception unwinding. */
#include "../../runtime/runtime.h"
#include <stdio.h>
#include <stdlib.h>

void guest_trap(const char *what, uint64_t address) {
    fprintf(stderr, "SSBU GUEST TRAP: %s (0x%llx)\n", what ? what : "unknown",
            (unsigned long long)address);
    abort();
}
void guest_unimpl(uint64_t pc, uint32_t instruction, const char *what) {
    fprintf(stderr, "SSBU UNTRANSLATED: 0x%llx instruction %08x (%s)\n",
            (unsigned long long)pc, instruction, what ? what : "unknown");
    abort();
}
void guest_svc(Ctx *context, uint32_t number) {
    (void)context;
    guest_trap("unsupported direct kernel SVC", number);
}
