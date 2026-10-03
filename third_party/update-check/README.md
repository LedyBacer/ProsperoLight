# Update check

From [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
`examples/update-check` at 1dbe974 (GPL-3.0-or-later): `update_check.h`, `update_check.c`,
`console_curl.h` and `console_curl.c`. The guide is the boilerplate's `docs/UPDATE_CHECK.md`.

`update_check.*` and `console_curl.h` are unchanged. `console_curl.c` leaves out the functions
ProsperoLight already defines (listed at the top of the file).

The build links PacBrew's libcurl (`PACBREW_PACKAGES`) and wraps `fcntl`. The launcher runs the
check once per launch on its worker thread (`CheckForUpdate` in `src/launcher/launcher_ps5.cpp`)
and shows a notice for ten seconds when the catalog lists a newer version.
