# Native build tooling

The normal build has no C# or .NET dependency. Repository-owned host tools are
C++20 and compile natively on Linux/WSL or through WSL from PowerShell.

## Division of responsibility

LLVM/Clang and lld handle the standard work that mature native tools already
solve: C/C++ compilation, archives, COMDAT, symbol resolution, TLS, unwind
records, and x86-64 relocations. The repository-owned converter handles only
PS5-specific output requirements:

- FreeBSD OS ABI 9, ABI version 2, and executable type `0xFE10`;
- execute-only, read-only, RELRO, writable, and flags-zero linking segments;
- process-parameter and parameter-block records;
- SDK import discovery from the installed public `.so` stubs;
- PS5 NIDs, module/library IDs, SysV hash, and dynamic tags;
- development FSELF wrapping and integrity metadata.

The converter reads imports directly from the PIE and the SDK stubs. It does
not contain a copied import catalog or firmware offsets.

Native third-party libraries are allowed when they solve a standard problem
and the build can acquire and link them reproducibly. zlib is used directly by
the FSELF tool; LLVM/lld handles standard object linking. This boundary keeps
the repository free of managed build tooling without reimplementing mature
native libraries such as zlib or OpenSSL. zlib 1.3.2 is downloaded from its
fixed upstream archive, SHA-256 verified, and compiled into `.deps/native/`;
the build no longer depends on a changing distribution package.

## Files

| File | Purpose |
| --- | --- |
| `tooling/native/app_crt.cpp` | Native C++ process startup and constructor handling |
| `tooling/native/app_cpp_runtime.cpp` | Minimal `new`/`delete` bridge to `malloc`, `free`, and `posix_memalign` |
| `tooling/native/app-symbols.map` | Keeps replacement allocation operators internal to the application |
| `tooling/native/ps5-pie.ld` | Non-overlapping intermediate PIE layout |
| `tooling/native/elf_object.*` | ELF and SDK-stub reader |
| `tooling/native/sce_module_writer.*` | PS5 executable converter |
| `tooling/native/self_container.*` | FSELF reader, writer, and verifier |
| `tooling/native/libc_builder.cpp` | Deterministic clean-room runtime emitter |
| `tooling/native/hash.hpp` | Project-owned SHA-1/SHA-256 implementation |

## Build the host utility manually

The normal build invokes the dependency bootstrap automatically. For a manual
PowerShell build, use the returned WSL paths rather than a global library:

```powershell
$deps = ./tools/setup-native-dependencies.ps1 | ConvertFrom-Json
wsl.exe --exec clang++ -std=c++20 -O2 -Wall -Wextra -Werror `
  -I $deps.zlibInclude `
  tooling/native/native_app_builder.cpp `
  tooling/native/self_container.cpp `
  tooling/native/elf_object.cpp `
  tooling/native/sce_module_writer.cpp `
  $deps.zlibArchive -o build/ps5-native-tool
```

zlib is the only directly linked host library.

## Target C++ profile

Application `.cpp` files compile as C++20 with exceptions and RTTI disabled.
The public SDK's libc++ headers provide the C++ library. The launcher's OpenGL
runtime is linked statically, together with the SDK's libc++, libc++abi and
unwind archives (`tools/prepare-opengl.sh` writes the link group). That runtime
needs a process heap of its own: `src/runtime/app_heap.c` is linked with
`--wrap` for the malloc family, so every allocation of the executable goes to a
128 MiB arena that is never unmapped and continues on the libc heap when the
arena is full. Repository-owned replacement allocation operators use that heap
for small objects and anonymous page mappings for allocations of 64 KiB or
more, with at least 32-byte alignment. The operators are localized before PS5
conversion, so they do not become loader-visible application exports.

Throwing `new` deliberately traps on allocation failure. Nothrow allocation
returns `nullptr`. Prefer value semantics and allocation-free RAII in steady
state, and do not introduce `std::shared_ptr`, streams, locale, filesystem,
exceptions, or RTTI without a separate runtime and firmware validation.

## Source quality

The repository uses the same focused Clang policy as the CPython PS5 project:
`make format-check` enforces `.clang-format`, while `make tidy` runs the Clang
core and security analyzers configured by `.clang-tidy`. `make lint` runs both
plus attribution, JSON, shell-syntax, local-path, and whitespace checks.

## Commands

```text
ps5-native-tool link --in <llvm-pie> --out <ps5-elf> --stub-dir <sdk-lib>
ps5-native-tool self --sign --in <ps5-elf> --out <fself>
ps5-native-tool self --extract --file <fself> --out <ps5-elf>
ps5-native-tool self --inspect --file <module>
```

`link` expects the repository linker script’s page-separated PIE. The normal
build invokes it correctly; the command is documented for debugging and CI.

## Current hardware-validation baseline

A clean build of the current C++20 graphical Hello World was deployed with
`make deploy`, launched, observed, and closed on firmware 6.02 and 12.70. The
exact tested artifacts are:

```text
Raw ELF SHA-256:  6198c82adf9d59f2ae14de4e1410e2482376afc9f9ee148f3d8cf406a952c0ff
FSELF SHA-256:    d791f65c5a62aec4d70c4e9c6b8b0baeeefbf51a41dd774287e1353f66af2966
Runtime SHA-256:  e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036
```

On 6.02 the captured display showed the complete CPU VideoOut scene and the
packaged `/app0/assets/banner.txt` content. On both consoles klog recorded
native `eboot` execution without a loader, fatal-signal, or crash marker. The
12.70 run also recorded transition into `AppScreen`. Title-aware closure was
confirmed by ShadowMount/KStuff, and FTP, klog, and elfldr remained reachable
after both runs.

The loader-visible comment record and the unmapped trailing note intentionally
have zero memory size. Firmware 6.02 rejects those records before entry when
their file size is incorrectly copied into `p_memsz`; the static validator
enforces the tested convention.
