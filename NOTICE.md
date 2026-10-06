# diskOS installer - third-party components & licenses

The diskOS installer ships the third-party programs and libraries below. Each is invoked as a
separate program or loaded as a library; this document provides the required attribution and points
to the corresponding source that ships alongside the binaries. The full verbatim license texts are in
[`licenses/`](licenses/); this file is the component-to-license index.

## Bundled native programs (called as subprocesses - mere aggregation)

| Component | Purpose | License | Source |
|---|---|---|---|
| **usbboot** | Ingenic X2000 mask-ROM USB loader (the flasher front-end) | **GPL-2.0-or-later** - (c) 2021 Aidan MacDonald, (c) 2015 Amaury Pouly | included: `src/usbboot/` (built from this) |
| **mksquashfs / unsquashfs** | build/extract the rootfs image | **GPL-2.0-or-later** | Linux: 4.6.1 in `corresponding-source/squashfs-tools_4.6.1*`; macOS arm64: 4.7.5 in `corresponding-source/macos-arm64/` |
| **dropbearmulti** (Dropbear SSH **2022.83**) | embedded in the diskOS image; started by the opt-in Debug Mode for SSH-over-WiFi | **MIT-style** ((c) 2002-2020 Matt Johnston; components under MIT/BSD/public-domain) - text in `licenses/LICENSE-dropbear.txt` | https://matt.ucc.asn.au/dropbear/dropbear.html (v2022.83; **predates the CVE-2023-48795 "Terrapin" Strict-KEX mitigation - an update is planned**. Debug Mode SSH is opt-in and short-lived, and Dropbear's Terrapin exposure is limited.) |

