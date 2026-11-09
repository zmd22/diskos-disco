# 3.4.3 release record

Fresh static MIPS32r2/o32/FP64/NaN2008 build using pinned GCC 11.2.0 / musl 1.2.6, with `-ffp-contract=off`. Built from a clean export of dev commit `0b787059c76e00c687a614744cf5b4339e045b8c` (version 3.4.3, empty test label); the exact tracked UI snapshot is recorded in `payload/mq_ui.build.json`.

Binary size: 7023680 bytes (3.4.2: 7019568).
Binary SHA-256: `36be56efb1041c6018215f8361e94303a9041f1d02570a0ce121b2e6086bf977`
Binary MD5: `4c90cb72a316e46a3474f56552b00f23`

Includes PR #25 by @ericklv (Disc Shelf in the Zune theme: home page, Server form layout, server validation messages).

Validation: target build verified static linkage and the MIPS32r2/o32/FP64/NaN2008 ABI; release preflight checks source/version/ELF/binary checksums. Host checks: the new `host-render … shade` mode rendered the Shade in all seven themes (Ring, Braun, Disco, Null, Squash, Zune, Braun 2.0); the previous / next glyphs were measured on the renders against their ovals' centre of area. The binary was signed and copied to a Disc's SD card as 3.4.3; it has not been physically tested before release.

[Release notes](releases/v3.4.3.md).
