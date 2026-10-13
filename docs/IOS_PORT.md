# iOS / iPadOS static-recompilation port — checkpoints 0–2 (experimental)

Status: **bootstrap only, no gameplay, no IPA, and no signed device build.**
The existing Windows port in this fork is kept intact.

## What this branch provides

- A minimal native SwiftUI iOS application with a C interoperability bridge.
- A C portability probe that includes the **real upstream** `runtime/runtime.h`
  and verifies guest context layout, memory accessors, bit operations,
  SIMD helpers, signed arithmetic, and conversions.
- An isolated, relocatable guest-memory experiment with bounds checks.
- An **opt-in** address translation hook in `runtime/runtime.h`. The C accessors
  `rd8/16/32/64`, `wr8/16/32/64`, `rdv/wrv`, and `stx8/16/32/64`
  plus the `neon.h` interleaved `v_ldn/v_stn` helpers now translate
  guest addresses when compiled with
  `SSBU_GUEST_ADDRESS_TRANSLATION`. Without this define, Windows stays unchanged.
- Synthetic tests of the real `rd/wr/rdv/wrv`, SIMD interleaved accessors,
  and one atomic `STXR` success case against a guest-memory
  mapping. This does **not** make other upstream host/runtime pointer casts safe.
- A native Metal presentation/clear pass in the iOS bootstrap app. No NVN
  graphics commands or in-game imagery are rendered yet.
- An incremental iOS arm64 static archive compiler for generated C files,
  tested in CI with a synthetic (original) C fixture.
- A separate `LiftedFixture` **static library target linked into the
  actual iOS app**, defining a synthetic `fn_table` and functions with
  the generated AArch64 `Ctx *` interface. A portable dispatcher resolves
  synthetic guest function addresses and invokes the statically linked code,
  exercising guest memory writes through address translation.
  This does **not** link or execute actual game code.
- A bounded `R_AARCH64_RELATIVE` relocation helper operating on the relocatable
  guest memory arena, including explicit ELF load-bias support.
- A new **synthetic ELF64/AArch64 ET_DYN image loader** that validates program
  headers, copies `PT_LOAD` data, zero-initializes BSS, inspects `PT_DYNAMIC`
  for `DT_RELA`, and applies supported relative relocations to a private
  guest-memory arena. Malformed inputs do not publish a partially loaded image.
  In a synthetic end-to-end test, its entry address resolves via the
  statically linked guest dispatcher and updates relocated guest memory
  using the AOT-compiled C fixture. It does not execute the ELF's native
  machine code, and is not a complete game/NSO/NRO loader.
- An immutable static AOT **module registry** with per-module load biases,
  sorted function/export tables, exact symbol lookup, dependency-name validation,
  and rejection of overlapping module ranges, duplicate module names/unselected exports,
  invalid addresses and unsorted function tables.
- Opt-in **ELF import linking** (`ssbu_guest_elf64_load_linked`) with bounded
  SysV `DT_HASH` symbol counts, `DT_SYMTAB`/`DT_STRTAB`, eager `DT_JMPREL`,
  and `R_AARCH64_ABS64`, `GLOB_DAT`, `JUMP_SLOT` and `RELATIVE`. Relocation
  slots contain guest VAs; only the dispatcher holds native C function pointers.
  Required missing imports/dependencies fail atomically, unresolved weak imports
  become zero, and defined/absolute symbols are checked before publication.
- A synthetic main ELF -> import slot -> separately linked `LiftedSDKFixture`
  function test in both the host CLI and the iOS app's runtime probe. This verifies
  the static linking architecture, not game SDK compatibility.
- An **owned multi-arena address space** which adopts loaded mappings, rejects
  overlapping guest/host ranges, and translates complete accesses using an
  immutable sorted region table. Generated typed helpers can now dereference
  relocated imports across caller/provider arenas.
- **Per-thread bindings** with mutex-protected mapping pins. Closing an owned
  space fails while any worker is bound; explicit unbind or the pthread-key
  thread-exit destructor releases the pin. Workers bind explicitly and do not
  inherit the parent's binding. Borrowed single-arena probes are still supported.