`usbboot` and squashfs-tools are **GPL-2.0-or-later**, so anyone redistributing them must make their
corresponding source available and include the GPL-2.0 license text. (`dropbearmulti` is MIT-style -
no source-availability obligation, only its license text as noted above.) **The complete
corresponding source ships in-tree and in every release tarball**, so it travels from the same place
as the binaries (GPL-2.0 section 3(a), plus the "same place" provision in section 3's final paragraph):
`usbboot`'s own source is `src/usbboot/usbboot.c`;
**Linux squashfs-tools 4.6.1** source is `corresponding-source/squashfs-tools_4.6.1*`. The macOS arm64 4.7.5 source, Homebrew patch, dependency sources and formulas are in `corresponding-source/macos-arm64/`. The Linux tools are rebuilt by
`build/build-usbboot-static.sh` / `build/build-squashfs-static.sh`.

**Static linking (Linux):** the prebuilt Linux `usbboot`, `mksquashfs`, and `unsquashfs` are shipped
**fully statically linked** (built with `gcc -static` on a glibc host). They therefore statically
incorporate:
- **libusb-1.0.27 (LGPL-2.1)** - in `usbboot`
- **liblzo2 2.10 (GPL-2.0-or-later)**, **zlib**, **liblzma** (permissive) - in squashfs-tools
- the **GNU C Library (glibc, LGPL-2.1-or-later)** - the host's system C runtime, in all three

Because liblzo2 is GPL, the resulting `mksquashfs`/`unsquashfs` binaries are effectively GPL-2.0. The
corresponding source for the statically-linked **GPL/LGPL** libraries is provided so their section 3/section 6
obligations are met:
- **liblzo2** (`corresponding-source/lzo2_2.10*`) and **libusb-1.0** (`corresponding-source/libusb-1.0_1.0.27*`)
  ship in-tree.
- **glibc** (statically linked, so it travels inside the binary): the **complete corresponding source**
  is vendored in `corresponding-source/glibc_2.39*` (the exact version the shipped binaries were built
  against). Because it is statically linked it does not qualify for the "system library" exception, so
  its source ships alongside the binaries like the others.

**Relink path (satisfies LGPL-2.1 section 6 for libusb *and* glibc):** we ship the **complete source** of
`usbboot` (`src/usbboot/usbboot.c`) and squashfs-tools (`corresponding-source/squashfs-tools_4.6.1*`)
plus the exact Linux build recipes (`build/build-usbboot-static.sh`, `build/build-squashfs-static.sh`). A
user can therefore rebuild and relink these tools against a **modified** libusb, liblzo2, **or glibc**
of their choosing - the freedom section 6 exists to protect. See
[`corresponding-source/README.md`](corresponding-source/README.md).

**Python runtime + libraries - NOT redistributed by this project.** The installer runs **from
source** with **your own Python**. The two Python dependencies (`pyusb`, `pycryptodome`) are fetched
from PyPI by *your* `pip` into a local `.venv` (created by `./install.sh`); CPython, `tkinter`, and
their transitive native libraries (OpenSSL, Tk/Tcl, the X11 stack, freetype, fontconfig, zlib, etc.)
all come from **your system's Python** - this project ships none of them, so their redistribution
notices are not our obligation. Their license texts (for reference) are still listed in
[`licenses/README.md`](licenses/README.md).

> If you instead build a self-contained PyInstaller onefile yourself (`build/build.sh`), *that*
> binary embeds CPython + ~90 native libraries and you become the redistributor of them - see
> [`licenses/THIRD_PARTY_BUNDLED.md`](licenses/THIRD_PARTY_BUNDLED.md) for the enumeration and
> obligations. The **published release does not do this**; it distributes source + the native flash
> tools below.

## Bundled device blobs

| Component | Purpose | Notes |
|---|---|---|
| **my_write6_dram.bin** | the bad-block-aware DRAM NAND writer run on-device (checks the Disc's firmware before writing) | diskOS project code - source: `flash/my_write6.c`, built by `flash/build_nand.sh` |
| **disc_spl_lpddr3.bin** | Ingenic X2000 USB stage-1 SPL (DRAM init) | **GPL-2.0** - built from source: Ingenic-community/uboot-xburst `1060b516` + our LPDDR3 patches. Corresponding source in `spl-src/`; see `SPL_SOURCE.md`. The binary also includes a roughly 332-byte DDR parameter block captured from the factory configuration. Its vendor-origin provenance remains an unresolved compliance item, as disclosed in `SPL_SOURCE.md`. (No vendor/USBCloner binary is redistributed.) |

## Python dependencies (fetched by your pip, not redistributed by us)

`./install.sh` installs these from PyPI into a local `.venv`; **CPython** comes from your own system
Python. This project does not ship any of them, so their license texts are listed here only for
reference. (**PyInstaller** is used *only* if you build the optional onefile yourself; it is not
involved in the source release.)

| Component | License |
|---|---|
| **pyusb** (USB device detection) | BSD-3-Clause |
| **pycryptodome** (AES-decrypt of FiiO OTA chunks) | BSD-2-Clause + public-domain (Unlicense) |
| **CPython** (your system's) | Python Software Foundation License |
| **PyInstaller** runtime bootloader (only for a self-built onefile) | GPL-2.0 **with a bootloader exception** permitting distribution of the packaged app under any license |

## Bundled shared libraries (macOS build)

| Component | License |
|---|---|
| **libusb-1.0.0.dylib** (libusb 1.0.30) | LGPL-2.1-or-later; `licenses/LGPL-2.1-libusb.txt` |
| **liblzo2.2.dylib** (lzo 2.10) | GPL-2.0-or-later; `licenses/GPL-2.0.txt` |
| **liblz4.1.10.0.dylib** (lz4 1.10.0) | BSD-2-Clause; `licenses/BSD-2-Clause-lz4.txt` |
| **liblzma.5.dylib** (xz 5.8.4) | 0BSD for liblzma; XZ source includes GPL-2.0-or-later parts; `licenses/0BSD-xz.txt`, `licenses/XZ-COPYING.txt` |
| **libzstd.1.5.7.dylib** (zstd 1.5.7) | BSD-3-Clause or GPL-2.0-only for the library; `licenses/BSD-3-Clause-zstd.txt`, `licenses/GPL-2.0-zstd.txt` |


The exact macOS arm64 upstream archives, the squashfs Homebrew backport, and formula build
recipes are in `corresponding-source/macos-arm64/` (see its `README.md`). `vendor/setup-macos.sh`
builds `usbboot` against Homebrew libusb, copies Homebrew squashfs tools, and uses
`dylibbundler` to copy and relocate these five libraries. LGPL-2.1 section 6 users can
rebuild and relink `usbboot` against a modified libusb using the included source and recipe.

## diskOS's own code

- **The Python installer, build scripts, and docs** in this repo are licensed **MIT** -
  `Copyright (c) 2026 diskOS contributors`. See `LICENSE`.
- **`payload/diskos-launch`** is MIT project tooling; source: `src/launcher/diskos_launch.c`,
  built by `build/build-launcher.sh`.
- **`payload/diskos-bootprobe`** is GPL-3.0-or-later UI helper code; source:
  `ui/tools/diskos_bootprobe.c`, built with the UI tools.
- **The diskOS UI source** (`ui/`, built to `payload/mq_ui`, static-musl MIPS) is licensed
  **GPL-3.0-or-later** (`ui/COPYING`, `licenses/GPL-3.0.txt`; (c) diskOS contributors) - deliberately
  separate from the MIT tooling above so UI forks stay open. It bundles LVGL (MIT), SQLite (public
  domain), jsmn (MIT), md5 (public domain), and Montserrat / Source Han Sans / Font Awesome /
  IBM Plex Mono (as `diskos_mono`) / Doto (as `diskos_dot`) and the theme fonts Inter, DSEG7 Classic, JetBrains Mono,
  Space Grotesk, IBM Plex Sans, Nunito, Courier Prime and Rubik Glitch (as `diskos_<theme>_*`) (all OFL-1.1);
  see `ui/README.md` and `ui/licenses/`.

  **`diskos-artdec`** (`payload/diskos-artdec`, source `ui/tools/diskos_artdec.c`) is a separate small helper
  binary shipped in the diskOS image at `/opt/diskos/bin/diskos-artdec`. It extracts and decodes album
  artwork for the UI. It is diskOS's own code (**GPL-3.0-or-later**) and statically embeds the unmodified
  `stb_image.h` v2.30 by Sean Barrett (dual licence, MIT or public domain, your choice; notice and licence
  text at the end of `ui/tools/stb_image.h`, pinned in `ui/tools/stb_image.VERSION`) and the static musl C
  runtime (MIT). stb_image is NOT part of `mq_ui`.

  The built `mq_ui` binary statically incorporates these third-party components, whose licenses
  apply to the shipped binary (and whose source ships in `ui/` / `ui/lvgl/`):

  | Embedded in `mq_ui` | License | Text / source |
  |---|---|---|
  | **musl libc** (static C runtime) | MIT | `licenses/MIT-musl.txt`; source: https://musl.libc.org/ |
  | **LVGL** (UI toolkit) | MIT | `licenses/MIT-LVGL.txt` |
  | **JSMN** (JSON parser) | MIT | `licenses/MIT-JSMN.txt` |
  | **SQLite** (amalgamation) | Public domain | https://sqlite.org/copyright.html |
  | **MD5** (Peslyak/Solar Designer) | Public domain | `ui/md5.c` |
  | **QR Code generator** (Nayuki `qrcodegen`) | MIT | `licenses/MIT-qrcodegen.txt` |
  | **TJpgDec** (tiny JPEG decoder, via LVGL) | BSD-style (ChaN) | `licenses/LICENSE-TJpgDec.txt` |
  | **Montserrat** font (via LVGL built-in fonts) | SIL OFL 1.1 | `licenses/OFL-1.1-Montserrat.txt` |
  | **Source Han Sans SC** (CJK fallback font, subset) | SIL OFL 1.1 | `licenses/OFL-1.1-SourceHanSans.txt` |
  | **IBM Plex Mono** (converted as `diskos_mono`; Reserved Font Name "Plex") | SIL OFL 1.1 | `licenses/OFL-1.1-IBMPlexMono.txt` |
  | **Doto** (converted as `diskos_dot`) | SIL OFL 1.1 | `licenses/OFL-1.1-Doto.txt` |
  | **Inter** (converted as `diskos_hifi_ui`) | SIL OFL 1.1 | `licenses/OFL-1.1-Inter.txt` |
  | **DSEG7 Classic** (converted as `diskos_hifi_seg`; Reserved Font Name "DSEG") | SIL OFL 1.1 | `licenses/OFL-1.1-DSEG.txt` |
  | **JetBrains Mono** (converted as `diskos_term`) | SIL OFL 1.1 | `licenses/OFL-1.1-JetBrainsMono.txt` |
  | **Space Grotesk** (converted as `diskos_bauh`) | SIL OFL 1.1 | `licenses/OFL-1.1-SpaceGrotesk.txt` |
  | **IBM Plex Sans** (converted as `diskos_blue_sans`; Reserved Font Name "Plex") | SIL OFL 1.1 | `licenses/OFL-1.1-IBMPlexSans.txt` |
  | **Nunito** (converted as `diskos_stone`) | SIL OFL 1.1 | `licenses/OFL-1.1-Nunito.txt` |
  | **Courier Prime** (converted as `diskos_zine`) | SIL OFL 1.1 | `licenses/OFL-1.1-CourierPrime.txt` |
  | **Rubik Glitch** (converted as `diskos_zine_glitch`) | SIL OFL 1.1 | `licenses/OFL-1.1-RubikGlitch.txt` |
  | **Font Awesome Free** glyphs (via LVGL `LV_SYMBOL_*`) | OFL 1.1 (fonts) + CC-BY 4.0 (icons) | `licenses/OFL-1.1-FontAwesome.txt`, `licenses/CC-BY-4.0.txt` |

## NOT distributed by this installer

- **FiiO's stock rootfs** - you supply your own official firmware; the installer only reads it locally
  and never redistributes it.
- **ffmpeg** - diskOS no longer uses it for album artwork: as of 1.2.0 artwork is decoded by the
  bundled `diskos-artdec` helper (see above; `ui/art.c`). FiiO's stock firmware has its own ffmpeg on
  the device; the installer does **not** ship or call it for artwork.

---

### License and source notes
- Full license texts shipped in [`licenses/`](licenses/) (GPL-2.0, LGPL-2.1, BSD, PSF, LVGL-MIT,
  dropbear, Font Awesome OFL/CC-BY) - see [`licenses/README.md`](licenses/README.md).
- diskOS installer/tooling license: **MIT** (`LICENSE`). The diskOS **UI source** is published
  under **GPL-3.0-or-later** in `ui/` (`ui/COPYING`, `licenses/GPL-3.0.txt`) with its embedded
  LVGL/SQLite/jsmn/Font Awesome/Montserrat/Source Han Sans/IBM Plex Mono/Doto/theme-font notices shipped.
- Upstream versions and corresponding source are shipped in-tree for the GPL/LGPL native
  binaries: Linux squashfs-tools 4.6.1 and libusb 1.0.27; macOS arm64 squashfs-tools 4.7.5 and libusb 1.0.30; liblzo2 2.10, the SPL, and usbboot. Source is in [`corresponding-source/`](corresponding-source/) and [`spl-src/`](spl-src/).
  The SPL includes the unresolved DDR parameter block described in [`SPL_SOURCE.md`](SPL_SOURCE.md).
