#!/usr/bin/env python3
"""Compile local generated recompilation C sources to an iOS ARM64 static archive.

This does NOT link a playable game or add an asset loader, runtime or renderer.
Inputs derived from a game dump must remain on the developer's own machine.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess
import sys


REPO = Path(__file__).resolve().parents[2]


def command(argv: list[str]) -> None:
    print("+", " ".join(argv), flush=True)
    subprocess.run(argv, check=True)


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--generated", type=Path, required=True,
                   help="Private directory of generated C files and headers")
    p.add_argument("--output", type=Path, required=True,
                   help="Private output directory for .o and archive files")
    p.add_argument("--jobs", type=int, default=4,
                   help="Maximum simultaneous clang processes")
    return p.parse_args()


def main() -> int:
    args = parse_args()
    if args.jobs < 1 or args.jobs > 64:
        raise ValueError("--jobs must be between 1 and 64")

    generated = args.generated.resolve()
    out = args.output.resolve()
    sources = sorted(generated.glob("*.c"))
    if not sources:
        raise FileNotFoundError(f"No generated .c files found under {generated}")

    obj_dir = out / "objects"
    obj_dir.mkdir(parents=True, exist_ok=True)
    runtime = REPO / "runtime"
    headers = [*generated.glob("*.h"), runtime / "runtime.h",
               runtime / "neon.h", runtime / "fpexact.h"]
    latest_headers = max((p.stat().st_mtime for p in headers), default=0.0)

    common = [
        "xcrun", "--sdk", "iphoneos", "clang", "-c",
        "-arch", "arm64", "-miphoneos-version-min=16.0",
        "-std=gnu11", "-O0", "-w", "-fno-strict-aliasing",
        "-DSSBU_GUEST_ADDRESS_TRANSLATION=1",
        "-I", str(runtime), "-I", str(generated),
    ]

    def compile_one(src: Path) -> Path:
        obj = obj_dir / (src.stem + ".o")
        if not obj.exists() or obj.stat().st_mtime < max(src.stat().st_mtime, latest_headers):
            # Same source basename must be unique (upstream gen output is flat).
            command(common + [str(src), "-o", str(obj)])
        return obj

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        objects = list(pool.map(compile_one, sources))

    archive = out / "libssbu_generated_ios_arm64.a"
    # macOS ar supports @response files indirectly through libtool? Avoid
    # thousands of argv elements in a single command by constructing with
    # repeated ar append operations in batches. First delete stale archive.
    archive.unlink(missing_ok=True)
    for offset in range(0, len(objects), 100):
        batch = objects[offset: offset + 100]
        command(["xcrun", "--sdk", "iphoneos", "ar", "-r",
                 str(archive)] + [str(obj) for obj in batch])
    command(["xcrun", "--sdk", "iphoneos", "ranlib", str(archive)])

    print(f"Compiled {len(objects)} object files to {archive}", flush=True)
    print("NOTE: Archive only; no game code has been linked to an iOS app.", flush=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (subprocess.CalledProcessError, OSError, ValueError) as exc:
        print(f"AOT build failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
