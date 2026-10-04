# Third-party notices

## Credits and acknowledgements

ProsperoLight exists thanks to the maintainers and contributors of:

- [Moonlight](https://github.com/moonlight-stream/moonlight-common-c) and
  [Sunshine](https://github.com/LizardByte/Sunshine) for the open streaming
  protocol ecosystem;
- [PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
  and the [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) for the
  reproducible native foundation and public target integration;
- [ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl) and
  [Mesa](https://www.mesa3d.org/), which the launcher draws with;
- [mbedTLS](https://github.com/Mbed-TLS/mbedtls) and
  [Opus](https://github.com/xiph/opus) for secure protocol and audio support;
- [MkPFS](https://github.com/PSBrew/MkPFS),
  [UFS2Tool](https://github.com/SvenGDK/UFS2Tool), LLVM/Clang, Python, zlib,
  and GoogleTest for build, packaging, and validation tooling.

The original ProsperoLight artwork and selection music are distributed under
the project license.

## Native build dependencies

The application build uses LLVM/Clang/lld, zlib 1.3.2, and the public
[PS5 payload SDK](https://github.com/ps5-payload-dev/sdk). The bootstrapper
downloads SDK v0.42 after verifying SHA-256
`8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da`.
It downloads zlib 1.3.2 from the upstream source archive after verifying
SHA-256 `bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16`
and compiles its static archive locally. Both dependencies remain under ignored
`.deps/native/`, retain their upstream licenses, and are not distributed by
this repository. No Sony SDK file is included.

Target C++ compilation uses the LLVM libc++ headers distributed by the public
SDK. Those headers retain the Apache-2.0 WITH LLVM-exception license recorded
upstream. The application statically links only the required portions of the
corresponding libc++ and libc++abi archives; it does not dynamically load the
complete archives.

The PS5 ELF converter and FSELF writer in `tooling/native/` are derived from
[SharpProspero](https://github.com/SvenGDK/SharpProspero), Copyright (C) 2026
SvenGDK, GPL-3.0, and were translated to C++ and modified by BlackBearReloaded.

## Host test dependency

The host unit-test target downloads
[GoogleTest](https://github.com/google/googletest) 1.17.0 after verifying
SHA-256 `65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c`.
It remains under ignored `.deps/test/`, retains its BSD-3-Clause license, and
is not linked into any PS5 application, runtime, or package artifact.

## Optional PacBrew dependencies

When selected through `PACBREW_*` build variables, the build downloads the prebuilt ports image
from [ps5-payload-dev/pacbrew-repo](https://github.com/ps5-payload-dev/pacbrew-repo)
release `v0.40.2`, verifies its published SHA-256, and extracts only the
`target/user/homebrew` prefix under ignored `.deps/pacbrew/`. It does not
replace the pinned SDK or install files globally. PacBrew recipes and every
linked third-party library retain their upstream licenses; applications must
review those terms before redistribution.

## Optional UFS2Tool dependency

When `.ffpkg` output is requested, the platform bootstrapper fetches
[SvenGDK/UFS2Tool](https://github.com/SvenGDK/UFS2Tool) at commit
`b5307a60d5b4e3a68ba680e0e33cfadf05017c77` into the ignored
`.deps/UFS2Tool` cache and builds it with the host .NET SDK. UFS2Tool is
BSD-2-Clause software and is not distributed by this repository.

## Optional MkPFS dependency

When `.ffpfsc` output is requested, the platform bootstrapper fetches
[PSBrew/MkPFS](https://github.com/PSBrew/MkPFS) at commit
`6cb8313dfe0c988ac52617794553f343243d3a56` into the ignored `.deps/MkPFS`
cache and installs its Python dependencies into an ignored virtual environment
there. MkPFS and its dependencies retain their own licenses and are not
distributed by this repository.

## Independently authored runtime shim

`tooling/native/libc_builder.cpp` and the manifests under
`tooling/native/runtime/` are independently authored for this project and
licensed under GPL-3.0-or-later. The generated `runtime/libc.prx` contains
project-authored compatibility stubs, startup code, and semantic loader
metadata. It contains no Sony runtime implementation.

Original ps5-native-app-boilerplate code is Copyright (C) 2026
BlackBearReloaded and licensed under GPL-3.0-or-later. Source and script files
carry matching SPDX identifiers.

## Original presentation assets

The BlackBear icon, selection artwork, and default selection track
`sce_sys/snd0.at9` are original assets supplied by BlackBearReloaded, Copyright
(C) 2026 BlackBearReloaded, and distributed under GPL-3.0-or-later. The track
is titled `Night Drive`.

No proprietary runtime module, encryption key, or game file is included.

## ProsperoLight streaming dependencies

The streaming protocol and decoder integration use pinned source revisions of
[moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c),
[mbedTLS](https://github.com/Mbed-TLS/mbedtls), and
[Opus](https://github.com/xiph/opus). Their licenses are retained in the
corresponding `third_party/` source trees. None of these dependencies is
claimed to be project-authored.

## Update check

`third_party/update-check/` is the update check of
[ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
(`examples/update-check`), Copyright (C) 2026 BlackBearReloaded,
GPL-3.0-or-later. It asks the [homebrew.page](https://homebrew.page) catalog
through libcurl, which the build takes from the pinned PacBrew release
v0.40.2 and links statically with its dependencies. Their licence texts are in
`third_party/licenses/`:

| Component | Version | Licence | Text |
| --- | --- | --- | --- |
| libcurl | 8.18.0 | curl licence (MIT/X derivative), Copyright (C) Daniel Stenberg and contributors | `curl.txt` |
| OpenSSL | 3.5.2 | Apache License 2.0, Copyright (C) The OpenSSL Project Authors | `openssl.txt` |
| zlib | 1.3.2 | zlib licence, Copyright (C) Jean-loup Gailly and Mark Adler | `zlib.txt` |
| zstd | 1.5.6 | BSD-3-Clause, Copyright (C) Meta Platforms, Inc. and affiliates | `zstd.txt` |
| libpsl | 0.21.5 | MIT, Copyright (C) Tim Rühsen and contributors; its Public Suffix List data is MPL-2.0 | `libpsl.txt` |

## Launcher artwork

PNG box art is decoded by static libpng 1.6.43 from the pinned PacBrew v0.40.2
sysroot. It uses the PNG Reference Library License; see
`third_party/licenses/libpng.txt`. This avoids unavailable system PNG imports
on some PS5 launch environments. Scaling and the 64 MiB pixel allocation limit
remain in the application worker.

## Launcher

The launcher is drawn with OpenGL through the
[ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl) SDK 1.0.0
(GPL-3.0-or-later), which the build downloads after verifying SHA-256
`f93643c04c843d56143b00951df1f8042ea7706ae9f19e4e9abf158e1ead77c5` and links
statically. The SDK contains Mesa 26.2.0 (primarily MIT, with file-specific
licences), OpenGNM and OpenGNM PSBC (MIT), SPIRV-Headers and Vulkan-Headers;
its own `LICENSE`, `LICENSES/` and `THIRD_PARTY_NOTICES.md` are unpacked beside
it under ignored `.deps/ps5-opengl/` and are not distributed by this
repository. `src/runtime/app_heap.c` and `src/runtime/runtime_shims.c` are
adapted from that SDK's native application recipe.

`third_party/ps5-homebrew-ui/` is the UI kit the launcher's widgets come from:
ps5-homebrew-ui, Copyright (C) 2026 BlackBearReloaded, GPL-3.0-or-later. Its
shader distance functions follow Inigo Quilez's published formulas (MIT).

The launcher's fonts in `assets/fonts/` are distance-field renderings, made
by `tools/bake-fonts.sh`, of the typefaces in `third_party/fonts/`: Inter and
Montserrat (SIL Open Font License 1.1) and DejaVu Sans Mono (Bitstream Vera
licence). Their licence texts are kept beside both. The baker and the PC
harness use `stb_truetype.h` and `stb_image_write.h` from
[stb](https://github.com/nothings/stb) (public domain or MIT); neither is part
of the application.

The launcher's sounds in `assets/audio/sfx/glass/` are original recordings by
BlackBearReloaded, generated with ElevenLabs Sound Effects, Copyright (C) 2026
BlackBearReloaded, and distributed under GPL-3.0-or-later.

The small sources in `vendor/ps5/sdk/stubs/` are linker-only import
descriptions for public PS5 system modules missing from the bundled SDK stub
set. They are compiled during the host build, are not packaged, and do not
contain a Sony implementation.

## PyroWave/RADV backend

Linked into the default build (`PYROWAVE=1`); `make PYROWAVE=0 app` leaves it out. The
licence texts of PyroWave, Granite, Volk, the Vulkan headers and Mesa are copied into the
app's `licenses/` folder when it is built.

- PyroWave: Themaister/pyrowave, Copyright (c) 2025 Hans-Kristian Arntzen, MIT, pinned to 186f0393b77f7755953b5ecde994bb1cec2e4155.
- Granite: Themaister/Granite, Copyright (c) 2017-2026 Hans-Kristian Arntzen, MIT, pinned to b6cffd5ce81f540f0855e6778428483e14763d9b.
- Volk: zeux/volk, Copyright (c) 2018-2026 Arseny Kapoulkine, MIT; Vulkan-Headers: KhronosGroup/Vulkan-Headers, The Khronos Group Inc., Apache-2.0 OR MIT.
- RADV/Mesa: Mihawk-99/PS5_Mesa, the Mesa 26.2.0 release with a PS5 winsys; Mesa's own licences (MIT for the parts linked here; `docs/license.rst` in that tree), Copyright (C) the Mesa authors.
- PS5 build/link integration: Mihawk-99/PS5_Vulkan, GPL-3.0-or-later.
- PayloadSDK platform fork: Mihawk-99/PS5_PayloadSDK, GPL-3.0-or-later; its platform layer includes Doug Lea's dlmalloc (public domain).
- PyroWave framing parser: derived from Nonary/moonlight-qt revision 5f9ce4a46d2b8fd2191f47cef043bc43f3d772d0, GPL-3.0-or-later. Original parser retained, with provenance headers; no Windows/D3D interop copied.
- moonlight-common-c: the original upstream submodule pin f900dd4767759c7b9d0e93bcea666b55c69ea62f is retained with a minimal protocol patch derived from Nonary revisions 927eba9639f3c9b5037ab45fc0d6486e2a19b39f and 3bbe8e818b42a8a1dc3a1381149b7e818fbd33fd, preserving its LICENSE.txt. No second transport implementation.

The decoder/context/renderer are adapted from the preceding MIT-licensed PS5 PyroWave PoC. Source license headers are preserved. Dependency pins and local patches are documented in docs/PYROWAVE.md and tools/pyrowave/. Build caches and binaries are not source deliverables.