- End-to-end synthetic function + data imports: provider C reads/writes its own
  imported data and writes a result back into a different caller arena. Worker
  tests cover isolation at identical guest VAs, refused teardown, thread-exit
  cleanup, mapping ownership transfer, overlap rejection and crossing boundaries.
- A portable **generated-call ABI bridge** implementing iOS-only
  `call_indirect(Ctx *, VA)` and `host_import(Ctx *, symbol_index)`. A per-worker
  runtime scope tracks the executing module, so nested imports resolve against
  that module's own generated `DynSym` table and restore the caller's scope.
  Import diagnostic metadata is restored after normal nested returns.
- A separate `LiftedRuntimeFixture` static target with two original modules
  using the real runtime.h ABI. Both use symbol index zero for different imports;
  a callback into the caller verifies scope restoration and cross-arena writes.
  The ELF import test now also uses the actual `call_indirect` wrapper.
- A **private module metadata preparer**: reads bounded ELF load/dynamic/symbol
  tables, preserves module-local symbol indices, converts upstream lifted
  function-table VAs to checked module offsets, and emits uniquely named static
  function/export/symbol descriptors plus an ELF-hash report.
- The AOT compiler can replace the original generated `table.c`/`imports.c`
  with prepared metadata and compile each module into a separate archive.
  CI tests two original synthetic module archives with a real host-runtime call,
  then cross-compiles both archives for iOS ARM64.
- An optional original static **host-service provider** for checked memcpy,
  memmove, memset, memcmp and memchr, fourteen checked string functions, plus nn::os tick/frequency/conversion,
  sleep and yield imports. The provider uses reserved guest function addresses
  and the same runtime import scopes; native data pointers are never returned.
- Explicit iOS fatal diagnostics for generated guest traps, untranslated
  instructions and unsupported direct kernel SVC. Host subprocess tests verify
  these terminate; no exception/kernel behavior is fabricated.
- **Explicit named provider preferences** allow selected SDK/host export
  collisions while preserving strict duplicate rejection by default. Runtime
  imports and optional ELF definition preemption use the same selection;
  prepared metadata preserves function-definition eligibility, so protected/
  local definitions and legacy unflagged runtime definitions stay local.
- A host CLI smoke test, fatal-diagnostic checks, host ThreadSanitizer CI check
  and ARM64 iOS syntax check.
- A GitHub Actions macOS runner that generates an Xcode project with
  XcodeGen, builds an **unsigned simulator application**, and separately compiles
  an **unsigned iOS ARM64 device app**.

The simulator artifact is **not** a device IPA and cannot play the game.
The unsigned device build is only an ARM64 compilation check; it is not signed,
installable on a stock iPhone, or a playable game.
It is deliberately an isolated, deterministic first checkpoint requiring
neither Nintendo content nor Apple signing credentials.

## Build locally (Mac with Xcode)

```sh
brew install xcodegen
cd ios
xcodegen generate
xcodebuild -project SSBURecompIOS.xcodeproj -scheme SSBURecompIOS \
  -configuration Debug -sdk iphonesimulator \
  -destination 'generic/platform=iOS Simulator' \
  CODE_SIGNING_ALLOWED=NO build
```

On macOS, the synthetic C tests can also run without XcodeGen:

```sh
clang -std=c11 -pthread -O2 -I runtime \
  ios/Portability/Probe.c ios/Portability/GuestMemory.c \
  ios/Portability/GuestRelocations.c ios/Portability/GuestELFLoader.c \
  ios/Portability/ProbeELFLoader.c ios/Portability/GuestMemoryBinding.c \
  ios/Portability/ProbeTranslated.c ios/Portability/GuestDispatch.c \
  ios/Portability/GuestModules.c ios/Portability/ProbeModuleImports.c \
  ios/Portability/ProbeAddressSpace.c ios/Portability/GuestRuntime.c \
  ios/Portability/ProbeGuestRuntime.c ios/Portability/GuestHostServices.c \
  ios/Portability/GuestDiagnostics.c ios/Portability/ProbeHostServices.c \
  ios/Portability/ProbeProviderPreferences.c \
  ios/tests/aot_fixture/synthetic_game.c ios/tests/aot_fixture/synthetic_sdk.c \
  ios/tests/aot_fixture/synthetic_runtime_main.c \
  ios/tests/aot_fixture/synthetic_runtime_sdk.c \
  ios/Portability/ProbeCli.c \
  -o /tmp/ssbu-probe
/tmp/ssbu-probe
```

