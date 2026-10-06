#!/usr/bin/env python3
"""Actual LVGL navigation/visibility/round-glass checks, plus reproducible Home/NP frames."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib
from artdec_test import png

app = Path(__file__).resolve().parents[1]


def save_png(ppm, output):
    header, pixels = ppm.read_bytes().split(b"\n255\n", 1)
    assert header == b"P6\n360 360" and len(pixels) == 360 * 360 * 3
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    filtered = b"".join(b"\0" + pixels[y*1080:(y+1)*1080] for y in range(360))
    output.write_bytes(b"\x89PNG\r\n\x1a\n" +
                       chunk(b"IHDR", struct.pack(">IIBBBBB",360,360,8,2,0,0,0)) +
                       chunk(b"IDAT",zlib.compress(filtered)) + chunk(b"IEND",b""))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, help="keep PNG frames in this directory")
    args = parser.parse_args()
    # The host executable is a diagnostic; this target does not certify the production theme or MIPS build.
    build=subprocess.run(["make", "-C",str(app),"CROSS=","host-render","-j1"],text=True,
                         stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    if build.returncode:
        print(build.stdout)
        raise SystemExit(build.returncode)
    exe = app / "tests/host_build/host-render"
    config = app / "tests/host_build/diskos.conf"
    cases = [(-1,-1,"migration"),(-2,0,"migration"),(-2,1,"migration"),(-2,2,"migration")]
    cases += [(p,v,"migration") for p in (0,2,7) for v in (-1,0,1)]
    cases += [(-2,0,"reset")]
    for prior,variant,page in cases:
        config.unlink(missing_ok=True)
        subprocess.run([str(exe),str(prior),str(variant),page,"0","/dev/null"],check=True)
    config.unlink(missing_ok=True)
    if args.out:
        args.out.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="diskos-render-test-") as tmp:
        for name, preset, variant in (("ring",8,0),("braun",9,1),("braun-dark",9,0)):
            subprocess.run([str(exe),str(preset),str(variant),"actions","0","/dev/null"],check=True)
            ppm=Path(tmp)/"immersive.ppm"
            ppm.with_suffix(".png").write_bytes(png(32,32,(209,57,12)))
            ppm.with_suffix(".lrc").write_text("[00:01]First lyric\n[00:02]Second lyric\n")
            subprocess.run([str(exe),str(preset),str(variant),"immersive","0",str(ppm)],check=True)
            if args.out: save_png(ppm,args.out/f"{name}-immersive.png")
            for page in ("home","np"):
                for enabled in (0,1):
                    ppm=Path(tmp) / "frame.ppm"
                    subprocess.run([str(exe),str(preset),str(variant),page,str(enabled),str(ppm)],check=True)
                    if args.out:
                        save_png(ppm,args.out / f"{name}-{page}-{'on' if enabled else 'off'}.png")
    print("PASS: 14 migration/reset cases, 3 navigation runs, 3 real-decoder immersive runs and 12 LVGL render/visibility checks")


if __name__ == "__main__":
    main()
