/* Entirely original ELF metadata and AOT functions; no game-derived content. */
#define SSBU_GUEST_ADDRESS_TRANSLATION 1
#include "GuestELFLoader.h"
#include "GuestModules.h"
#include "GuestAddressSpace.h"
#include "GuestRuntime.h"
#include "Probe.h"
#include "../tests/aot_fixture/synthetic_sdk.h"

#include <stdio.h>
#include <string.h>

#define MAIN_BIAS UINT64_C(0x7100000000)
#define SDK_BIAS UINT64_C(0x7200000000)
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "Module import check failed at line %d: %s\n", __LINE__, #x); \
    failed = 1; goto done; } } while (0)

static void put16(unsigned char *p, uint16_t x) {
    p[0] = (unsigned char)x; p[1] = (unsigned char)(x >> 8);
}
static void put32(unsigned char *p, uint32_t x) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (unsigned char)(x >> (i*8));
}
static void put64(unsigned char *p, uint64_t x) {
    for (unsigned i = 0; i < 8; ++i) p[i] = (unsigned char)(x >> (i*8));
}
/* String offsets are intentionally explicit for malformed-metadata tests. */
static const char strings[] = "\0synthetic-sdk\0sdk_add\0weak_hook\0local_data\0sdk_data\0sdk_update\0";
enum { NAME_DEP = 1, NAME_ADD = 15, NAME_WEAK = 23, NAME_LOCAL = 33, NAME_DATA = 44, NAME_UPDATE = 53 };
static void symbol(unsigned char *p, uint32_t name, unsigned char info,
                   uint16_t section, uint64_t value, uint64_t size) {
    put32(p, name); p[4] = info; put16(p + 6, section);
    put64(p + 8, value); put64(p + 16, size);
}
static void rela(unsigned char *p, uint64_t target, uint32_t type,
                 uint32_t sym, uint64_t addend) {
    put64(p, target); put64(p + 8, ((uint64_t)sym << 32) | type);
    put64(p + 16, addend);
}

static void fixture(unsigned char *f, size_t size) {
    memset(f, 0, size);
    memcpy(f, "\x7f" "ELF\x02\x01\x01", 7);
    put16(f + 16, 3); put16(f + 18, 183); put32(f + 20, 1);
    put64(f + 24, 0x100); put64(f + 32, 64);
    put16(f + 52, 64); put16(f + 54, 56); put16(f + 56, 2);
    unsigned char *ph = f + 64;
    put32(ph, 1); put32(ph + 4, 7); put64(ph + 8, 0);
    put64(ph + 16, 0); put64(ph + 32, 0xc00); put64(ph + 40, 0x2000);
    put64(ph + 48, 1);
    ph += 56;
    put32(ph, 2); put32(ph + 4, 6); put64(ph + 8, 0x300);
    put64(ph + 16, 0x300); put64(ph + 32, 13*16); put64(ph + 40, 13*16);
    put64(ph + 48, 1);
    const uint64_t dyn[][2] = {
        {1, NAME_DEP}, {4, 0x800}, {5, 0x900}, {6, 0x600},
        {7, 0xa00}, {8, 5*24}, {9, 24}, {10, sizeof strings}, {11, 24},
        {2, 24}, {20, 7}, {23, 0xb00}, {0, 0}
    };
    for (size_t i = 0; i < sizeof dyn / sizeof dyn[0]; ++i) {
        put64(f + 0x300 + i*16, dyn[i][0]);
        put64(f + 0x308 + i*16, dyn[i][1]);
    }
    symbol(f + 0x618, NAME_ADD, 0x12, 0, 0, 0); /* GLOBAL FUNC undefined */
    symbol(f + 0x630, NAME_WEAK, 0x20, 0, 0, 0); /* WEAK NOTYPE undefined */
    symbol(f + 0x648, NAME_LOCAL, 0x11, 1, 0x1800, 8);
    put32(f + 0x800, 1); put32(f + 0x804, 5); /* SysV nbucket, nchain */
    symbol(f + 0x660, NAME_DATA, 0x11, 0, 0, 0); /* imported data */
    put32(f + 0x808, 1);
    put32(f + 0x810, 2); put32(f + 0x814, 3); put32(f + 0x818, 4);
    memcpy(f + 0x900, strings, sizeof strings);
    rela(f + 0xa00, 0x1200, 1027, 0, 0x1800);
    rela(f + 0xa18, 0x1208, 1025, 1, 4);
    rela(f + 0xa30, 0x1210, 257, 3, UINT64_MAX - 7); /* -8 */
    rela(f + 0xa48, 0x1218, 257, 2, 0);
    rela(f + 0xa60, 0x1228, 1025, 4, 0);
    rela(f + 0xb00, 0x1220, 1026, 1, 0);
}