## AOT compilation experiment

The branch includes `ios/scripts/compile_generated.py`. Run it on a Mac
with Xcode's command line tools, using *locally* generated C sources from
`tools/lifter/recomp_eh.py` and their accompanying generated headers:

```sh
python3 ios/scripts/compile_generated.py \
  --generated /path/to/private/generated_C \
  --output /path/to/private/build-ios-aot \
  --jobs 4
```

The input folder must contain `.c` files, including any generated tables,
and the matching generated headers. It can be produced on a separate computer;
only the compilation phase requires macOS with the iOS SDK.

The output is a **partial build product**, `libssbu_generated_ios_arm64.a`,
not an iOS executable, game, app, or IPA. It does not resolve the game's
imports or DLL/plugin loading and may expose Clang compatibility problems
that need debugging. Never commit generated code or artifacts derived from
your game dump. The CI test invokes this script only with a hand-written
synthetic C fixture in `ios/tests/aot_fixture`.

## Major upstream constraints still to solve

1. **Host/runtime dependencies**: `runtime/boot.c`, `runtime/hostrt.c`,
   `runtime/os_threads.c` and other modules rely on Win32 threads,
   VirtualAlloc, synchronisation, Win32 file and timing APIs.
2. **Executable loading**: `tools/lifter/build.py` links the main
   recompiled code into a Windows DLL and loads other runtime modules
   as DLLs. iOS execution should precompile and statically link the game
   code and eligible modules in the signed application. No downloaded
   executable pages, runtime JIT, or external game-code DLLs.
3. **Graphics**: current NVN backends are Win32 Vulkan/OpenGL.
   Plan a Metal presentation layer and a MoltenVK-based feasibility
   experiment, or translate NVN calls to a native Metal backend.
4. **Guest virtual addresses**: main's fixed high virtual addresses
   and guest-as-host-pointer assumptions are especially dangerous under
   Apple's address space layout; redesign or safely validate memory
   mapping and relocation without clobbering platform memory.
5. **Assets**: 13.0.4 user-supplied dump parsing, sandboxed storage,
   shader cache, saves, and usable per-device install flow are unimplemented.
6. **Input, audio, video, threads, and FPS**: controllers/touch UI and
   Apple media/system services need actual runtime integrations and tests.
7. **Size and limits**: upstream-generated C and binaries are large.
   Linker memory, app size, memory pressure, GPU shader compile overhead,
   and thermal constraints must be measured on actual devices.

## Planned verified checkpoints

- **0 — Verified**: Xcode bootstrap, shared C helper checks, simulator and device ARM64 compilation.
- **1 — In progress**: portable memory translation hooked into typed generated-code helpers;
  tests with generated synthetic guest data (not game code).
- **2 — Synthetic static dispatch verified only**: AOT game + module linking
  from an owner's local dump with reproducible device-build tooling; resolve
  imports, symbol collisions, unwinding, and the real module loader. The
  iOS app now statically links synthetic lifted code, not actual SSBU code.
- **3 — Presentation prototype only**: render a static Metal clear pass;
  next implement NVN drawing and test real, reproducible game frames.
- **4**: game startup, input, audio, videos, save paths, and playable tests.
- **5**: performance evaluation, packaging, and optional device distribution.

