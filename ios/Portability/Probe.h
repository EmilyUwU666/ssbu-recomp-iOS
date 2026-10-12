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

#ifdef __cplusplus
}
#endif

#endif
