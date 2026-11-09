#!/usr/bin/env python3
"""Run focused real-LVGL Display/idle regressions; no device or real radios."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

ui=Path(__file__).resolve().parents[1]
renderer=ui/'tests/host_build/host-render'
assert renderer.is_file(), 'Build with make -C ui CROSS= -j1 host-render first'
def chunk(kind,data):
    return struct.pack('!I',len(data))+kind+data+struct.pack('!I',zlib.crc32(kind+data)&0xffffffff)
png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!2I5B',360,360,8,2,0,0,0))+chunk(b'IDAT',zlib.compress((b'\x00'+b'\x50\x50\x50'*360)*360))+chunk(b'IEND',b'')
cases=[(theme,'REVIEW_DISPLAY') for theme in [10,11,12,8,9]]+[
    (11,'REVIEW_DISPLAY_IDLE'),(11,'REVIEW_NULL'),(11,'REVIEW_NULL_GESTURES'),(11,'REVIEW_NULL_VOLUME'),
    (12,'REVIEW_MODES'),(11,'REVIEW_MODES'),(10,'REVIEW_NAV'),(10,'REVIEW_HINTS'),
    (10,'REVIEW_DISCO_SETTINGS'),(12,'REVIEW_SQUASH_FOLLOW'),(11,'REVIEW_SQUASH_ACCENT'),
    (10,'REVIEW_HELP_MENU'),(11,'REVIEW_HELP_MENU'),(12,'REVIEW_HELP_MENU')]
with tempfile.TemporaryDirectory(prefix='disco-display-') as directory:
    out=Path(directory)/'fixture.ppm';out.with_suffix('.png').write_bytes(png)
    for theme,test in cases:
        result=subprocess.run([str(renderer),str(theme),'0','preview-home','0',str(out)],env={**os.environ,test:'1','DISCO_SIDE':'0'},capture_output=True,text=True,timeout=45)
        print(f'{theme} {test}: {result.stdout.strip()}',flush=True)
        if result.returncode:raise RuntimeError(f'{test} failed ({result.returncode}): {result.stderr}')
print('PASS: focused Display and visual-idle regression suite')
