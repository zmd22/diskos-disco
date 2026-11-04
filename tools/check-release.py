#!/usr/bin/env python3
"""Fail before packaging if the visible version, source or shipped UI disagree."""
import argparse, hashlib, json, re, struct, subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--tag');a=p.parse_args()
version=re.search(r'#define DISCO_VERSION\s+"([^"]+)"',(root/'ui/version.h').read_text())[1]
_test=re.search(r'#define DISCO_TEST_LABEL\s+"([^"]*)"',(root/'ui/version.h').read_text())
if _test and _test[1]:raise SystemExit(f'ui/version.h: DISCO_TEST_LABEL is "{_test[1]}" - empty it for a release')
if a.tag and a.tag != 'v'+version: raise SystemExit(f'Tag {a.tag} does not match Disco {version}')
meta=json.loads((root/'payload/mq_ui.build.json').read_text())
assert meta['version']==version, 'payload version is stale'
data=(root/'payload/mq_ui').read_bytes()
assert meta['binary']['bytes']==len(data), 'payload size differs'
assert meta['binary']['sha256']==hashlib.sha256(data).hexdigest(), 'payload checksum differs'
assert data[:6]==b'\x7fELF\x01\x01', 'expected little-endian ELF32'
assert struct.unpack_from('<H',data,18)[0]==8, 'expected MIPS machine'
flags=struct.unpack_from('<I',data,36)[0]
assert flags & 0xf0000000 == 0x70000000 and flags & 0x1000 and flags & 0x400, 'expected MIPS32r2/o32/NaN2008 ABI'
# A dynamic interpreter would make this depend on unshipped target libraries.
phoff=struct.unpack_from('<I',data,28)[0];phsize,phnum=struct.unpack_from('<HH',data,42)
assert all(struct.unpack_from('<I',data,phoff+i*phsize)[0]!=3 for i in range(phnum)), 'expected static UI'
h=hashlib.sha256()
paths=subprocess.check_output(['git','ls-files','-z','ui'],cwd=root).split(b'\0')
for name in sorted(n for n in paths if n):h.update(name+b'\0'+(root/name.decode()).read_bytes()+b'\0')
assert h.hexdigest()==meta['ui_source_sha256'], 'UI source differs from payload build metadata'
assert (root/f'docs/releases/v{version}.md').is_file(), 'release notes missing'
assert (root/'licenses/OpenSans-OFL.txt').is_file(), 'Zune font licence missing'
print(f'PASS release {version}: source, version, static MIPS ELF and payload checksums agree')
