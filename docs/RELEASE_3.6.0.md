# 3.6.0 release record

Fresh static MIPS32r2/o32/FP64/NaN2008 build using pinned GCC 11.2.0 / musl 1.2.6, with `-ffp-contract=off`. Built from a clean export of dev commit `167dcac58ff65ccb22ca3a3bc7f1e6847a70b06b` (version 3.6.0, empty test label); the exact tracked UI snapshot is recorded in `payload/mq_ui.build.json`.

Binary size: 7859716 bytes (3.5.1: 7822080).
Binary SHA-256: `8701f47f67041e6000404ef7301fbcddf990d43bbc91c8dbb76045600d96823b`
Binary MD5: `03c56e54a255f1f3df115176b38ab2c5`

What changed from 3.5.1: progressive lyrics in Lyrics and Immersive (smooth line motion, exact Enhanced LRC word timing, optional estimated word timing, tap a timed line to seek; two new Text & layout settings, both On by default) from #29, taken without that PR's Now Playing title/artist links; the built-in Cat Dash game (#26) with a frame timer that runs only during a run, an App Shelf entry and the Null nav hidden over it; Zune ignores a saved Outdoor mode, and the RTC is read before the Auto appearance at startup (#27); Disc Shelf SMB publishes downloads on FAT/exFAT drivers without no-replace rename or hard links by an exclusive bounded copy, and keeps the first operation-specific error (#28). Synced lyric lines grow from about 124 to about 516 bytes each (word timings) in the two 240-line buffers. No audio, Bluetooth, Wi-Fi or update-path code changed.

Validation: target build verified static linkage and the MIPS32r2/o32/FP64/NaN2008 ABI; release preflight checks source/version/ELF/binary checksums. Host: `check-theme`, the host-render build, the lyrics tests (`lyrics_word`, `lyrics_motion`, `imm_lyrics_compositor`, ASan/UBSan), the SMB publication fault tests and the encrypted SMB3 integration test (exact SHA-256 of completed 21/33 MiB downloads), and Cat Dash / App Shelf renders. The same source with the test label "dev1" was signed and run on a Disc as 3.5.1-dev1: clean start, no stalls or crashes logged, memory steady (about 20 MB available), and Immersive with lyrics measured with Progressive lyrics on and off (on was not higher). This exact binary differs from it only in its version and empty test label and has not itself been physically tested.

[Release notes](releases/v3.6.0.md).
