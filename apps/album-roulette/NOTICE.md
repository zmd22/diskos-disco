Album Roulette 1.3.1 — GPL-3.0-or-later.

Uses diskOS / diskOS Disco! code by diskOS contributors, from
https://github.com/zmd22/diskos-disco commit d9a3be3a140bfd8d790222fd9bec4f226db2feb7.
The corresponding source archive includes the used routines and headers.
The vendored musicdb.c is adapted to omit Disco-owned schema migrations and to
permit a test database path override; album planning code is retained.

LVGL 9.2.2: MIT, see licenses/LVGL.txt in the source archive.
SQLite: public domain, see the header of vendor/sqlite3.c.
Bundled font families: SIL Open Font License 1.1, see licenses/OFL-*.txt.
Disco theme engine is included with a read-only standalone settings adapter;
button component rules are adapted from its theme kits.

App rendering follows the framebuffer/touch rotation mapping from diskOS.
Artwork uses the diskos-artdec helper already installed by Disco.
This is an independent homebrew app, not an official FiiO product.

Roulette-rim numeral glyphs: DejaVu Sans; see licenses/DejaVu.txt.
