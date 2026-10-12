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
  and rejection of overlapping module ranges, duplicate names/exports,
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
- A host CLI smoke test, host ThreadSanitizer CI check and ARM64 iOS syntax check.
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
  ios/Portability/ProbeAddressSpace.c \
  ios/tests/aot_fixture/synthetic_game.c ios/tests/aot_fixture/synthetic_sdk.c \
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
TLS/IFUNC/COPY and packed REL/RELR relocations, symbol interposition, initialization/finalization,
GPU execution and section permissions are unsupported. The upstream
`call_indirect`/`host_import` implementation is not yet wired to this registry.
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
names across modules are rejected rather than silently selecting one. The
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
NSO/NRO module ingestion or Win32 service integration has been validated.
