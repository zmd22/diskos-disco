#!/usr/bin/env python3
"""Package the built UI and standalone user app; never embed the app in mq_ui."""
from pathlib import Path
import argparse
import hashlib
import re
import shutil
import tarfile
import zipfile

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, default=root / 'dist')
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
version = re.search(r'#define DISCO_VERSION\s+"([^"]+)"', (root / 'ui/version.h').read_text()).group(1)
app = root / 'apps/album-roulette'
source = root / 'corresponding-source/album-roulette-1.3.1-source.tar.gz'
shutil.copy2(root / 'payload/mq_ui', out / 'mq_ui')
app_archive = out / 'Album-Roulette-1.3.1.tar.gz'
with tarfile.open(app_archive, 'w:gz') as archive:
    archive.add(app, arcname='album-roulette')
complete = out / f'diskos-disco-{version}-complete.zip'
with zipfile.ZipFile(complete, 'w', zipfile.ZIP_DEFLATED) as archive:
    archive.write(root / 'payload/mq_ui', 'mq_ui')
    archive.write(root / 'docs/ALBUM_ROULETTE_INSTALL.md', 'INSTALL.md')
    archive.write(root / f'docs/releases/v{version}.md', 'RELEASE-NOTES.md')
    archive.write(source, 'corresponding-source/' + source.name)
    for path in sorted(app.rglob('*')):
        if path.is_file():
            archive.write(path, path.relative_to(root))
for name, algorithm in [('SHA256SUMS', 'sha256'), ('MD5SUMS', 'md5')]:
    lines = []
    for path in [out / 'mq_ui', app_archive, complete]:
        h = hashlib.new(algorithm)
        with path.open('rb') as f:
            for block in iter(lambda: f.read(1024 * 1024), b''):
                h.update(block)
        lines.append(f'{h.hexdigest()}  {path.name}\n')
    (out / name).write_text(''.join(lines))
print(f'Packaged Disco {version}: {complete}')
