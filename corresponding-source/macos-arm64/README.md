# macOS arm64 native-tool corresponding source

These are the exact upstream sources used by the Homebrew formulas for the native tools
in `vendor/macos-arm64/`. Archive SHA-256 values match the formula pins. The formula
snapshots in `homebrew-formulas/` record build options, dependencies and bottle inputs.

| File | Component | Version | License | SHA-256 |
|---|---|---|---|---|
| `squashfs-tools-4.7.5.tar.gz` | mksquashfs, unsquashfs | 4.7.5 | GPL-2.0-or-later | `547b7b7f4d2e44bf91b6fc554664850c69563701deab9fd9cd7e21f694c88ea6` |
| `squashfs-tools-4.7.5-f88f4a65.patch` | Homebrew Darwin build backport | f88f4a65 | GPL-2.0-or-later (upstream patch) | `3f3f568514c57fd50f508fef67e0e293a9668067801f42d4471b429a79bd1575` |
| `lzo-2.10.tar.gz` | liblzo2.2.dylib | 2.10 | GPL-2.0-or-later | `c0f892943208266f9b6543b3ae308fab6284c5c90e627931446fb49b4221a072` |
| `libusb-1.0.30.tar.bz2` | libusb-1.0.0.dylib | 1.0.30 | LGPL-2.1-or-later | `fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf` |
| `lz4-1.10.0.tar.gz` | liblz4.1.10.0.dylib | 1.10.0 | BSD-2-Clause (library) | `537512904744b35e232912055ccf8ec66d768639ff3abe5788d90d792ec5f48b` |
| `xz-5.8.4.tar.gz` | liblzma.5.dylib | 5.8.4 | 0BSD (liblzma); GPL-2.0-or-later parts in source | `0014c7886930454fe8bd4228665b51af55eeae560ea135c9c4cd33f55b2591d9` |
| `zstd-1.5.7.tar.gz` | libzstd.1.5.7.dylib | 1.5.7 | BSD-3-Clause or GPL-2.0-only (library); MIT/BSD-2-Clause parts in source | `37d7284556b20954e56e1ca85b80226768902e2edabd3b649e9e72c0c9012ee3` |

`vendor/setup-macos.sh` is the project build recipe: it installs Homebrew libusb,
squashfs, lzo, lz4, xz and zstd at the versions above; compiles `usbboot` from
`src/usbboot/usbboot.c`; copies Homebrew's lzo-capable `mksquashfs` and
`unsquashfs`; and uses `dylibbundler` to bundle the five dylibs and rewrite load
paths to `@loader_path/lib/`. The squashfs formula applies the included patch.
The formula files here show the upstream compilation steps. On macOS arm64, use
the same pinned Homebrew formulas and `vendor/setup-macos.sh` to rebuild.

libusb is LGPL-2.1-or-later. The included `usbboot` source and setup recipe allow
rebuilding and relinking it against a modified libusb as LGPL-2.1 section 6
requires. The library's own source and license are included here and in
`licenses/LGPL-2.1-libusb.txt`.
