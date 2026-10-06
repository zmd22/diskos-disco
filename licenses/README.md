# Bundled license texts

This directory carries the full license texts for the third-party components the diskOS installer
bundles. See `../NOTICE.md` for the component->license mapping and the static-linking obligations.

| Component (bundled) | License | Full text |
|---|---|---|
| usbboot | GPL-2.0-or-later | [`GPL-2.0.txt`](GPL-2.0.txt) |
| squashfs-tools 4.6.1 (Linux), 4.7.5 (macOS arm64) | GPL-2.0-or-later | [`GPL-2.0.txt`](GPL-2.0.txt) |
| liblzo2 (static in mksquashfs/unsquashfs) | GPL-2.0-or-later | [`GPL-2.0.txt`](GPL-2.0.txt) |
| libusb-1.0.27 (static in usbboot) | LGPL-2.1 | [`LGPL-2.1.txt`](LGPL-2.1.txt) |
| libusb 1.0.30 (macOS arm64 dylib) | LGPL-2.1-or-later | [`LGPL-2.1-libusb.txt`](LGPL-2.1-libusb.txt), copied from its source tarball |
| lzo 2.10 (macOS arm64 dylib) | GPL-2.0-or-later | [`GPL-2.0.txt`](GPL-2.0.txt) |
| lz4 1.10.0 library (macOS arm64 dylib) | BSD-2-Clause | [`BSD-2-Clause-lz4.txt`](BSD-2-Clause-lz4.txt), [`LZ4-LICENSE.txt`](LZ4-LICENSE.txt), copied from the source tarball |
| xz 5.8.4 liblzma (macOS arm64 dylib) | 0BSD; source also contains GPL-2.0-or-later parts | [`0BSD-xz.txt`](0BSD-xz.txt), [`XZ-COPYING.txt`](XZ-COPYING.txt), [`GPL-2.0.txt`](GPL-2.0.txt) |
| zstd 1.5.7 library (macOS arm64 dylib) | BSD-3-Clause or GPL-2.0-only; source also has MIT/BSD-2-Clause parts | [`BSD-3-Clause-zstd.txt`](BSD-3-Clause-zstd.txt), [`GPL-2.0-zstd.txt`](GPL-2.0-zstd.txt); full source in `../corresponding-source/macos-arm64/` |
| glibc (static in usbboot/mksquashfs/unsquashfs) | LGPL-2.1-or-later | [`LGPL-2.1.txt`](LGPL-2.1.txt); corresponding source in `../corresponding-source/glibc_2.39*` (see `../NOTICE.md` for the relink path) |
| PyInstaller bootloader (only for a self-built onefile) | GPL-2.0 + bootloader exception | [`GPL-2.0.txt`](GPL-2.0.txt) + exception note in `../NOTICE.md` |
| zlib / liblzma (static in squashfs-tools) | zlib / public-domain | permissive notices travel with the squashfs-tools sources the build script fetches |
| pyusb | BSD-3-Clause | [`BSD-3-Clause-pyusb.txt`](BSD-3-Clause-pyusb.txt) (verbatim from pyusb 1.3.1) |
| pycryptodome | BSD-2-Clause + public-domain | [`pycryptodome-LICENSE.txt`](pycryptodome-LICENSE.txt) (verbatim from pycryptodome 3.23.0) |
| CPython | Python Software Foundation License | [`PSF-Python.txt`](PSF-Python.txt) (verbatim from the bundled CPython) |
| LVGL (diskOS UI) | MIT | [`MIT-LVGL.txt`](MIT-LVGL.txt) (verbatim from `../ui/lvgl/LICENCE.txt`) |
| JSMN (diskOS UI JSON parser) | MIT | [`MIT-JSMN.txt`](MIT-JSMN.txt) (verbatim from `../ui/jsmn.h`, (c) 2010 Serge Zaitsev) |
| Font Awesome Free glyphs (via LVGL `LV_SYMBOL_*`) | OFL-1.1 (fonts) + CC-BY-4.0 (icons) | [`OFL-1.1-FontAwesome.txt`](OFL-1.1-FontAwesome.txt) (Fonticons' own LICENSE.txt: full OFL-1.1 + MIT) + [`CC-BY-4.0.txt`](CC-BY-4.0.txt) |
| musl libc (static in diskOS UI `mq_ui`) | MIT | [`MIT-musl.txt`](MIT-musl.txt) |
| SQLite (amalgamation in `mq_ui`) | Public domain | https://sqlite.org/copyright.html (no license text required) |
| QR Code generator, Nayuki (via LVGL, in `mq_ui`) | MIT | [`MIT-qrcodegen.txt`](MIT-qrcodegen.txt) (verbatim from the LVGL source header) |
| TJpgDec, ChaN (via LVGL, in `mq_ui`) | BSD-style | [`LICENSE-TJpgDec.txt`](LICENSE-TJpgDec.txt) (verbatim from the LVGL source header) |
| Montserrat font (via LVGL built-in fonts, in `mq_ui`) | OFL-1.1 | [`OFL-1.1-Montserrat.txt`](OFL-1.1-Montserrat.txt) |
| Source Han Sans SC (CJK fallback font in `mq_ui`) | OFL-1.1 | [`OFL-1.1-SourceHanSans.txt`](OFL-1.1-SourceHanSans.txt) |
| IBM Plex Mono (converted as `diskos_mono`, "something" theme, in `mq_ui`) | OFL-1.1 (Reserved Font Name "Plex") | [`OFL-1.1-IBMPlexMono.txt`](OFL-1.1-IBMPlexMono.txt) |
| Doto (converted as `diskos_dot`, "something" theme, in `mq_ui`) | OFL-1.1 | [`OFL-1.1-Doto.txt`](OFL-1.1-Doto.txt) |
| Inter (converted as `diskos_hifi_ui`, hi-fi theme text, in `mq_ui`) | OFL-1.1 | [`OFL-1.1-Inter.txt`](OFL-1.1-Inter.txt) |
| DSEG7 Classic (converted as `diskos_hifi_seg`, hi-fi theme clock and time counters, in `mq_ui`) | OFL-1.1 (Reserved Font Name "DSEG") | [`OFL-1.1-DSEG.txt`](OFL-1.1-DSEG.txt) |
| JetBrains Mono (converted as `diskos_term`, terminal theme text and clock, in `mq_ui`) | OFL-1.1 | [`OFL-1.1-JetBrainsMono.txt`](OFL-1.1-JetBrainsMono.txt) |
| Space Grotesk (converted as `diskos_bauh`, bauhaus theme text, titles and clock, in `mq_ui`) | OFL-1.1 | [`OFL-1.1-SpaceGrotesk.txt`](OFL-1.1-SpaceGrotesk.txt) |
| IBM Plex Sans (converted as `diskos_blue_sans`, blueprint theme text, in `mq_ui`) | OFL-1.1 (Reserved Font Name "Plex") | [`OFL-1.1-IBMPlexSans.txt`](OFL-1.1-IBMPlexSans.txt) |
| Nunito (converted as `diskos_stone`, stone theme text and clock, in `mq_ui`) | OFL-1.1 | [`OFL-1.1-Nunito.txt`](OFL-1.1-Nunito.txt) |
| Courier Prime (converted as `diskos_zine`, zine theme text and clock, in `mq_ui`) | OFL-1.1 | [`OFL-1.1-CourierPrime.txt`](OFL-1.1-CourierPrime.txt) |
| Rubik Glitch (converted as `diskos_zine_glitch`, zine theme titles, in `mq_ui`) | OFL-1.1 | [`OFL-1.1-RubikGlitch.txt`](OFL-1.1-RubikGlitch.txt) |
| MD5, Peslyak/"Solar Designer" (in `mq_ui`) | Public domain | in `../ui/md5.c`; no license text required |
| dropbearmulti (Dropbear SSH 2022.83, Debug Mode) | MIT-style | [`LICENSE-dropbear.txt`](LICENSE-dropbear.txt) (verbatim upstream) |
| disc_spl_lpddr3.bin (X2000 stage-1 SPL) | GPL-2.0 | [`GPL-2.0.txt`](GPL-2.0.txt); corresponding source in `../spl-src/` (see `../SPL_SOURCE.md`) |

All texts above are included **verbatim from the actual upstream sources** (not hand-written), with
their real copyright lines. **Copyleft texts (GPL-2.0, LGPL-2.1)** carry the redistribution
obligations (corresponding source + relink path; see `../NOTICE.md`). The macOS arm64 source archives, formula snapshots and build note are in
`../corresponding-source/macos-arm64/`. Linux zlib and liblzma notices travel with
the corresponding source.

## diskOS's own license
The **Python installer, build scripts, and docs** are licensed **MIT** - `Copyright (c) 2026 diskOS
contributors`. See [`../LICENSE`](../LICENSE). The **diskOS UI source** (`ui/`, built to
`payload/mq_ui`) is licensed **GPL-3.0-or-later** (`GPL-3.0.txt`, `../ui/COPYING`; (c) diskOS
contributors) - deliberately separate from the MIT tooling so UI forks stay open. Its embedded
third-party notices - LVGL (MIT), SQLite (public domain), JSMN (MIT), md5 (public domain), and
Montserrat / Source Han Sans / Font Awesome / IBM Plex Mono / Doto and the theme fonts (Inter, DSEG7 Classic, JetBrains Mono, Space Grotesk, IBM Plex Sans, Nunito, Courier Prime, Rubik Glitch) (OFL-1.1) - ship in this directory and in
`../ui/licenses/`.
