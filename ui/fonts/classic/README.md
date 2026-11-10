# Classic interface font source

ChicagoFLF 2.0 by Robin Casady, released into the public domain (see ../../../licenses/PublicDomain-ChicagoFLF.txt).

Source: https://github.com/css-pkg/chicago.css (ChicagoFLF.ttf)

SHA-256: `3c5896edd062de6a2fd786317e3e2f75982219ab623a184beba55bd951b38762`

Rebuild with lv_font_conv 1.5.3:

```sh
python3 ui/tools/gen_classic_fonts.py --converter /path/to/lv_font_conv/lv_font_conv.js
```

Text cuts carry Latin-1 and typographic punctuation; other scripts use the shared international/CJK fallback chain.
