# 3.4.4 release record

Fresh static MIPS32r2/o32/FP64/NaN2008 build using pinned GCC 11.2.0 / musl 1.2.6, with `-ffp-contract=off`. Built from a clean export of dev commit `9eb48c621f57ed45675b1510162faaf8702fab17` (version 3.4.4, empty test label); the exact tracked UI snapshot is recorded in `payload/mq_ui.build.json`.

Binary size: 7810256 bytes (3.4.3: 7023680).
Binary SHA-256: `788f12f01d0c8d9f431fe59b02f1709772e841cfcf66212f9c4f087237c66230`
Binary MD5: `be382a735d597a9119793ac5230a78f2`

The only change from 3.4.3: the 28 fonts of 32 px and up are stored uncompressed again and `LV_USE_FONT_COMPRESSED` is 0, undoing the compression from 3.4.2. LVGL 9.2 keeps no glyph cache, so each compressed glyph was decoded on every draw. The Zune cuts keep PR #24's Cyrillic. Boomshine's run-length glyphs are unchanged.

Validation: target build verified static linkage and the MIPS32r2/o32/FP64/NaN2008 ABI; release preflight checks source/version/ELF/binary checksums. Host renders of every theme, light and dark, on Home, Now Playing, actions and the Shade, with both Now Playing fonts: 140 frames pixel-identical to 3.4.3. The binary was signed and copied to a Disc's SD card as 3.4.4; it has not been physically tested before release.

[Release notes](releases/v3.4.4.md).
