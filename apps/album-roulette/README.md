# Album Roulette 1.3.1

Standalone user app, shipped alongside Disco 1.2.2. It is not linked into mq_ui.
Copy app and app.conf to /usr/data/apps/album-roulette; app must be executable.
Close Roulette before replacing it. Keep an existing app.conf if customized.
See docs/ALBUM_ROULETTE_INSTALL.md for commands.

This revision shares one fallback image and bounds uncached heap artwork to three
covers. Cached covers use reclaimable file mappings. Decoder memory guards remain
unchanged, so very low available RAM can still produce placeholders.

Complete source: corresponding-source/album-roulette-1.3.1-source.tar.gz.
Extract and run make CROSS=/path/to/mipsel-linux-musl- using the included toolchain
recipe (GCC 11.2.0, musl 1.2.6). Copy build/mips/album-roulette to apps/album-roulette/app.
Licenses: NOTICE.md and the source archive's LICENSE, NOTICE.md and licenses/.
