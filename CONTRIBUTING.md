# Contributing

Keep ProsperoLight reproducible and preserve the native boilerplate workflow.

Before opening a change:

1. Run `make test` and `make lint`; add a focused unit or integration regression
   for behavior changed by the patch.
2. Run `make`; it must reproduce the clean-room `runtime/libc.prx` digest.
3. Confirm the build reports zero static FSELF errors.
4. Do not commit `.env`, `build/`, `dist/`, `.local/`, proprietary PRXs, game files, SDK
   binaries, generated `runtime/libc.prx`, console dumps, keys, or credentials.
5. Include the firmware and loader context for platform-specific behavioral
   claims.
6. Name release tags with the exact `sce_sys/param.json` `contentVersion`
   (`NN.NNN.NNN`, without a `v` prefix).

Every comment-capable code, script, workflow, tooling configuration, and
manifest must retain the project copyright and
`GPL-3.0-or-later` SPDX header. JSON and binary formats cannot carry comments;
their licensing is covered by `LICENSE` and `THIRD_PARTY_NOTICES.md`.

Changes to `tooling/native/` must include a deterministic host check and a
narrowly scoped static-format regression. Loader-visible changes also require
hardware results before release.
