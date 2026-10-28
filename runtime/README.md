# Clean-room runtime shim

`libc.prx` is generated locally from the independently authored source in this
repository and is distributed under GPL-3.0-or-later. It contains no Sony
runtime implementation, proprietary SDK binary, or game file.

The release artifact has SHA-256:

```text
48427d4447d9b8cfa344d0f191ea8cc29ee9a25a60628180e50d161b0ca9b575
```

Generate it from the repository root:

```sh
make libc
```

Then verify it from this directory with:

```sh
sha256sum -c libc.prx.sha256
```

The generated file is ignored by Git. Bare `make` also creates it as part of a
normal application build. Tagged GitHub Releases provide the verified binary
as a convenience asset.

The complete source, reproduction procedure, and compatibility scope are in
[`tooling/native`](../tooling/native) and
[`docs/RUNTIME_SHIM.md`](../docs/RUNTIME_SHIM.md).
