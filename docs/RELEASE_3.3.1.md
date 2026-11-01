# 3.3.1 release record

Fresh static MIPS32r2/o32/FP64/NaN2008 build using pinned GCC 11.2.0 / musl 1.2.6, with `-ffp-contract=off`. Built from main commit `5ec6a096c641cbf3227b9a183f610fedbfa3434b` with the version set to 3.3.1; the exact tracked UI snapshot is recorded in `payload/mq_ui.build.json`.

Binary size: 7316908 bytes.
Binary SHA-256: `6b4cb4d45abf1e784c8f5e7fb9b7cd3f5395e6b93cdf20a04ae566c750ca7879`
Binary MD5: `1a367db7e98bcc62bc83f6de366f0874`

Validation: target build helper verified static linkage, MIPS32r2/o32/FP64/NaN2008 ABI; release preflight checks source/version/ELF/binary checksums; complete-ZIP packaging checks document links. The native LVGL Settings motion regression fails on the previous implementation and passes on the fixed implementation in Squash light/dark, Disco and Null. Physical-device testing of this binary has not been performed.

[Release notes](releases/v3.3.1.md).
