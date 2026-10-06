# Dependency inventory - generated from a self-built onefile artifact

> **Scope note:** this inventory describes a **self-built PyInstaller onefile**. The **published
> diskOS release does NOT ship that onefile** - it distributes source and runs from your own Python
> (see [`NOTICE.md`](../NOTICE.md) / [`licenses/THIRD_PARTY_BUNDLED.md`](../licenses/THIRD_PARTY_BUNDLED.md)),
> so these bundled libraries are not redistributed by this project. This file is retained for anyone
> who chooses to build and redistribute the onefile themselves (they take on these obligations).


This is an artifact-derived list from a PyInstaller onefile build: ~93 distinct native `.so` entries
were bundled. Below are the license-bearing shared libraries PyInstaller pulled in transitively
(CPython + Tkinter GUI + pycryptodome + pyusb chains), grouped by license obligation. They matter
**only if you build and redistribute the onefile yourself**; the published source release does not
bundle them.

## Copyleft - obligations to satisfy (priority)
| Library | Bundled as | License | Obligation |
|---|---|---|---|
| **libudev** (systemd) | `libudev.so.1` | **LGPL-2.1** | ship license text + section 6 relink materials (or drop it) |
| **FreeType** | `libfreetype.so.6` | **FTL or GPLv2 (dual)** | ship FTL text (permissive path) + attribution |
| **libcap** | `libcap.so.2` | BSD-3 / GPLv2 (dual) | permissive path: BSD text |

> NOTE: `libudev` (LGPL) is bundled only because of the **Tkinter GUI**'s X/font chain. Consider a
> `--nowindow`/CLI-only build, or explicitly excluding `libudev`, to remove the LGPL relink burden.

## Permissive - attribution/notice only (ship license texts)
| Library | Bundled as | License |
|---|---|---|
| OpenSSL 3 | `libssl.so.3`, `libcrypto.so.3` | Apache-2.0 |
| Tcl / Tk 8.6 | `libtcl8.6.so`, `libtk8.6.so` | TCL/Tk (BSD-style) |
| libpng | `libpng16.so.16` | PNG Reference License |
| zlib | `libz.so.1` | zlib |
| xz / liblzma | `liblzma.so.5` | 0BSD / public-domain |
| bzip2 | `libbz2.so.1.0` | bzip2 (BSD-style) |
| libffi | `libffi.so.8` | MIT |
| fontconfig | `libfontconfig.so.1` | MIT-style |
| expat | `libexpat.so.1` | MIT |
| brotli | `libbrotlicommon/dec` | MIT |
| X11 client libs | `libX11/Xext/Xrender/Xft/Xau/Xdmcp/Xss` | MIT (X11) |
| BLT | `libBLT*` | BSD-style |

## Already in NOTICE.md (direct deps)
CPython (PSF), pyusb (BSD-3), pycryptodome (BSD-2 + public-domain), PyInstaller bootloader
(GPL-2.0 + bootloader exception). The pycryptodome `_raw_*`/`_ghash_*` etc. modules are part of it.

## If you build and redistribute the onefile yourself

The published release does **not** ship the onefile, so none of the above is an obligation for it. If
you choose to build one (`build/build.sh`) and hand it to others, **you** become the redistributor of
everything it embeds and must satisfy those licenses - in particular the LGPL `libudev` and the
dual-licensed FreeType pulled in by the Tkinter GUI. The simplest way to avoid that burden is a
CLI-only build that excludes `tkinter`. To enumerate exactly what a given onefile bundles, walk the
extracted binary's shared objects and map each back to its distribution package.
