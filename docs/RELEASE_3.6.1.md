# 3.6.1 release record

Fresh static MIPS32r2/o32/FP64/NaN2008 build using GCC 11.2.0 / musl 1.2.6, with `-ffp-contract=off`. Built from a clean export of commit `72a39fda0420e21598d306ccd7d25c84e897504b` (version 3.6.1, empty test label); the exact tracked UI snapshot is recorded in `payload/mq_ui.build.json`.

Toolchain note: built in a cloud workspace without access to the GNU download sites. GCC 11.2.0 (gcc-mirror tag `releases/gcc-11.2.0`), musl 1.2.6 and Linux headers 4.19.88-2 are the versions in `ui/toolchain/mcm-config.mak`, with musl-cross-make `227df8b` patches applied; binutils is 2.42 (Ubuntu's `binutils-mipsel-linux-gnu`) instead of the pinned 2.44, and GMP/MPFR/MPC are Ubuntu's. The binary is therefore not byte-reproducible with the pinned toolchain.

Binary size: 8099632 bytes (3.6.0: 7859716).
Binary SHA-256: `058de8b9df3a54e2cc55c303cdc4e199c9e2b2809fd0a5b9d4411bbb026dc78b`
Binary MD5: `d85c225f98f28caefb2edecbd5d1e162`

What changed from 3.6.0: the Graphite theme (#31, by @ericklv), preset ID 15: a monochrome-LCD palette (one backlight, one ink, one mid tone; White/Grey/Amber backlight in Light), ChicagoFLF fonts at 12-64 px (public domain), its own layout kit (`ui/classic.c`), Quick Settings selection and Backlight / Spectrum bars settings, and per-screen palette hooks across the UI. Size grows by about 240 KB, mostly the fonts. No audio, Bluetooth, Wi-Fi or update-path code changed.

Validation: the ELF is static MIPS32r2/o32/FP64/NaN2008 (checked with readelf); release preflight checks source/version/ELF/binary checksums. Host: `check-theme` and the host-render build. The maintainer reports Graphite was tested on a Disc by others; this exact binary has not itself been physically tested.

[Release notes](releases/v3.6.1.md).
