# 3.4.2 release record

Fresh static MIPS32r2/o32/FP64/NaN2008 build using pinned GCC 11.2.0 / musl 1.2.6, with `-ffp-contract=off`. Built from a clean export of dev commit `6c7c6ff8f728682417ebe10f922ff5c1683773c6` (version 3.4.2, empty test label); the exact tracked UI snapshot is recorded in `payload/mq_ui.build.json`.

Binary size: 7019568 bytes (3.4.1: 7703164).
Binary SHA-256: `bca2b27381b36c888cd94350dd559d89800afa3797dee67f556c802a443864ce`
Binary MD5: `1cad93a54b6a8173a007e187a43b93a7`

Includes PR #24 by Mihail Dyusov (@meekieD): Cyrillic coverage (0x400–0x45F plus typographic quotes and dashes) in all 21 Zune cuts, and the `REVIEW_ZUNE_CYR` host check. The large Zune cuts were then regenerated in LVGL's compressed font format (`LV_USE_FONT_COMPRESSED`, `ui/tools/compress_lv_font.py`, which decodes every glyph back and fails on any pixel difference).

Validation: target build verified static linkage and the MIPS32r2/o32/FP64/NaN2008 ABI; release preflight checks source/version/ELF/binary checksums. Host checks: 636 renders (15 themes × light/dark × 22 scenes) pixel-identical before/after font compression, 720 Boomshine text frames identical, the Cyrillic Zune review, `tests/battcycle_test` (synthetic charges and a replay of a real `usage.bin`), `REVIEW_SQUASH_FOLLOW`. The same source was tested on a Disc as signed test builds, the last being 3.4.2-squash2; this exact binary differs from it only in its version label and has not itself been physically tested.

[Release notes](releases/v3.4.2.md).
