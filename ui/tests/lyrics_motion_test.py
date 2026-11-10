#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real LVGL lyric motion/input tests; build host-render first, no player required."""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib


def chunk(kind, data):
    return (struct.pack("!I", len(data)) + kind + data +
            struct.pack("!I", zlib.crc32(kind + data) & 0xffffffff))


def cover():
    rows = b"".join(b"\0" + b"".join(bytes((40+x*2, 36+y*2, 125+x))
                                    for x in range(64)) for y in range(64))
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack("!2I5B", 64, 64, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def main():
    ui = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--renderer", type=Path, default=ui/"tests/host_build/host-render")
    parser.add_argument("--out", type=Path, help="keep 360px PPM animation frames here")
    parser.add_argument("--preset", type=int, action="append", help="default: Disco, Zune, Ring")
    args = parser.parse_args()
    assert args.renderer.is_file(), "Build with make -C ui CROSS= host-render first"
    with tempfile.TemporaryDirectory(prefix="disco-lyrics-motion-") as temporary:
        output = args.out or Path(temporary)
        output.mkdir(parents=True, exist_ok=True)
        for preset in args.preset or [10, 13, 8]:
            ppm = output/f"lyrics-{preset}.ppm"
            ppm.with_suffix(".png").write_bytes(cover())
            result = subprocess.run([str(args.renderer), str(preset), "0", "lyrics-motion", "0", str(ppm)],
                                    env={**os.environ, "REVIEW_LYRICS_MOTION": "1", "DISCO_SIDE": "0"},
                                    capture_output=True, text=True, timeout=45)
            print(f"preset {preset}: {result.stdout.strip()}", flush=True)
            if result.returncode:
                raise RuntimeError(f"lyrics-motion exited {result.returncode}: {result.stderr}")
            frames = list(output.glob(ppm.name + ".*.ppm"))
            assert len(frames) >= 10
            for frame in frames:
                header, pixels = frame.read_bytes().split(b"\n255\n", 1)
                assert header == b"P6\n360 360" and len(pixels) == 360*360*3
    print("PASS: actual LVGL lyrics behavior and 360px render frames")


if __name__ == "__main__":
    main()
