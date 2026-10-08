# Bundled Album Roulette 1.3.0

`album-roulette` is the static MIPS32r2 FP64 NaN2008 executable compiled from
`corresponding-source/album-roulette-1.3.0-source.tar.gz`. GPL-3.0-or-later; see
ALBUM-ROULETTE-NOTICE.md and the source archive's LICENSE, NOTICE.md and licenses/.

Extract the archive, then run its Makefile with CROSS pointing at the MIPS musl toolchain
(GCC 11.2.0, musl 1.2.6, toolchain recipe included). Copy build/mips/album-roulette here.
The UI Makefile embeds this file through bundled_album.S; ordinary mq_ui updates carry it too.

On UI startup, missing app files are installed under /usr/data/apps/album-roulette.
The executable is written in 32 KiB chunks to a temporary file, synced and linked atomically.
An existing executable or app.conf is preserved. No app starts during installation.
The landing button retries installation if startup installation failed.

Binary SHA-256: 3413e39fd696d3fc527b5046bf4473ce3d78e0158b37feeeace9c671c0b07839
