# Filesystem elevation with upstream Lapy

ProsperoLight packages an exact-title one-shot helper built from pinned revision
`54a095c0f19161825e845daa760a03b446e654fa` of
[mpereiraesaa's PS5-Lapy-JB-Daemon](https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon).

At single-threaded startup the client first proves whether `/data` access is
already present. It then gives a resident Lapy service a bounded opportunity
through `/download0/elevate_proc`. If no service claims that request, the client
cancels it and streams packaged `/app0/lapy.elf` to the loopback ELF loader on
port 9021. A versioned handshake binds the helper to the current PID and title;
the app uses `/data` only after a read/write proof succeeds.

The build verifies Lapy's source revision, target-title manifest, protocol hash,
ELF hash, and loader framing. The upstream MIT license ships as `Lapy-MIT.txt`.
No kernel offsets or elevation implementation are maintained in this repository.
