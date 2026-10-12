/* Original generated-style module. Uses the actual runtime.h call ABI. */
#define SSBU_GUEST_ADDRESS_TRANSLATION 1
#include "synthetic_runtime.h"

static void main_callback(Ctx *c) {
    if (c->in_import && !strcmp(c->in_import, "main_callback") &&
        c->import_from == SSBU_BRIDGE_SDK + 0x10c) ++c->x[10];
    else ++c->x[12];
    ++c->x[0];
}
static void main_entry(Ctx *c) {
    uint64_t lr = c->x[30];
    c->x[2] = rd64(SSBU_BRIDGE_MAIN + 0x1108); /* imported data VA */
    c->x[3] = SSBU_BRIDGE_MAIN + 0x1800;
    c->x[30] = SSBU_BRIDGE_MAIN + 0x110;
    call_indirect(c, rd64(SSBU_BRIDGE_MAIN + 0x1100));
    c->x[30] = SSBU_BRIDGE_MAIN + 0x114;
    host_import(c, 0); /* SAME index as provider, DIFFERENT imported name. */
    c->x[30] = lr;
}
static void main_defined(Ctx *c) { c->x[0] += 2; }

const FnEntry fn_table_bridge_main[] = {
    {0x100, main_entry}, {0x200, main_callback}, {0x300, main_defined}
};
const size_t fn_table_bridge_main_count = 3;
const DynSym dyn_syms_bridge_main[] = {
    {"sdk_bridge_update", 0, 0}, {"main_defined", 0x300, 1},
    {"sdk_data", 0, 0}, {"absent_required", 0, 0}
};
const size_t dyn_sym_count_bridge_main = 4;
