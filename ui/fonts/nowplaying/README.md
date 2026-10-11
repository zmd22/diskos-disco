# Now Playing font sources

Unmodified Google Fonts sources, under the bundled SIL OFL 1.1 licenses:

- Inter: `https://github.com/google/fonts/tree/main/ofl/inter`
- Nunito: `https://github.com/google/fonts/tree/main/ofl/nunito`

`ui/tools/gen_np_fonts.py` instances Inter at weight 600 and optical size 14,
and Nunito at weight 700. It rasterizes 30 px titles and 22 px artist text as
4-bit LVGL fonts. Install `lv_font_conv` (version 1.5.3), then run from the repo root:

```sh
python ui/tools/gen_np_fonts.py --converter /path/to/lv_font_conv/lv_font_conv.js
```

The sources are bundled for reproducibility. Runtime uses only the generated C
bitmaps; it does not load TTF files, scale glyphs or synthesize bold text.
The original international and CJK fallback chains remain available.
