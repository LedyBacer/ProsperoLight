# Filesystem access with upstream Lapy

ProsperoLight uses the cooperative owned-root protocol from
[ArkSama's PS5-Lapy-JB-Daemon](https://github.com/ArkSama/PS5-Lapy-JB-Daemon) and
[mpereiraesaa's PS5-Lapy-JB-Daemon](https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon).
Lapy is loaded separately on the console and performs the privileged operation. ProsperoLight
only publishes the requesting process ID through `/download0/elevate_proc`, waits for the
result, and proves that `/data` is readable and writable before using it.

The application-side request follows Lapy's upstream
[`cooperative_elevation.c`](https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon/blob/main/examples/cooperative_elevation.c)
example. The result handshake and `/data` proof follow its upstream cooperative test app. The
request file is written to a temporary file and atomically renamed so Lapy cannot observe a
partial JSON document.

ProsperoLight does not bundle, fork, or reimplement the Lapy daemon. In particular, the package
contains no `sandbox-elevator.elf`, kernel offsets, credential mutation, file-descriptor-table
mutation, or elfldr connection. Use an official upstream Lapy owned-root build in one-shot or
resident-service mode. If no compatible Lapy service is waiting, the request times out and the
app stays on `/app0` and `/download0` paths.

Credits for Lapy and the cooperative elevation design go to
[ArkSama and the PS5-Lapy-JB-Daemon contributors](https://github.com/ArkSama/PS5-Lapy-JB-Daemon/graphs/contributors)
and
[mpereiraesaa and the PS5-Lapy-JB-Daemon contributors](https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon/graphs/contributors).
