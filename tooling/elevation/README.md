# Filesystem elevation with upstream Lapy

ProsperoLight packages an exact-title one-shot helper built from pinned revision
`57fcc19b7f57e46c823d6f2a6c6233ebf3288079` of
[BlackBearReloaded's PS5-Lapy-JB-Daemon](https://github.com/blackbearreloaded/PS5-Lapy-JB-Daemon).
That revision includes the donor-release fix, 32-byte credential attributes and
the guarded target lifetime;
the helper builds with payload SDK v0.42 for firmware 13.40 through 13.60.

At single-threaded startup the client first proves whether `/data` access is
already present. It then gives a resident Lapy service a bounded opportunity
through `/download0/elevate_proc`. If no service claims that request, the client
cancels it and streams packaged `/app0/lapy.elf` to the loopback ELF loader on
port 9021. A versioned handshake binds the helper to the current PID and title;
the app uses `/data` only after a read/write proof succeeds.

The build verifies Lapy's source revision, target-title manifest, protocol hash,
ELF hash, and loader framing. The upstream MIT license ships as `Lapy-MIT.txt`.
No kernel offsets or elevation implementation are maintained in this repository.