The opt-in memory accessor path is **incomplete**: direct guest pointer casts,
Win32 virtual mappings, unwinding and actual guest SDK calls still need porting.
Owned multi-arena worker scopes protect mapping lifetime, but production worker
scheduling/publication and all direct host/runtime pointer casts remain unaudited. The ELF loader
supports ET_DYN, PT_LOAD and a restricted eager RELA/JMPREL import subset.
NSO/NRO conversion/metadata integration, GNU-hash-only images, versioned symbols,
TLS/IFUNC/COPY and packed REL/RELR relocations, automatic symbol interposition, initialization/finalization,
GPU execution and section permissions are unsupported. The new iOS-only
`call_indirect`/`host_import` bridge routes through the static module registry;
the upstream Windows host implementation is unchanged. Actual generated game
and SDK metadata, hardcoded guest addresses and Win32 import services still
need integration.
The registry's dependency callback verifies descriptors, not loaded data arenas:
the caller must stage dependencies before execution. The owned address space
now translates data across separate arenas and pins their lifetimes per worker.
The bridge terminates on an invalid translated access rather than dereferencing
host address 0. Callers using the borrowed single-arena compatibility API must
still unbind before freeing that mapping.

All claims should be backed by build logs or device captures. In particular,
successfully compiling one runtime header must not be described as compiling
the actual game.

## Legal and distribution

This branch contains no game assets, binaries derived from a game dump,
console keys, firmware, or redistributed Nintendo content. The game requires
the user to supply their own lawfully obtained files. Apple TestFlight review,
code-signing requirements, and intellectual-property restrictions still apply.

## Static module import contract

`SSBUIOSGuestModule` descriptors are original build metadata for prelinked C
functions. `FnEntry.addr` and `SSBUIOSGuestExport.offset` are module-local ELF
virtual addresses, rebased by `load_bias` into the declared exclusive guest VA
interval. Export tables are sorted by exact `strcmp` order; duplicate export
names across modules are rejected unless an explicit named provider preference
covers that exact symbol. There is no automatic weak-definition precedence. The
registry borrows immutable descriptors and never registers downloaded code,
unloads modules, or owns guest-memory mappings.

Prepare and validate a dependency registry, supply `ssbu_ios_modules_resolve`
and `ssbu_ios_modules_dependency` as `SSBUIOSGuestLinker` callbacks, and stage
an ELF with `ssbu_guest_elf64_load_linked`. All relocation values are planned
before writes, and only a successful complete load publishes the arena/entry.
Callback side effects are outside the rollback contract; callbacks must use
immutable tables and return guest addresses, not host pointers.

The symbolic relocation subset uses `S + A` for ABS64, GLOB_DAT and JUMP_SLOT,
and `load_bias + A` for RELATIVE, following the
[Arm ELF64 relocation definitions](https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst).
This is an explicitly restricted platform contract; the upstream Windows host
currently ignores addends for GLOB_DAT/JUMP_SLOT, so actual Switch module behavior
still needs verification with local dumps. No lazy PLT resolver or native ELF
entry execution is implemented. Resolved function VAs must have corresponding
statically linked Ctx entries before dispatch; data exports may have no function
entry. The existing `ssbu_guest_elf64_load` wrapper supplies no linker callbacks.

Validation includes function and data imports, local and SHN_ABS definitions,
weak-null imports, signed addends, cross-module guest-address dispatch, missing
required symbols/dependencies, bad string/hash/symbol tables, unsupported
relocations, overflow, malformed dynamic PHDRs, and registry collisions.
Only original synthetic input is used. Actual Smash main/SDK/fighter modules,
production worker scheduling and Apple runtime services remain unverified.

## Owned guest address spaces and worker scopes

After staging separate module ELF arenas, pass their owning descriptors to
`ssbu_guest_address_space_create`. Success moves ownership into the new space
and clears the input descriptors. It accepts unsorted input, rejects overlapping
host or guest intervals, and performs no partial ownership transfer on failure.
Do not retain copied owners or raw host pointers to transferred mappings.

Every worker calls `ssbu_guest_address_space_bind` before using generated memory
helpers. One thread can bind one space; rebinding requires explicit unbind.
Bindings do not inherit into new threads. Reads/writes locate the arena from its
guest VA and validate the entire access; a store cannot span two adjacent arenas.
Region tables are immutable and lookup does not hold the lifetime mutex while
performing a guest access. Shared guest-memory data synchronization remains the
guest runtime's responsibility; mapping pins do not serialize data writes.

