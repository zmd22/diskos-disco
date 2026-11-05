# 3.4.1 release record

Fresh static MIPS32r2/o32/FP64/NaN2008 build using pinned GCC 11.2.0 / musl 1.2.6, with `-ffp-contract=off`. Built from a clean export of dev commit `7d8e66b0feb0c53932af8b52e570d00fac84acc3` (version 3.4.1, empty test label); the exact tracked UI snapshot is recorded in `payload/mq_ui.build.json`.

Binary size: 7703164 bytes.
Binary SHA-256: `4f986897e5c081bb727a0d0ec43f9a285e296d65ea00d5dd708db0b85e4a0d96`
Binary MD5: `59dfb3e57bf937b5f7b0a196ec3ffdbf`

Embedded, new in 3.4.1: Monocypher 4.0.2 (BSD-2-Clause or CC0-1.0) for MA Sendspin's encryption and pairing, and dr_flac 0.13.4 (Unlicense or MIT-0) for its FLAC; both unmodified in `ui/vendor/`.

Validation: target build verified static linkage and the MIPS32r2/o32/FP64/NaN2008 ABI; release preflight checks source/version/ELF/binary checksums. Host tests: Sendspin crypto known-answer tests (spec token vectors, Elligator 2), the Sendspin interop test against aiosendspin 10.3 (21 checks: handshake, approval, pairing by code, QR and token, unpair, FLAC and PCM, metadata, artwork; host build and the MIPS build under qemu), the shuffle-history test (11 checks), and Up Next against a copy of a real 28,619-song queue. The same source was tested on a Disc as signed test builds 3.4.1 and 3.4.1-fixed; this exact binary differs from 3.4.1-fixed only in its empty test label and has not itself been physically tested.

[Release notes](releases/v3.4.1.md).
