#!/usr/bin/env python3
# ProsperoLight - Isolate the OpenGL SDK's Mesa symbols from RADV.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Rewrite SDK archives into a build-local cache; never modify verified inputs."""
import hashlib
import re
from pathlib import Path
import shutil
import subprocess
import sys


def fingerprint(digest, path):
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)


def is_archive(path):
    with path.open("rb") as stream:
        return stream.read(8) == b"!<arch>\n"


def symbols(archives):
    result = set()
    for archive in archives:
        output = subprocess.check_output([
            "llvm-nm-18", "--defined-only", "--extern-only", "--format=posix", str(archive)
        ], text=True)
        for line in output.splitlines():
            fields = line.split()
            if len(fields) > 2 and fields[1] in "TDBRSGCVW":
                result.add(fields[0])
    return result


def isolate(prefix, radv, destination):
    source = prefix / "lib"
    archives = sorted(p for p in source.glob("*.a") if is_archive(p))
    if not archives:
        raise RuntimeError("OpenGL SDK contains no ELF archives")
    digest = hashlib.sha256(Path(__file__).read_bytes())
    for path in [radv, *sorted(source.glob("*.a"))]:
        digest.update(path.name.encode())
        fingerprint(digest, path)
    key = digest.hexdigest()
    stamp = destination / "input.sha256"
    if stamp.exists() and stamp.read_text().strip() == key:
        return destination
    # Standard-library templates share the single chosen C++ ABI. Mesa's own
    # weak templates/type metadata must be isolated alongside its strong code.
    collisions = symbols(archives) & symbols([radv])
    collisions = {name for name in collisions if not name.startswith(
        ("_ZNSt", "_ZNKSt", "_ZSt", "_ZTVSt", "_ZTISt", "_ZTSSt", "_ZTVNSt", "_ZTINSt", "_ZTSNSt", "_ZTTNSt"))}
    if any(re.match(r"(?:gl|egl|vk)[A-Z]", name) for name in collisions):
        raise RuntimeError("Unexpected public graphics API collision")
    destination.mkdir(parents=True, exist_ok=True)
    mapping = destination / "mesa-symbols.txt"
    mapping.write_text("".join(f"{name} pl_gl_{name}\n" for name in sorted(collisions)))
    for path in sorted(source.glob("*.a")):
        output = destination / path.name
        if path in archives:
            subprocess.run(["llvm-objcopy-18", f"--redefine-syms={mapping}", str(path), str(output)], check=True)
            subprocess.run(["llvm-ranlib-18", str(output)], check=True)
        else:
            # SDK .a linker groups name their members relative to SEARCH_DIR.
            shutil.copyfile(path, output)
    stamp.write_text(key + "\n")
    print(f"Isolated {len(collisions)} OpenGL/Mesa symbols from RADV", file=sys.stderr)
    return destination


def resolve_libraries(flags):
    directories = [Path(flag[2:]) for flag in flags if flag.startswith("-L")]
    archives = {Path(flag).resolve() for flag in flags if not flag.startswith("-") and Path(flag).is_file()}
    for flag in flags:
        if not flag.startswith("-l"):
            continue
        for folder in directories:
            path = folder / ("lib" + flag[2:] + ".a")
            if path.is_file():
                archives.add(path.resolve())
                break
        else:
            raise RuntimeError(f"Cannot resolve runtime archive {flag}")
    return sorted(archives)


def isolate_runtime(radv, libraries, destination):
    """RADV also embeds zlib/zstd; keep libcurl's copies independent."""
    archives = [p for p in libraries if p.is_file() and p.suffix == ".a"]
    digest = hashlib.sha256(Path(__file__).read_bytes())
    for path in [radv, *archives]:
        digest.update(str(path).encode())
        fingerprint(digest, path)
    key = digest.hexdigest()
    output = destination / "radv-isolated.a"
    stamp = destination / "radv-input.sha256"
    if output.exists() and stamp.exists() and stamp.read_text().strip() == key:
        return output
    collisions = symbols([radv]) & symbols(archives)
    mapping = destination / "radv-runtime-symbols.txt"
    mapping.write_text("".join(f"{name} pl_radv_{name}\n" for name in sorted(collisions)))
    subprocess.run(["llvm-objcopy-18", f"--redefine-syms={mapping}", str(radv), str(output)], check=True)
    subprocess.run(["llvm-ranlib-18", str(output)], check=True)
    stamp.write_text(key + "\n")
    print(f"Isolated {len(collisions)} RADV runtime symbols from launcher dependencies", file=sys.stderr)
    return output


if __name__ == "__main__":
    prefix, radv, destination = (Path(arg).resolve() for arg in sys.argv[1:4])
    print(isolate(prefix, radv, destination))
    print(isolate_runtime(radv, resolve_libraries(sys.argv[4:]), destination))