Unbind with `ssbu_guest_memory_unbind`; thread exit also releases a binding via
the pthread-key destructor. `ssbu_guest_address_space_close` returns -1 while
any binding is active and leaves its handle/mappings intact. The owner must stop
new bind attempts and synchronize the owning handle before destroying it.
Join workers before final destruction. This handles already-acquired worker
scopes, not arbitrary races in publication of a raw address-space pointer.
Borrowed host pointers are valid only inside the acquiring thread's binding.

Synthetic checks run in the CLI and iOS bootstrap: separately loaded function
and data imports, caller/provider memory writes, two worker spaces using the
same guest VA, non-inheritance, active-pin teardown refusal, thread-exit cleanup,
and guest/host overlap, holes and boundary rejection. Local host tests also
pass AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer. LeakSanitizer
is disabled locally due to restricted /proc task inspection. No real game code,
NSO/NRO module ingestion or complete SDK/Win32 service integration has been validated.

## Generated call and import scopes

`SSBUIOSModuleSymbols` associates each registry module name with that module's
own immutable generated `DynSym` table. `ssbu_ios_runtime_create` requires one
entry per module, validates names/defined-symbol offsets, and normalizes input
order. It borrows the registry/module/symbol storage; keep that storage immutable
and alive until the runtime is closed. Its per-thread bindings pin the runtime,
not guest mappings; bind an owned address space separately before memory access.

Enter code through `ssbu_ios_runtime_call`. The bridge tracks the executing
module across calls independently of `Ctx.fn`, which records the most recently
entered function. A generated `host_import(c, index)` therefore resolves an index
from the active module's symbol table, even after a nested callback into a
module whose index namespace differs. Defined symbols use that module's load
bias unless optional eligibility metadata and an explicit preference permit a
selected provider. Undefined names resolve through the static export registry. Data exports
and addresses without corresponding AOT function entries cannot be called.
`Ctx.in_import` and `Ctx.import_from` are restored after normal nested returns.

The checked call/import variants return -1 for missing bindings, invalid indices,
null/unknown addresses, missing imports, data-only targets, invalid return PCs or
excessive nesting; lookup failures preserve context state. Import calls outside
an entered runtime scope are rejected rather than guessing a module from the
last `Ctx.fn`. Generated void ABI wrappers abort on failure and never silently
return a fabricated zero result. Unbind during a running call is ignored to
protect its frame. Runtime close refuses active worker bindings; explicit unbind
or a pthread thread-exit destructor releases them. Stop new bind attempts before
final destruction and join workers, as for address spaces.

Limitations: `DynSym` exposes only name/value/defined, so runtime table definitions
must supply module-relative symbol values. Optional preemptibility bytes preserve
eligibility for provider selection but do not represent full SHN_ABS/type/version
metadata. Absent flags keep all defined runtime imports local. ELF relocation metadata remains separately checked.
A missing weak function is not callable. The optional memory/timing provider is
available and generated trap/SVC symbols now stop with diagnostics. Most SDK
services, actual kernel SVC, exception/non-local unwinding and production thread
scheduling remain unfinished.
Generated sources must use these external ABI wrappers and module-unique tables;
standalone generated dispatch definitions cannot be linked unchanged alongside
this bridge. Only original synthetic fixtures have been compiled and executed.

## Prepare local generated modules (Windows/Linux/macOS; Python 3.10+)

The preparer can use a locally converted ELF and the existing lifter's `table.c`.
It does not extract a title, lift instructions, or run the ELF. Keep derived inputs
and outputs outside tracked source folders (for example under ignored `build/`).

```sh
python3 ios/scripts/prepare_module.py \
  --elf /private/main.elf \
  --functions /private/gen-main/table.c \
  --module main --name main \
  --load-bias 0x7100000000 \
  --function-addresses guest \
  --output /private/build/main_metadata.c
```

Select `--function-addresses guest` when table entries are absolute guest VAs
(as in the upstream main lifter), or `offset` for module-local ELF VAs. For a
named NRO-style table, pass `--function-table fn_table_MODULENAME`. The load bias
must match addresses embedded in the lifted functions; this tool adjusts metadata
only, not hardcoded PCs or pointer constants in generated code. `--module` is a
unique C identifier for output names; `--name` is the exact registry/dependency
name, including any name expected by another ELF's `DT_NEEDED`.

