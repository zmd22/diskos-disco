# 3.7.1 release record

Fresh static MIPS32r2/o32/FP64/NaN2008 build using GCC 11.2.0 / musl 1.2.6 / binutils 2.44 (the versions pinned in `ui/toolchain/mcm-config.mak`), with `-ffp-contract=off`. Built from a clean export of commit `146794a2017882ae866f9d17a4f16c578eb91a92` (version 3.7.1, empty test label); the exact tracked UI snapshot is recorded in `payload/mq_ui.build.json`.

Binary size: 8107832 bytes (3.7.0: 8107832).
Binary SHA-256: `964eb2eda95f9e74d3241d476babe52bf5a42ce436f7bbd20a3bf1462e808157`
Binary MD5: `f4d17e1c8c4d41e9d10523175e71bca8`

What changed from 3.7.0: a regression fix only. 3.7.0 keeps Immersive open under Queue / Up Next, Search and Quick Settings opened from its shortcuts, but `main.c` latched every touch as Immersive's while `ui_np_fsart_active()` was true, so the Back, Quick Settings and Home gestures were swallowed on every screen. The touch loop now requires Now Playing to be the current screen, and `screenmgr.c` ends Immersive (`ui_np_fsart_drop`) on any transition after which the back stack no longer holds Now Playing. Nothing else changed.

Validation: the ELF is static MIPS32r2/o32/FP64/NaN2008; release preflight checks source/version/ELF/binary checksums. Host: host-render build and the Immersive edge touch test. The gesture loop runs only on the device; the same fix labelled 3.7.1-test was signed and copied to the Disc that reported the problem. This exact binary has not itself been physically tested.

[Release notes](releases/v3.7.1.md).
