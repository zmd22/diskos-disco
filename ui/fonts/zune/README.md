# Zune interface font source

Open Sans variable font from the Google Fonts repository (SIL Open Font License; see ../../../licenses/OpenSans-OFL.txt).

Source: https://github.com/google/fonts/blob/main/ofl/opensans/OpenSans%5Bwdth,wght%5D.ttf

SHA-256: `36643644f318a812aab2d2ed3bb98f8cf0872527f835fe9398d95fe6b9adb878`

Rebuild with lv_font_conv 1.5.3:

```sh
python3 ui/tools/gen_zune_fonts.py --converter /path/to/lv_font_conv/lv_font_conv.js
```

Generated C symbol/file sizes remain nominal slots so existing callers are preserved. The raster sizes increase by 1px for slots through 17, 2px through 50, and 3px for large numerals. The 32px and 46px nominal Light slots used by the Home menu use medium weight 500 in both dark and light palettes. Other small Light text uses weight 350, other large Light text 325, and Regular text 425. Semibold stays 600. The drawn 64px logo is unchanged. These are pre-rendered font tables, with no runtime scaling or added timers.

Text cuts include the Cyrillic block U+0400-U+045F (Russian, Ukrainian, Belarusian, Serbian, Bulgarian); other scripts continue through the shared international/CJK fallback chain. Clock digits and the drawn 64px logo stay Latin-only.
