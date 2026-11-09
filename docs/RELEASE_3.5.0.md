# 3.5.0 release record

Fresh static MIPS32r2/o32/FP64/NaN2008 build using pinned GCC 11.2.0 / musl 1.2.6, with `-ffp-contract=off`. Built from a clean export of dev commit `84e9b03147ab3439a38004b2d88a95745c3df7b3` (version 3.5.0, empty test label); the exact tracked UI snapshot is recorded in `payload/mq_ui.build.json`.

Binary size: 7821056 bytes (3.4.4: 7810256).
Binary SHA-256: `012e5e6bfffe7685a0a2b6215083fc61286a56cb27b459bd1888e0494ed2f91d`
Binary MD5: `602237f7dec17a463e9403094185f0df`

What changed from 3.4.4: an interface polish pass. A nothing-playing state in every theme; larger text and less empty space in Squash; larger Now Playing, Quick Settings captions and dial keyboard in Zune; Shade, navigation and layout fixes; Screen Off now acts when the screensaver is Off. No audio, Bluetooth, Wi-Fi or update-path code changed. The build now fails on an implicit function declaration.

Validation: target build verified static linkage and the MIPS32r2/o32/FP64/NaN2008 ABI; release preflight checks source/version/ELF/binary checksums. Host renders of every page in all seven themes, light and dark, were compared against 3.4.4: only the pages this release means to change differ. Host tests: history, idle power-off (19 cases), plugged-in detection, theme compatibility, curve geometry and stability pass. The same source was signed with the test label "rc1" and put on a Disc as 3.5.0-rc1; this exact binary differs from it only in its empty test label and has not itself been physically tested.

[Release notes](releases/v3.5.0.md).
