# Compressed PS5 package

`make ffpfsc` produces a raw `PPSA99002.exfat` and a compressed
`PPSA99002.ffpfsc` containing exactly that volume. The Windows entry point
uses the same Linux/WSL packaging pipeline.

1. Serialize exFAT and compare every payload file with the app folder.
2. Pack the volume with MkPFS `pack file`, PS5 profile, 32-bit inodes,
   64 KiB blocks, explicit zlib compression at level 6.
3. Run MkPFS verification and decompress the entire outer container. Compare
   the inner exFAT size and SHA-256 with the verified raw volume.

The former `pack folder` path also produced an inner exFAT, but its streaming
implementation did not pass the requested compression backend into the
encoder. Using `pack file` makes the selected backend explicit and permits
an independent whole-volume comparison. Host verification is necessary but
cannot prove compatibility with the console's hardware decompressor.

Use only one canonical image for a title in ShadowMountPlus. After replacing
an image, unmount and remount it; an existing mount still refers to the old
image. Verify `eboot.bin`, fonts, catalogs and other mounted payloads before
claiming on-console compression compatibility. Keep a known-good raw image
outside the launcher's scan until that verification is complete.

Reference implementations reviewed on 2026-10-06:

- [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus): outer PFSC
  containing a nested exFAT/PFS image, not loose game files.
- [MkPFS](https://github.com/PSBrew/MkPFS): pinned build tooling; newer main
  revisions were reviewed but do not establish a console decompression fix.
- [PS5 Game Compressor](https://github.com/juma-sayeh/PS5-Game-Compressor):
  hardware validation and replacement of failing compressed blocks with raw
  blocks, demonstrating why desktop decompression alone is insufficient.
