# Filesystem access (sandbox elevation)

From [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
`examples/sandbox-elevation`, as ProsperoEden carries it (GPL-3.0-or-later). The app side is in
`src/elevation` (`elevation.hpp`, `protocol.hpp`, `elevation.cpp`); this folder has the helper the
console's ELF loader runs (`helper/`) and the check of its file (`validate-helper.py`).

ProsperoLight changes: the helper's `target_title_id` is `PPSA99002`, and it reads `protocol.hpp`
from `src/elevation`. The build makes the helper with the PS5 payload SDK and ships it as
`sandbox-elevator.elf` beside `eboot.bin`. `storage::Initialize()` (`src/app_storage.cpp`) asks
for `Capability::filesystem` once at startup, and with it the app keeps everything it writes
under `/data/prosperolight`. Without an ELF loader on the console the request fails and the app
keeps its sandbox paths (`/app0`, `/download0`).
