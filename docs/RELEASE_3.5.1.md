# 3.5.1 release record

Fresh static MIPS32r2/o32/FP64/NaN2008 build using pinned GCC 11.2.0 / musl 1.2.6, with `-ffp-contract=off`. Built from a clean export of dev commit `1ac6dc5a0c7732d874e45db0e654a7c1aa554bfa` (version 3.5.1, empty test label); the exact tracked UI snapshot is recorded in `payload/mq_ui.build.json`.

Binary size: 7822080 bytes (3.5.0: 7821056).
Binary SHA-256: `1c3ed27db4840d26c6213b1b0228a4399af178db8e3c65575c7e3f75db484829`
Binary MD5: `05112587146d3da792a0dbd17495b8e3`

What changed from 3.5.0: one setting, Lock on timeout (Battery & power, Off by default). On, when diskOS turns the screen off by itself it writes one power-button press into `/dev/input/event0`; the player reads that device without a grab and runs its own lock (touch chip asleep, panel off), so its lock state stays right and one real press wakes the screen. The press is sent only while the player is unlocked (brightness above 0), and the screen counts as player-locked only once brightness reads 0. With the setting Off, behaviour is unchanged. No audio, Bluetooth, Wi-Fi or update-path code changed.

Validation: target build verified static linkage and the MIPS32r2/o32/FP64/NaN2008 ABI; release preflight checks source/version/ELF/binary checksums. On a Disc, the player's response to a written power key was measured first (brightness, `bl_power`, touch interrupts per CPU). The same source was then signed with the test label "lock2" and put on a Disc as 3.5.0-lock2: Off unchanged; On locks with no touch interrupts and one press wakes; a tap during the screensaver still wakes; the button lock and wake are unchanged; it held under load. This exact binary differs from it only in its version and empty test label and has not itself been physically tested.

[Release notes](releases/v3.5.1.md).
