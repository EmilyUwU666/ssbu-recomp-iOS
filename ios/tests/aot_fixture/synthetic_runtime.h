#ifndef SSBU_SYNTHETIC_RUNTIME_H
#define SSBU_SYNTHETIC_RUNTIME_H
#include "../../../runtime/runtime.h"
#define SSBU_BRIDGE_MAIN UINT64_C(0x7400000000)
#define SSBU_BRIDGE_SDK UINT64_C(0x7500000000)
extern const FnEntry fn_table_bridge_main[];
extern const size_t fn_table_bridge_main_count;
extern const DynSym dyn_syms_bridge_main[];
extern const size_t dyn_sym_count_bridge_main;
extern const FnEntry fn_table_bridge_sdk[];
extern const size_t fn_table_bridge_sdk_count;
extern const DynSym dyn_syms_bridge_sdk[];
extern const size_t dyn_sym_count_bridge_sdk;
#endif
