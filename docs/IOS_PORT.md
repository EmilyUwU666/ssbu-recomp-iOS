# iOS / iPadOS static-recompilation port — checkpoint 0

Status: **bootstrap only, no gameplay, no IPA, and no signed device build.**
The existing Windows port in this fork is kept intact.

## What this branch provides

- A minimal native SwiftUI iOS application with a C interoperability bridge.
- A C portability probe that includes the **real upstream** `runtime/runtime.h`
  and verifies guest context layout, memory accessors, bit operations,
  SIMD helpers, signed arithmetic, and conversions.
- A host CLI smoke test and an ARM64 iOS cross-compilation syntax check.
- A GitHub Actions macOS runner that generates an Xcode project with
  XcodeGen and builds an **unsigned simulator application**.

The simulator artifact is **not** a device IPA and cannot play the game.
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
clang -std=c11 -O2 ios/Portability/Probe.c ios/Portability/ProbeCli.c -o /tmp/ssbu-probe
/tmp/ssbu-probe
```

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

- **0 — Current**: Xcode bootstrap, shared C helper checks, simulator build.
- **1**: portable platform abstraction for guest memory and file paths;
  tests with generated synthetic guest data (not game code).
- **2**: AOT game + module linking from an owner's local dump with reproducible
  device-build tooling; resolve symbols, unwind support, and loading.
- **3**: implement iOS rendering and present a reproducible frame.
- **4**: game startup, input, audio, videos, save paths, and playable tests.
- **5**: performance evaluation, packaging, and optional device distribution.

All claims should be backed by build logs or device captures. In particular,
successfully compiling one runtime header must not be described as compiling
the actual game.

## Legal and distribution

This branch contains no game assets, binaries derived from a game dump,
console keys, firmware, or redistributed Nintendo content. The game requires
the user to supply their own lawfully obtained files. Apple TestFlight review,
code-signing requirements, and intellectual-property restrictions still apply.
