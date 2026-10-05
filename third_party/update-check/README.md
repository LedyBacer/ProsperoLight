# Update check and self-update

From [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
at f98de73 (GPL-3.0-or-later): `examples/update-check` (`update_check.*`, `console_curl.*`) and
`examples/self-update` (`self_update*.{h,c}`). The guides are the boilerplate's
`docs/UPDATE_CHECK.md` and `docs/SELF_UPDATE.md`.

`self_update.c` and `self_update.h` are ProsperoEden's copies: `self_update_check` also puts the
catalog entry's `release_notes` and `release_notes_truncated` into the offer (`notes`,
`notes_truncated`), for the update dialog's What's new view.

Two more files differ from the boilerplate's:

- `console_curl.c` leaves out the functions ProsperoLight already defines (listed at the top
  of the file).
- `self_update_ps5.c` includes `self_update_paths.h`, ProsperoLight's own, which names the
  helper, the app's `param.json` and the sequence file: with filesystem access the app's
  folder is not `/app0` and its settings are not in `/download0`.

The helper the app sends to the console's payload loader is in `third_party/self-update-helper`
(the boilerplate's `examples/self-update-helper`, with its Makefile's source folders changed);
it reads ZIP archives with `third_party/miniz` (MIT). `make self-update-helper` builds
`build/self-update/self-updater.elf`, which every package carries. `make test-self-update` runs
the engine against the helper on the PC (`third_party/self-update-helper/test_self_update.cpp`).

The build links PacBrew's libcurl (`PACBREW_PACKAGES`) and wraps `fcntl`. The launcher asks once
per launch on its worker thread (`CheckForUpdate` in `src/launcher/launcher_ps5.cpp`); the view
shows the offer, the progress and a failure (`update_modal` in `src/launcher/launcher_view.cpp`),
and `main` closes the app once the update is staged.

For trying it before a release is listed: a build made with `UPDATE_DEV_OFFER=1` takes the
offer from `update-offer.txt` in the app's folder (five lines: new content version, release
name, ZIP on GitHub, SHA-256, size; further lines are the release notes) instead of the catalog. It skips the catalog's signature:
never ship such a build. `UPDATE_AUTO_ACCEPT=<seconds>` makes it accept the offer by itself.