The C output defines `ssbu_main_module` and `ssbu_main_module_symbols` (with the
chosen module ID replacing `main`). It provides its own checked function table,
symbol table in the ELF's original index order, and byte-sorted export names.
The adjacent JSON report records source ELF SHA-256, bias/arena extent, counts,
dependency names and descriptor symbols. On validation failure, existing output
files remain untouched. C strings use fixed-width byte escapes, preserving raw
symbol names without allowing names to inject C tokens.

On a Mac with Xcode, compile the lifted function sources with this metadata:

```sh
python3 ios/scripts/compile_generated.py \
  --generated /private/gen-main \
  --metadata /private/build/main_metadata.c \
  --output /private/build/main-arm64 --jobs 4
```

Repeat for SDK/other modules with distinct metadata and output directories. With
`--metadata`, the compiler omits the input folder's `table.c` and `imports.c`;
`--exclude-source filename.c` omits additional flat source files if needed. It
includes the portable metadata headers, compiles the replacement separately,
and adds only the selected objects to that module's archive. Wire descriptors
into `ssbu_ios_modules_init` and their symbol descriptors into
`ssbu_ios_runtime_create` when integrating the signed app's build.

This remains a partial archive pipeline. It does not implement NSO/NRO conversion,
SDK host services, app asset loading, exception/resume tables, native module
initializers or a complete executable link. Removing the old table file also
removes any exception/resume metadata it contained; that metadata needs a future
compatible unwinding path. Standalone generated dispatchers/NRO import adapters
must be adapted separately. No real Smash-generated code has been compiled here.

The preparer currently requires a SysV hash and the restricted symbol types/
bindings understood by the runtime. It rejects special/absolute symbols,
unnamed non-null symbols, unsupported PCS/versioning/packed relocation models,
malformed tables, out-of-range definitions and function entries outside text.
The stricter rejection can expose actual-module compatibility work; it should
not be treated as proof of supporting every Smash main/SDK/fighter module.

Run the original pipeline checks with:

```sh
python3 ios/tests/test_prepare_module.py
```

They prepare two ELF/lifted-code modules, compile separate host archives, and
link/invoke them through the actual portable runtime. CI additionally invokes
the real iOS archive compiler with prepared metadata; original input tables are
intentionally non-compilable to verify replacement/exclusion. Malformed inputs
and C-unsafe names/table syntax are tested without game content.

## Optional portable memory and timing imports

Include `ssbu_ios_host_services_module` alongside the prepared guest modules
and `ssbu_ios_host_services_symbols` alongside their runtime symbol descriptors.
This provider defines statically linked Ctx handlers at reserved guest VAs
starting at `SSBU_IOS_HOST_SERVICES_BASE`. It needs no guest data arena of its
own. Exact C/nn::os import names resolve through the same export registry and
can therefore be placed in ELF import relocation slots and invoked through
`host_import`/`call_indirect`. Host function/data pointers are never guest values.

The supported imports are memcpy, memmove, memset, memcmp and memchr, plus
nn::os GetSystemTick, GetSystemTickFrequency, ConvertToTick, ConvertToTimeSpan,
SleepThread and YieldThread. Checked memory services validate complete source
and destination ranges before accessing memory. Nonzero accesses must fit
single bound arenas; source and destination can live in different arenas.
Memcpy overlap is rejected, memmove supports it, zero-byte operations do not
access pointers, and memchr returns a guest VA. Invalid checked arguments leave
memory/registers unchanged; the generated void adapter aborts with a diagnostic.

