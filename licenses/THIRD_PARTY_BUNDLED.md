# Third-party libraries bundled in a self-built onefile

> **Scope:** this applies **only if you build a self-contained PyInstaller onefile yourself**
> (`build/build.sh`). The **published diskOS release does not ship such a binary** - it distributes
> source, and the Python runtime + these native libraries come from *your* system Python (see
> [`../NOTICE.md`](../NOTICE.md)), so this project does not redistribute them. If you *do* build and
> redistribute the onefile, **you** become the redistributor of everything it embeds.
>
> The onefile embeds the CPython runtime plus **~90 native shared libraries** its GUI/runtime pulls
> in (CPython + Tkinter GUI + pycryptodome + pyusb chains). The table below lists the principal
> permissive families with verbatim texts in [`bundled/`](bundled/); it is **illustrative, not a
> complete manifest** of all ~90 entries. To regenerate the full list for a given build, walk the
> extracted onefile's shared objects and map each to its distribution package. Note the GUI chain
> also pulls in **libudev (LGPL-2.1)** and **freetype (FTL/GPL dual)** - a CLI-only build
> (`excludes=['tkinter']`) avoids both.

| Bundled library (`.so`) | License | Text |
|---|---|---|
| `libcrypto.so.3` (OpenSSL 3) | Apache-2.0 | [`bundled/libcrypto.copyright.txt`](bundled/libcrypto.copyright.txt) |
| `libbrotlicommon.so.1`, `libbrotlidec.so.1` (Brotli) | MIT | [`bundled/libbrotli.copyright.txt`](bundled/libbrotli.copyright.txt) |
| `libbsd.so.0` | BSD-2/3-Clause, ISC, etc. | [`bundled/libbsd.copyright.txt`](bundled/libbsd.copyright.txt) |
| `libbz2.so.1.0` (bzip2) | bzip2 (BSD-style) | [`bundled/libbz2.copyright.txt`](bundled/libbz2.copyright.txt) |
| `libcap.so.2` | BSD-3-Clause OR GPL-2.0-only (used under BSD) | [`bundled/libcap.copyright.txt`](bundled/libcap.copyright.txt) |
| `libX11.so.6` | MIT/X11 | [`bundled/libX11.copyright.txt`](bundled/libX11.copyright.txt) |
| `libXau.so.6` | MIT/X11 | [`bundled/libXau.copyright.txt`](bundled/libXau.copyright.txt) |
| `libXdmcp.so.6` | MIT/X11 | [`bundled/libXdmcp.copyright.txt`](bundled/libXdmcp.copyright.txt) |
| `libXext.so.6` | MIT/X11 | [`bundled/libXext.copyright.txt`](bundled/libXext.copyright.txt) |
| `libXft.so.2` | MIT/X11 | [`bundled/libXft.copyright.txt`](bundled/libXft.copyright.txt) |
| `libXrender.so.1` | MIT/X11 | [`bundled/libXrender.copyright.txt`](bundled/libXrender.copyright.txt) |
| `libXss.so.1` | MIT/X11 | [`bundled/libXss.copyright.txt`](bundled/libXss.copyright.txt) |
| `libBLT.2.5.so.8.6` (BLT, a Tk widget library) | BSD-style | [`bundled/libBLT.copyright.txt`](bundled/libBLT.copyright.txt) |

CPython itself is PSF-licensed (see [`PSF-Python.txt`](PSF-Python.txt)); the Python packages `pyusb`
(BSD) and `pycryptodome` (BSD / public-domain) are covered in [`README.md`](README.md).

These libraries are present **only in a self-built onefile**. The published release runs the
installer **from source** with your own Python (`./install.sh` then `./diskos-installer`), where they
come from *your* system's Python and are **not redistributed by this project**. (`bash build/build.sh`
is the separate, optional step that produces the onefile - if you run *that* and redistribute the
result, the obligations above become yours.)
