/* Original generated-style provider with its OWN import-index namespace. */
#define SSBU_GUEST_ADDRESS_TRANSLATION 1
#include "synthetic_runtime.h"

static void sdk_update(Ctx *c) {
    uint64_t lr = c->x[30];
    const char *name = c->in_import;
    uint64_t from = c->import_from;
    uint32_t value = rd32(c->x[2]) + (uint32_t)c->x[0];
    wr32(c->x[2], value);
    c->x[0] = value;
    c->x[30] = SSBU_BRIDGE_SDK + 0x110;
    host_import(c, 0); /* provider index 0 == main_callback */
    if (c->in_import == name && c->import_from == from) ++c->x[11];
    else ++c->x[12];
    wr32(c->x[3], (uint32_t)c->x[0] * 2);
    c->x[30] = lr;
}
const FnEntry fn_table_bridge_sdk[] = {{0x100, sdk_update}};
const size_t fn_table_bridge_sdk_count = 1;
const DynSym dyn_syms_bridge_sdk[] = {{"main_callback", 0, 0}};
const size_t dyn_sym_count_bridge_sdk = 1;
