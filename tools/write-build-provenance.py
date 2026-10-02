#!/usr/bin/env python3
# ps5-native-app-boilerplate / ProsperoLight - Build receipt outside the installed app.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    return subprocess.check_output(args, cwd=ROOT, text=True).strip()


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    executable, destination = map(Path, sys.argv[1:])
    sdk = ROOT / ".deps/native/ps5-payload-sdk"
    llvm = os.environ["LLVM_CONFIG"]
    bindir = Path(run(llvm, "--bindir"))
    # Exact source contents, including local work; no patch payloads or user data.
    source_paths = subprocess.check_output(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z",
         "src", "include", "platform", "tools", "tooling", "Makefile", "sce_sys/param.json"],
        cwd=ROOT,
    ).decode().split("\0")
    source_hash = hashlib.sha256()
    for name in sorted(set(filter(None, source_paths))):
        path = ROOT / name
        source_hash.update(name.encode() + b"\0")
        source_hash.update((digest(path) if path.is_file() else "deleted").encode())
    inputs = [ROOT / p for p in os.environ.get("APP_STATIC_ARCHIVES", "").split()]
    inputs += [ROOT / "runtime/libc.prx", ROOT / "tools/setup-native-dependencies.sh"]
    inputs += sorted((sdk / "target/lib").glob("*.so"))
    receipt = {
        "schema": 1,
        "commit": run("git", "rev-parse", "HEAD"),
        "source_sha256": source_hash.hexdigest(),
        "submodules": run("git", "submodule", "status", "--recursive").splitlines(),
        "definitions": os.environ.get("APP_DEFINITIONS", "").split(),
        "llvm_version": run(llvm, "--version"),
        "compiler_version": run(os.environ["PS5_CLANG"], "--version").splitlines()[0],
        "app_compiler_sha256": digest(os.environ["PS5_CLANG"]),
        "sdk_compiler_version": run(str(sdk / "bin/prospero-clang"), "--version").splitlines()[0],
        "tool_sha256": {name: digest(bindir / name) for name in ("clang", "ld.lld", "llvm-ar")},
        "input_sha256": {str(p.relative_to(ROOT)): digest(p) for p in inputs},
        "stream_dependencies_key": (ROOT / "build/stream-deps/options").read_text().strip(),
        "eboot_sha256": digest(executable),
    }
    patch = ROOT / "tools/pyrowave/patches/moonlight/0001-vibepollo-independent-record-protocol.patch"
    receipt["moonlight_protocol_patch_sha256"] = digest(patch)
    if "PROSPEROLIGHT_PYROWAVE=1" in receipt["definitions"]:
        pins = {}
        for line in (ROOT / "tools/pyrowave/pins.sh").read_text().splitlines():
            if "_REV=" in line:
                name, revision = line.split("=", 1)
                pins[name] = revision
        archives = [Path(line) for line in (ROOT / "build/pyrowave/archives.txt").read_text().splitlines() if line]
        archives.append(ROOT / ".deps/pyrowave/PS5_Vulkan/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a")
        receipt["pyrowave"] = {"pins": pins, "bitstream": "186f0393",
            "archives": [{"name": p.name, "sha256": digest(p)} for p in archives],
            "wsi_patches": {p.name: digest(p) for p in sorted((ROOT / "tools/pyrowave/patches/radv").glob("*.patch"))},
            "shared_scanout_header_sha256": digest(ROOT / "include/ps5_videoout_formats.h")}
    destination.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(f"Build provenance: {destination}")


if __name__ == "__main__":
    main()
