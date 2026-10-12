#ifndef SSBU_IOS_GUEST_DISPATCH_H
#define SSBU_IOS_GUEST_DISPATCH_H

#include "../../runtime/runtime.h"

/*
 * Dispatch statically linked recompilation functions via their guest addresses.
 * These symbols are currently supplied by the ORIGINAL synthetic test library.
 * Later integrating generated game tables needs imports, collision handling,
 * guest image loading, relocation, and extensive runtime porting.
 */
int ssbu_ios_dispatch_guest(Ctx *context, uint64_t guest_address);

#endif
