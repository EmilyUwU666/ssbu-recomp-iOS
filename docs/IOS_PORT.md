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
  This is not a complete game/NSO/NRO loader and does not execute ELF code.
- A host CLI smoke test and an ARM64 iOS cross-compilation syntax check.
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

On macOS, the C-only probe can also run without XcodeGen:

```sh
clang -std=c11 -O2 ios/Portability/Probe.c ios/Portability/GuestMemory.c \
  ios/Portability/GuestMemoryBinding.c ios/Portability/ProbeTranslated.c \
  ios/Portability/ProbeCli.c -o /tmp/ssbu-probe
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
Win32 virtual mappings, cross-module pointers, unwinding, guest SDK calls, and
guest/memory lifetime management are outside its scope. The ELF loader
currently supports ET_DYN, PT_LOAD and bounded DT_RELA-relative relocations;
imports, NSO/NRO conversion, other relocation kinds, GPU execution, section
permissions and executable entry dispatch are unsupported. The current bridge
terminates on an invalid translated access rather than dereferencing host
address 0, and the prototype binder must be unbound before memory is freed.

All claims should be backed by build logs or device captures. In particular,
successfully compiling one runtime header must not be described as compiling
the actual game.

## Legal and distribution

This branch contains no game assets, binaries derived from a game dump,
console keys, firmware, or redistributed Nintendo content. The game requires
the user to supply their own lawfully obtained files. Apple TestFlight review,
code-signing requirements, and intellectual-property restrictions still apply.