String imports include strlen, strnlen, strcmp, strncmp, strcpy, strncpy, strchr,
strrchr, strcat, strncat, strstr, strpbrk, strspn and strcspn. Scans stay within one
bound arena, even if another arena is adjacent.
Unbounded scans must reach a NUL or an earlier valid result (a comparison
difference or first strchr match); bounded reads can stop at count or NUL without
requiring the unused suffix to be mapped. Comparisons use unsigned bytes.
Copies validate all required source/destination bytes and reject overlap before
writing. Strncpy copies exactly count bytes, padding after a NUL, and need not
terminate a truncated result. Zero-count bounded calls do not access pointers;
searches return guest VAs, including the terminator when searching for NUL.
Concatenation validates the existing terminated destination before any source
access, reserves space for the final NUL, and rejects source overlap with the
entire resulting destination interval. Strncat consumes at most count source
bytes, appends those bytes and always terminates the result (unlike strncpy).
At count zero it validates the destination but does not access the source.
Substring and character-set searches require both complete strings to terminate
inside their arenas, even if an early result or empty needle/set would otherwise
be possible. They return guest pointers or byte counts, never host pointers.
The checked span API exposes only the remainder of one arena with a borrowed
binding lifetime. Callers must synchronize concurrent changes to string memory;
the validation/write sequence is not a transaction against other workers.

Integer imports include atoi, atol, atoll, strtol, strtoll, strtoul and strtoull,
plus __errno and __errno_location aliases. This is an explicit LP64, ASCII/C-locale ABI: int is 32 bits;
long and long long are 64 bits. Base zero recognizes decimal, octal and 0x
hexadecimal prefixes; bases 2..36 are supported. C23 binary prefixes and locale
extensions are not implemented. Strto functions consume all valid digits even
on overflow, return a saturated limit, and store guest ERANGE (newlib-style
value 34). Unsigned negative values wrap when representable. Successful/no-digit
conversions preserve errno; no-digit end pointers point to the original input.
Invalid bases fail the checked call rather than publishing a result/errno.
Simple atoi/atol/atoll overflow is rejected rather than invoking undefined C
behavior. Numeric strings must terminate inside one bound arena.

