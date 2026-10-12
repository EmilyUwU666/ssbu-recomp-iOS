#ifndef SSBU_IOS_PORTABILITY_PROBE_H
#define SSBU_IOS_PORTABILITY_PROBE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Returns a bitmask; zero means all checks passed. */
int ssbu_portability_selftest(void);

/* Tests the isolated, relocatable guest-memory mapping prototype. */
int ssbu_guest_memory_selftest(void);

/* Test runtime.h rd/wr/rdv/wrv helpers through a relocatable backing region. */
int ssbu_translated_memory_selftest(void);

/* Synthetic AArch64 RELATIVE relocation validation with overflow checks. */
int ssbu_relative_relocation_selftest(void);

/* Original, synthetic ELF64/AArch64 ET_DYN segment + relocation tests. */
int ssbu_guest_elf_loader_selftest(void);

/* Synthetic main -> import slot -> separate static provider and rejection checks. */
int ssbu_guest_module_import_selftest(void);

/* Owned multi-arena lookup, worker isolation and pinned mapping teardown. */
int ssbu_guest_address_space_selftest(void);

/* Generated call_indirect/host_import ABI and module-local nesting. */
int ssbu_guest_runtime_selftest(void);

/* Checked memory/timing ABI via optional static host-service imports. */
int ssbu_guest_host_services_selftest(void);

/* Explicit duplicate-export resolution and definition eligibility. */
int ssbu_guest_provider_preferences_selftest(void);

#ifdef __cplusplus
}
#endif

#endif
