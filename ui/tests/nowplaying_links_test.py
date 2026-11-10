#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real LVGL pointer tests for title/artist navigation; no player or device.

Build a fresh isolated host tree with:
make -C ui CROSS= EXTRA_CFLAGS='-DDB_PATH=\"/tmp/disco-now-playing-links.db\"' host-render
The explicit fixture DB path is checked by the renderer before using it.
"""
import argparse
import os
from pathlib import Path
import sqlite3
import struct
import subprocess
import tempfile
import zlib


def cover():
    def chunk(kind, data):
        return (struct.pack("!I", len(data)) + kind + data +
                struct.pack("!I", zlib.crc32(kind + data) & 0xffffffff))
    pixels = b"".join(b"\0" + bytes((42, 91, 155)) * 64 for _ in range(64))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack("!2I5B", 64, 64, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b""))


def database(path, track):
    # The driver refuses a pre-existing database instead of risking real data.
    assert not path.exists(), f"Remove only your prior test fixture first: {path}"
    with sqlite3.connect(path) as db:
        db.executescript("""
            CREATE TABLE SONG(ID INTEGER PRIMARY KEY, PATH, NAME, TITLE, ARTIST,
                ALBUM, ALBUM_ARTIST, GENRE, TRACK, DISC, DURATION, IS_CUE, IS_ISO);
            CREATE TABLE LIST_SONG_0(ID INTEGER PRIMARY KEY, PATH, NAME, TITLE,
                TRACK, IS_CUE, IS_ISO);
        """)
        rows = [
            (1, str(track), "Canonical Track", "Lead Artist, Guest Artist", "Core Album", "Album Collective", 1, 0),
            (2, str(track)+"-second", "Second Canonical Track", "Lead Artist, Guest Artist", "Core Album", "Album Collective", 2, 0),
            (3, str(track)+"-lead", "Lead Solo", "Lead Artist", "Lead LP", "Lead Artist", 1, 0),
            (4, str(track)+"-guest", "Guest Solo", "Guest Artist", "Guest LP", "Guest Artist", 1, 0),
            (5, str(track)+"-group", "Group Track", "Different Performer", "Group LP", "Album Collective", 1, 0),
            (6, str(track)+"-noalbum", "No Album", "Lead Artist", "", "Lead Artist", 1, 0),
            (7, str(track)+"-noartist", "No Artist", "", "No Artist Album", "", 1, 0),
            (8, str(track)+"-cue", "Cue One", "Cue Artist One", "Cue Album One", "Cue Artist One", 1, 1),
            (9, str(track)+"-cue", "Cue Two", "Cue Artist Two", "Cue Album Two", "Cue Artist Two", 2, 1),
        ]
        db.executemany("""INSERT INTO SONG(ID,PATH,NAME,TITLE,ARTIST,ALBUM,ALBUM_ARTIST,
            TRACK,IS_CUE,IS_ISO,GENRE,DISC,DURATION) VALUES(?,?,?, ?,?,?,?,?,?,0,'Test',1,240000)""",
                       [(i, p, title+".flac", title, artist, album, group, number, cue)
                        for i, p, title, artist, album, group, number, cue in rows])
        db.execute("INSERT INTO LIST_SONG_0 VALUES(41,?,?,?,?,1,0)",
                   (str(track)+"-cue", "Cue Two.flac", "Cue Two", 2))


def main():
    app = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--renderer", type=Path, default=app/"tests/host_build/host-render")
    parser.add_argument("--preset", type=int, action="append")
    parser.add_argument("--variant", type=int, default=0)
    parser.add_argument("--out", type=Path, help="retain the final 360px PPM frames")
    args = parser.parse_args()
    db = Path("/tmp/disco-now-playing-links.db")
    assert args.renderer.is_file()
    presets = args.preset or [6, 8, 9, 10, 11, 12, 13]
    with tempfile.TemporaryDirectory(prefix="disco-now-playing-links-") as directory:
        root = args.out or Path(directory)
        root.mkdir(parents=True, exist_ok=True)
        cases = [(p, 1, False, False) for p in presets]
        if 6 in presets: cases += [(6, style, False, False) for style in (0, 2, 3)]
        cases += [(p, 1, True, False) for p in presets if p in (10, 11, 12, 13)]
        if 11 in presets: cases += [(11, 1, False, True)]
        for preset, style, long_text, gestures in cases:
            name = f"{preset}-{style}-{'long' if long_text else 'short'}-{'gesture' if gestures else 'buttons'}"
            output = root/(name+".ppm")
            output.with_suffix(".png").write_bytes(cover())
            database(db, output.with_suffix(".png"))
            env = {**os.environ, "REVIEW_NP_LINKS": "1", "NP_LINKS_DB": str(db),
                   "NP_LINKS_STYLE": str(style), "NP_PLAYING": "1"}
            if long_text: env["NP_LINKS_LONG"] = "1"
            if gestures: env["NP_LINKS_GESTURES"] = "1"
            try:
                result = subprocess.run([str(args.renderer), str(preset), str(args.variant),
                                         "now-playing-links", "0", str(output)],
                                        env=env, capture_output=True, text=True, timeout=35)
                print(f"{name}: {result.stdout.strip()}", flush=True)
                if result.returncode:
                    raise RuntimeError(f"{name}: exit {result.returncode}\n{result.stderr}")
                assert "PASS now-playing links:" in result.stdout
            finally:
                db.unlink(missing_ok=True)
                for suffix in ("-wal", "-shm", "-journal"):
                    Path(str(db)+suffix).unlink(missing_ok=True)
    print("PASS: real pointer title/artist navigation, canonical destinations and guarded gestures")


if __name__ == "__main__":
    main()