Each worker must call `ssbu_guest_errno_bind(nonzero_aligned_guest_address)`
after its memory bind before using errno-location or strto imports. This validates a
four-byte cell without allocating or initializing it. Owned address-space
bindings pin the mapping; legacy bindings remain borrowed. The location is
discarded on memory unbind/thread exit and does not inherit into other workers.
The caller reserves distinct cells or synchronizes shared cells. __errno returns
the guest address. Optional eight-byte end-pointer outputs may be in another
arena; input/output/errno overlap or an invalid output range fails before writes.
No host errno or native pointer is exposed, and this is not a complete console
TLS/reentrancy implementation. The guest errno ABI still needs actual SDK
validation. See the [newlib integer conversion reference](https://sourceware.org/newlib/libc.html).

Qsort and bsearch imports call statically registered guest comparator addresses
through the checked runtime. Arguments identify guest array elements, and
bsearch's first comparator argument is the original guest key pointer. Each
callback receives a copied Ctx with the caller's stack/TLS and import diagnostic
state; its signed 32-bit x0 result controls ordering. Nested imports and sorting
a separate array are supported. Caller registers remain unchanged for qsort;
bsearch changes x0 to a guest match address or zero. The source array must be
properly ordered for bsearch. Element sizes must be positive for nonempty calls;
zero-count calls access neither pointers nor the comparator.

The complete array must fit one bound arena, with checked count/size arithmetic.
Bsearch validates one key byte because the key structure may be smaller than an
array element; its comparator must use checked reads for the actual key fields.
Qsort uses a bottom-up index mergesort and a payload snapshot, with at most
64 MiB of scratch (payload plus two index arrays). It changes array order only
after all comparisons succeed and the input still matches the snapshot. Invalid
callback targets, unsupported sizes, scratch exhaustion and detected array
mutation fail without service writes. Callback side effects cannot be undone;
the mutation check is not a transaction or race detector. Comparators must
return normally, remain consistent and leave array contents untouched, as in
the [qsort callback contract](https://pubs.opengroup.org/onlinepubs/009695399/functions/qsort.html).
Non-local exits, automatic stack allocation and native callback pointers are
unsupported. Caller-owned memory synchronization and guest stack/TLS setup
remain required.

Balanced worker memory guards keep bindings pinned during callback services;
memory unbind attempts inside a guard are ignored, as runtime unbind attempts
inside calls already are. Guards nest, and failure paths release them. Owned
address spaces cannot close while bound. Borrowed legacy mappings must still
be kept alive by their owner; guards cannot prevent direct owner misuse.

Tick frequency matches upstream's 19,200,000 Hz. Tick reads use host
CLOCK_MONOTONIC; integer signed conversions truncate toward zero and reject
unrepresentable results. Sleep uses nanosleep with remaining-time retries after
EINTR; nonpositive durations and YieldThread use the host scheduler's yield.
Void sleep/yield calls preserve registers. This is a host timing implementation,
not the console's affinity/priority, suspend/resume, deterministic-clock or
thread scheduling model. No random, heap allocation, file or full
synchronization services are included yet.

Registry export collisions remain errors by default, including when an actual
SDK module exports a name also provided here. Explicit per-name provider
preferences now permit selected collisions; there is no silent host override or
automatic weak/global precedence policy. Existing
Windows implementations remain unchanged. Actual game use is unverified.

Generated `guest_trap`, `guest_unimpl` and `guest_svc` now link to fatal iOS
diagnostics. Unsupported kernel calls remain unsupported and abort rather than
returning fabricated results. This does not implement guest exception unwinding.
The CLI-only diagnostic modes are tested in subprocesses with core files disabled;
the normal iOS probe never intentionally executes a fatal diagnostic.

## Explicit provider preferences for SDK/host coexistence

Strict `ssbu_ios_modules_init` is unchanged: duplicate export names are errors.
For intentional replacement, supply a byte-sorted `SSBUIOSProviderPreference`
array to `ssbu_ios_modules_init_preferred`, for example:

```c
const SSBUIOSProviderPreference preferences[] = {
    {"memcpy", "ios-host-services"},
    {"memset", "ios-host-services"},
};
```

Every preferred module must exist and export the exact name. Duplicate/unsorted
rules, unknown providers/names and any unlisted export collision fail before
publishing a registry. Module name and guest-range collisions remain errors.
Selection is independent of registration order, with no wildcard names,
first-provider wins, or automatic weak-definition precedence. Preferences and
strings are borrowed/immutable for the registry and runtime lifetime. The caller
must ensure the selected implementation's signature and semantics are compatible.

`ssbu_ios_modules_resolve` applies preferences to exported names, including
undefined ELF/runtime imports. To opt into definition replacement during ELF
loading, set `SSBUIOSGuestLinker.preempt = ssbu_ios_modules_preferred`.
Only default-visible GLOBAL/WEAK ordinary definitions use this callback;
LOCAL, internal, hidden, protected and SHN_ABS definitions keep their original
values. Omitting the callback preserves prior image-local definition behavior.
The callback returns 0 for a selected guest VA, 1 for no rule, or -1 on error;
errors leave the arena/entry outputs unpublished. This is a restricted explicit
policy, not a complete platform dynamic linker. The visibility boundaries follow
[ELF symbol visibility](https://gabi.xinuos.com/v42/elf/05-symtab.html).

Runtime `DynSym` has no visibility field. `SSBUIOSModuleSymbols.preemptible` is
therefore an optional immutable byte per symbol index: 1 authorizes applying a
preference to that function definition, 0 keeps it local. NULL keeps all defined
symbols local. Undefined imports still use normal preferred export resolution.
The preparer emits these bytes only for default-visible GLOBAL/WEAK FUNC
definitions and NOTYPE definitions corresponding to lifted function entries;
OBJECT/protected/hidden/internal/local definitions are not authorized. Invalid
flags or a flag on an undefined symbol are rejected at runtime creation.
Regenerate metadata/rebuild archives to preserve these rules when using the
updated portable headers; no inference from symbol names or last-called PCs
is used.

Tests deliberately register both a synthetic SDK memcpy and the real portable
provider. They verify default rejection, invalid/unsorted rules, unlisted
collisions, order-independent selection, undefined and eligible-defined runtime
calls, local/legacy definitions, preserved ELF visibility/absolute definitions,
callback failure rollback, and generated eligibility flags. No actual Smash
SDK replacement or game compatibility has been validated.
