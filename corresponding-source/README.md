# Corresponding source for the shipped GPL/LGPL native binaries

The installer ships prebuilt native tools under `vendor/<os>-<arch>/`. Some of them are, or
statically link, **GPL-2.0** / **LGPL-2.1** code. To satisfy those licenses this directory carries the
**complete corresponding source** for the exact upstream versions those binaries were built from - so
the source travels **from the same place** as the binaries (GPL-2.0 section 3(a) + the "same place" clause
in section 3's final paragraph; LGPL-2.1 section 6(a)/(d)).

Each item below is a full Debian source package (`.orig` upstream tarball + `.debian` packaging +
`.dsc`); extract with `dpkg-source -x <name>.dsc` (or unpack the `.orig` tarball directly).

| Shipped binary | Component here | Version | License | Source files |
|---|---|---|---|---|
| `mksquashfs` / `unsquashfs` | **squashfs-tools** | 4.6.1-1build1 | GPL-2.0 | `squashfs-tools_4.6.1*` |
| (statically linked into the above) | **liblzo2** (lzo2) | 2.10-2build4 | GPL-2.0-or-later | `lzo2_2.10*` |
| `usbboot` | **libusb-1.0** (statically linked) | 1.0.27-1 | LGPL-2.1 | `libusb-1.0_1.0.27*` |
| all three tools (static C runtime) | **glibc** | 2.39-0ubuntu8.8 | LGPL-2.1-or-later | `glibc_2.39*` |
| `usbboot` | its own C source | - | GPL-2.0-or-later | `../src/usbboot/usbboot.c` |
| `disc_spl_lpddr3.bin` | stage-1 SPL (u-boot-xburst) | see `../SPL_SOURCE.md` | GPL-2.0 | `../spl-src/` |

Also statically linked into `mksquashfs`/`unsquashfs`: **zlib** and **liblzma/xz** - both permissive
(zlib / 0BSD-public-domain), needing attribution only (see `../NOTICE.md`), not corresponding source.

## Rebuilding the binaries from this source

The build scripts that produced the shipped binaries are in `../build/`:
- `build/build-squashfs-static.sh` - static `mksquashfs`/`unsquashfs` (gzip+lzo+xz), with an LZO
  round-trip self-test. Uses squashfs-tools + liblzo2 above.
- `build/build-usbboot-static.sh` - fully static `usbboot`, rebuilding libusb (above) with
  `--disable-udev --enable-static` so no libudev/libcap is pulled in.

Those scripts fetch the same upstream source via `apt-get source`; the tarballs here pin the exact
versions used so you can rebuild - or substitute your own modified libusb and relink `usbboot`
(the LGPL-2.1 section 6 relink path) - without depending on a distribution's source availability.

The macOS arm64 tools use separate Homebrew inputs: squashfs-tools 4.7.5,
libusb 1.0.30, lzo 2.10, lz4 1.10.0, xz 5.8.4 and zstd 1.5.7.
Their archives, squashfs patch, formula snapshots and relink instructions
are in [`macos-arm64/README.md`](macos-arm64/README.md).

## The LGPL-2.1 relink path for libusb (usbboot)

`usbboot` statically links `libusb-1.0.a`. To exercise your LGPL right to run `usbboot` against a
modified libusb: extract `libusb-1.0_1.0.27*`, modify it, `./configure --disable-udev --enable-static
--disable-shared && make`, then relink with the app source and the same command
`build/build-usbboot-static.sh` uses (`gcc -O2 -std=c99 -I<libusb-inc> -static -o usbboot
../src/usbboot/usbboot.c <your libusb-1.0.a> -lpthread`).