static int nested_status;
static void synthetic_consumer(Ctx *context) {
    uint64_t target = rd64(context->x[1]); /* guest import slot */
    call_indirect(context, target);
    nested_status = 0;
}

static int resolver_error(void *opaque, const char *name, uint64_t *out) {
    (void)opaque; (void)name; (void)out;
    return -1;
}
static int resolver_overflow(void *opaque, const char *name, uint64_t *out) {
    (void)opaque;
    if (!strcmp(name, "sdk_add")) { *out = UINT64_MAX - 1; return 0; }
    return 1;
}

/* All failed loads must preserve BOTH caller outputs. */
static int rejects(const unsigned char *bytes, size_t size,
                    const SSBUIOSGuestLinker *linker) {
    SSBUIOSGuestMemory mem = {0};
    uint64_t entry = 123;
    int rc = ssbu_guest_elf64_load_linked(bytes, size, MAIN_BIAS, linker, &mem, &entry);
    int valid = rc == -1 && !mem.host && !mem.length && !mem.guest_base && entry == 123;
    ssbu_guest_memory_close(&mem);
    return valid;
}

int ssbu_guest_module_import_selftest(void) {
    unsigned char bytes[4096];
    SSBUIOSGuestMemory arenas[2] = {{0}};
    SSBUIOSGuestMemory *mem = &arenas[0];
    SSBUIOSGuestAddressSpace *space = NULL;
    SSBUIOSGuestRuntime *runtime = NULL;
    const FnEntry main_functions[] = {{0x100, synthetic_consumer}};
    const SSBUIOSGuestExport sdk_exports[] = {{"sdk_add", 0x100}, {"sdk_data", 0x1800}, {"sdk_update", 0x200}};
    SSBUIOSGuestModule modules[] = {
        {"synthetic-main", MAIN_BIAS, MAIN_BIAS, 0x2000,
         main_functions, 1, NULL, 0},
        {"synthetic-sdk", SDK_BIAS, SDK_BIAS, 0x2000,
         fn_table_synthetic_sdk, fn_table_synthetic_sdk_count, sdk_exports, 3}
    };
    SSBUIOSModuleRegistry registry = {0};
    SSBUIOSGuestLinker linker = {ssbu_ios_modules_resolve,
                                ssbu_ios_modules_dependency, &registry};
    uint64_t entry = 0, pointer = 0;
    int failed = 0;
    CHECK(ssbu_ios_modules_init(&registry, modules, 2) == 0);
    const SSBUIOSModuleSymbols runtime_tables[] = {
        {"synthetic-main", NULL, 0}, {"synthetic-sdk", NULL, 0}
    };
    CHECK(ssbu_ios_runtime_create(&runtime, &registry, runtime_tables, 2) == 0);
    CHECK(ssbu_ios_runtime_bind(runtime) == 0);
    fixture(bytes, sizeof bytes);
    CHECK(ssbu_guest_elf64_load_linked(bytes, sizeof bytes, MAIN_BIAS,
                                      &linker, mem, &entry) == 0);
    CHECK(entry == MAIN_BIAS + 0x100);
    CHECK(ssbu_guest_memory_read(mem, MAIN_BIAS + 0x1200, &pointer, 8) == 0 &&
          pointer == MAIN_BIAS + 0x1800);
    CHECK(ssbu_guest_memory_read(mem, MAIN_BIAS + 0x1208, &pointer, 8) == 0 &&
          pointer == SDK_BIAS + 0x104);
    CHECK(ssbu_guest_memory_read(mem, MAIN_BIAS + 0x1210, &pointer, 8) == 0 &&
          pointer == MAIN_BIAS + 0x17f8);
    CHECK(ssbu_guest_memory_read(mem, MAIN_BIAS + 0x1218, &pointer, 8) == 0 &&
          pointer == 0);
    CHECK(ssbu_guest_memory_read(mem, MAIN_BIAS + 0x1220, &pointer, 8) == 0 &&
          pointer == SDK_BIAS + 0x100);
    CHECK(ssbu_guest_memory_read(mem, MAIN_BIAS + 0x1228, &entry, 8) == 0 &&
          entry == SDK_BIAS + 0x1800);
    entry = MAIN_BIAS + 0x100;
    /* ELF main entry -> original consumer C -> relocated import slot ->
     * separately linked provider C; ELF bytes remain non-executable data. */
    Ctx context = {0};
    context.x[0] = 5; context.x[1] = MAIN_BIAS + 0x1220;
    nested_status = -1;
    CHECK(ssbu_guest_memory_bind(mem) == 0);
    CHECK(ssbu_ios_runtime_call(&context, entry) == 0 &&
          nested_status == 0 && context.x[0] == 12 && context.fn == pointer);
    ssbu_guest_memory_unbind();
    CHECK(ssbu_ios_modules_dispatch(&registry, &context, SDK_BIAS + 0x101) == -1 &&
          context.x[0] == 12);
    /* Reload with the data-access provider imported by name, so BOTH function
     * and data pointers below come from the actual ELF relocation path. */
    ssbu_guest_memory_close(mem);
    fixture(bytes, sizeof bytes);
    put32(bytes + 0x618, NAME_UPDATE);
    CHECK(ssbu_guest_elf64_load_linked(bytes, sizeof bytes, MAIN_BIAS,
                                      &linker, mem, &entry) == 0);
    const uint64_t update_fn = SDK_BIAS + 0x200;
    CHECK(ssbu_guest_memory_open(&arenas[1], SDK_BIAS, 0x2000) == 0);
    const uint32_t sdk_before = 20;
    CHECK(ssbu_guest_memory_write(&arenas[1], SDK_BIAS + 0x1800, &sdk_before, 4) == 0);
    CHECK(ssbu_guest_address_space_create(&space, arenas, 2) == 0 &&
          !arenas[0].host && !arenas[1].host);
    CHECK(ssbu_guest_address_space_bind(space) == 0);
    context.x[0] = 3;
    context.x[1] = MAIN_BIAS + 0x1220;
    context.x[2] = rd64(MAIN_BIAS + 0x1228); /* actual relocated data import */
    context.x[3] = MAIN_BIAS + 0x1800;
    CHECK(ssbu_ios_runtime_call(&context, entry) == 0 &&
          nested_status == 0 && context.x[0] == 23 && context.fn == update_fn);
    CHECK(rd32(SDK_BIAS + 0x1800) == 23 && rd32(MAIN_BIAS + 0x1800) == 46);
    CHECK(ssbu_guest_address_space_close(&space) == -1 && space);
    ssbu_guest_memory_unbind();
    CHECK(ssbu_guest_address_space_close(&space) == 0 && !space);
    ssbu_ios_runtime_unbind();
    CHECK(ssbu_ios_runtime_close(&runtime) == 0 && !runtime);

    /* An absolute defined symbol does not acquire the image's load bias. */
    put16(bytes + 0x648 + 6, 0xfff1); put64(bytes + 0x648 + 8, 100);
    CHECK(ssbu_guest_elf64_load_linked(bytes, sizeof bytes, MAIN_BIAS,
                                      &linker, mem, &entry) == 0);
    CHECK(ssbu_guest_memory_read(mem, MAIN_BIAS + 0x1210, &pointer, 8) == 0 &&
          pointer == 92);
    ssbu_guest_memory_close(mem);
    fixture(bytes, sizeof bytes);
    /* Exercise JUMP_SLOT S+A with a nonzero addend explicitly. */
    put64(bytes + 0xb10, 4);
    CHECK(ssbu_guest_elf64_load_linked(bytes, sizeof bytes, MAIN_BIAS,
                                      &linker, mem, &entry) == 0);
    CHECK(ssbu_guest_memory_read(mem, MAIN_BIAS + 0x1220, &pointer, 8) == 0 &&
          pointer == SDK_BIAS + 0x104);
    ssbu_guest_memory_close(mem);

    /* A present provider also satisfies a weak import. */
    fixture(bytes, sizeof bytes);
    put32(bytes + 0x630, NAME_DATA);
    CHECK(ssbu_guest_elf64_load_linked(bytes, sizeof bytes, MAIN_BIAS,
                                      &linker, mem, &entry) == 0);
    CHECK(ssbu_guest_memory_read(mem, MAIN_BIAS + 0x1218, &pointer, 8) == 0 &&
          pointer == SDK_BIAS + 0x1800);
    ssbu_guest_memory_close(mem);

    /* Relocation writes may alias metadata: plan all values first. */
    fixture(bytes, sizeof bytes);
    put64(bytes + 0xa00, 0x618); /* first write overwrites imported symbol */
    CHECK(ssbu_guest_elf64_load_linked(bytes, sizeof bytes, MAIN_BIAS,
                                      &linker, mem, &entry) == 0);
    CHECK(ssbu_guest_memory_read(mem, MAIN_BIAS + 0x1220, &pointer, 8) == 0 &&
          pointer == SDK_BIAS + 0x100);
    ssbu_guest_memory_close(mem);

    /* PLT-only images can carry RELAENT without a regular RELA table. */
    fixture(bytes, sizeof bytes);
    put64(bytes + 0x340, 21); put64(bytes + 0x350, 21);
    CHECK(ssbu_guest_elf64_load_linked(bytes, sizeof bytes, MAIN_BIAS,
                                      &linker, mem, &entry) == 0);
    CHECK(ssbu_guest_memory_read(mem, MAIN_BIAS + 0x1220, &pointer, 8) == 0 &&
          pointer == SDK_BIAS + 0x100);
    ssbu_guest_memory_close(mem);

    fixture(bytes, sizeof bytes);
    CHECK(rejects(bytes, sizeof bytes, NULL)); /* DT_NEEDED without linker */
    put32(bytes + 0x618, NAME_WEAK); /* unresolved required symbol */
    CHECK(rejects(bytes, sizeof bytes, &linker));
    fixture(bytes, sizeof bytes);
    bytes[0x630 + 4] = 0x10; /* weak becomes required */
    CHECK(rejects(bytes, sizeof bytes, &linker));
    fixture(bytes, sizeof bytes);
    SSBUIOSGuestLinker broken = linker; broken.resolve = resolver_error;
    /* Resolver errors must not be hidden as missing weak imports. */
    put32(bytes + 0x618, NAME_WEAK); bytes[0x618 + 4] = 0x20;
    CHECK(rejects(bytes, sizeof bytes, &broken));
    fixture(bytes, sizeof bytes);
    broken.resolve = resolver_overflow;
    CHECK(rejects(bytes, sizeof bytes, &broken));

    /* Metadata failures and unsupported symbol/relocation models. */
    fixture(bytes, sizeof bytes);
    put64(bytes + 0x308, NAME_WEAK); /* missing named dependency */
    CHECK(rejects(bytes, sizeof bytes, &linker));
    fixture(bytes, sizeof bytes);
    put32(bytes + 0x618, sizeof strings); /* out-of-bounds string */
    CHECK(rejects(bytes, sizeof bytes, &linker));
    fixture(bytes, sizeof bytes);
    memset(bytes + 0x900 + NAME_ADD, 'x', sizeof strings - NAME_ADD);
    CHECK(rejects(bytes, sizeof bytes, &linker));
    fixture(bytes, sizeof bytes);
    put64(bytes + 0xa20, (UINT64_C(5) << 32) | 1025); /* index == nchain */
    CHECK(rejects(bytes, sizeof bytes, &linker));
    fixture(bytes, sizeof bytes);
    put64(bytes + 0x388, 16); /* wrong DT_SYMENT */
    CHECK(rejects(bytes, sizeof bytes, &linker));
    fixture(bytes, sizeof bytes);
    put64(bytes + 0x318, 0x1200); /* hash in BSS */
    CHECK(rejects(bytes, sizeof bytes, &linker));
    fixture(bytes, sizeof bytes);
    put32(bytes + 0x804, UINT32_MAX); /* oversized symbol count */
    CHECK(rejects(bytes, sizeof bytes, &linker));
    fixture(bytes, sizeof bytes);
    put64(bytes + 0x3a8, 17); /* PLTREL == REL instead of RELA */
    CHECK(rejects(bytes, sizeof bytes, &linker));
    fixture(bytes, sizeof bytes);
    bytes[0x618 + 4] = 0x16; /* TLS symbol */
    CHECK(rejects(bytes, sizeof bytes, &linker));
    fixture(bytes, sizeof bytes);
    put16(bytes + 0x648 + 6, 0xfff2); /* SHN_COMMON */
    CHECK(rejects(bytes, sizeof bytes, &linker));
    fixture(bytes, sizeof bytes);
    put64(bytes + 0x648 + 8, 0x3000); /* defined symbol outside PT_LOAD */
    CHECK(rejects(bytes, sizeof bytes, &linker));
    fixture(bytes, sizeof bytes);
    put64(bytes + 0xb08, (UINT64_C(1) << 32) | 1032); /* IRELATIVE */
    CHECK(rejects(bytes, sizeof bytes, &linker));
    fixture(bytes, sizeof bytes);
    put64(bytes + 0xb00, 0x1ffc); /* target crosses segment end */
    CHECK(rejects(bytes, sizeof bytes, &linker));
    fixture(bytes, sizeof bytes);
    put64(bytes + 64 + 56 + 8, 0x310); /* dynamic PHDR/file mismatch */
    CHECK(rejects(bytes, sizeof bytes, &linker));
    fixture(bytes, sizeof bytes);
    put64(bytes + 0x3b0, 6); /* duplicate SYMTAB */
    CHECK(rejects(bytes, sizeof bytes, &linker));
    fixture(bytes, sizeof bytes);
    put64(bytes + 0x3b0, 0x6ffffff0); /* versioning */
    CHECK(rejects(bytes, sizeof bytes, &linker));
    fixture(bytes, sizeof bytes);
    put64(bytes + 0x3b0, 36); /* RELR must not be silently skipped */
    CHECK(rejects(bytes, sizeof bytes, &linker));
    fixture(bytes, sizeof bytes);
    put64(bytes + 0xa40, UINT64_C(0x8000000000000000)); /* INT64_MIN */
    CHECK(rejects(bytes, sizeof bytes, &linker));
    /* Relocation target in a hole inside the arena but outside PT_LOAD. */
    fixture(bytes, sizeof bytes);
    put16(bytes + 56, 3);
    put64(bytes + 64 + 40, 0x1900);
    unsigned char *third = bytes + 64 + 2*56;
    put32(third, 1); put32(third + 4, 6);
    put64(third + 8, 0xc00); put64(third + 16, 0x2000);
    put64(third + 32, 0x100); put64(third + 40, 0x100); put64(third + 48, 1);
    put64(bytes + 0xb00, 0x1a00);
    CHECK(rejects(bytes, sizeof bytes, &linker));

    /* Registry output remains unchanged on descriptor/collision failures. */
    SSBUIOSModuleRegistry saved = registry;
    modules[1].guest_start = MAIN_BIAS;
    CHECK(ssbu_ios_modules_init(&registry, modules, 2) == -1 &&
          registry.modules == saved.modules && registry.count == saved.count);
    modules[1].guest_start = SDK_BIAS;
    modules[1].name = modules[0].name;
    CHECK(ssbu_ios_modules_init(&registry, modules, 2) == -1);
    modules[1].name = "synthetic-sdk";
    const SSBUIOSGuestExport duplicate[] = {{"sdk_add", 0x100}};
    modules[0].exports = duplicate; modules[0].export_count = 1;
    CHECK(ssbu_ios_modules_init(&registry, modules, 2) == -1);
    modules[0].exports = NULL; modules[0].export_count = 0;
    const FnEntry unsorted[] = {{0x200, synthetic_consumer}, {0x100, synthetic_consumer}};
    modules[0].functions = unsorted; modules[0].function_count = 2;
    CHECK(ssbu_ios_modules_init(&registry, modules, 2) == -1);
    modules[0].functions = main_functions; modules[0].function_count = 1;
    CHECK(ssbu_ios_modules_init(&registry, modules, 2) == 0);
    pointer = 123;
    CHECK(ssbu_ios_modules_resolve(&registry, "absent", &pointer) == 1 && pointer == 123);
    CHECK(ssbu_ios_modules_resolve(&registry, "SDK_ADD", &pointer) == 1 && pointer == 123);
    CHECK(ssbu_ios_modules_dispatch(&registry, NULL, MAIN_BIAS + 0x100) == -1);
done:
    ssbu_guest_memory_unbind();
    ssbu_guest_memory_close(mem);
    ssbu_guest_memory_close(&arenas[1]);
    if (ssbu_guest_address_space_close(&space)) failed = 1;
    ssbu_ios_runtime_unbind();
    if (ssbu_ios_runtime_close(&runtime)) failed = 1;
    return failed;
}
