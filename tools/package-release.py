#!/usr/bin/env python3
"""Package the verified UI, source pointers, licenses and instructions.
Disc Shelf is built into mq_ui (3.2.0+): the legacy standalone Disc-Shelf-1.0.4.tar and its source were last
published with 3.3.1 and are no longer packaged. apps/disc-shelf and its corresponding source stay in the repository
for anyone who received that binary."""
from pathlib import Path
import argparse,hashlib,json,re,shutil,subprocess,sys,tarfile,zipfile
from urllib.parse import urlsplit, unquote
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,default=root/'dist');a=p.parse_args()
subprocess.run([sys.executable,str(root/'tools/check-release.py')],check=True)
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
version=re.search(r'#define DISCO_VERSION\s+"([^"]+)"',(root/'ui/version.h').read_text()).group(1)
meta=json.loads((root/'payload/mq_ui.build.json').read_text())
assert meta['version']==version
assert hashlib.sha256((root/'payload/mq_ui').read_bytes()).hexdigest()==meta['binary']['sha256']
assert (root/'payload/mq_ui').stat().st_size==meta['binary']['bytes']
outputs=[]
for name in ['mq_ui','mq_ui.build.json']:
    shutil.copy2(root/'payload'/name,out/name);outputs.append(out/name)
# Windows app (built by the release workflow: windows/build.sh and windows/wsl/build-env.sh). The name has no version, so
# https://github.com/zmd22/diskos-disco/releases/latest/download/diskOS-Disco-for-Windows.zip always gets the newest.
win=root/'windows'/'out'
if (win/'diskOS-Disco.exe').is_file() and (win/'diskos-wsl.tar.gz').is_file():
    wz=out/'diskOS-Disco-for-Windows.zip'
    with zipfile.ZipFile(wz,'w',zipfile.ZIP_DEFLATED) as z:
        z.write(win/'diskOS-Disco.exe','diskOS-Disco.exe')
        z.write(win/'diskos-wsl.tar.gz','diskos-wsl.tar.gz',compress_type=zipfile.ZIP_STORED)   # already compressed
        z.write(root/'docs'/'WINDOWS.md','README.md')
        for name in ['LICENSE','NOTICE.md']:z.write(root/name,name)
    outputs.append(wz)
complete=out/f'diskos-disco-{version}-complete.zip'
with zipfile.ZipFile(complete,'w',zipfile.ZIP_DEFLATED) as z:
    z.write(out/'mq_ui','mq_ui');z.write(out/'mq_ui.build.json','mq_ui.build.json')
    def document(source, destination):
        # Repository-relative links break when a document is moved into the ZIP or release page.
        # Pin them to the release snapshot, keeping the original document's directory semantics.
        def pin(match):
            target=match[1]
            if urlsplit(target).scheme or target.startswith(('#','//')):return match[0]
            path,sep,fragment=target.partition('#')
            resolved=(source.parent/unquote(path)).resolve()
            rel=resolved.relative_to(root)
            if not resolved.exists():raise SystemExit(f'Missing document target: {source}: {target}')
            kind='tree' if resolved.is_dir() else 'blob'
            return ']('+f'https://github.com/zmd22/diskos-disco/{kind}/v{version}/'+rel.as_posix()+(sep+fragment if sep else '')+')'
        z.writestr(destination,re.sub(r'\]\(([^)\s]+)\)',pin,source.read_text()))
    document(root/'RELEASE_INSTALL.md','INSTALL.md')
    document(root/f'docs/releases/v{version}.md','RELEASE-NOTES.md')
    document(root/'README.md','README.md')
    for name in ['UPDATING.md','WINDOWS.md','MACOS.md','SWITCHING.md','ALBUM_ROULETTE_INSTALL.md','DISC_SHELF_INSTALL.md','DISPLAY_SETTINGS.md',f'RELEASE_{version}.md','themes/BRAUN2.md','themes/zune.md','IMMERSIVE_CONTROLS.md']:
        document(root/'docs'/name,'docs/'+name)
    z.write(root/'LICENSE','LICENSE')
    document(root/'NOTICE.md','NOTICE.md')
    document(root/'corresponding-source/README.md','corresponding-source/README.md')
    for license_file in sorted((root/'licenses').rglob('*')):
        if license_file.is_file():z.write(license_file,license_file.relative_to(root).as_posix())
# Validate archive-local Markdown links before publishing any checksum manifests.
with zipfile.ZipFile(complete) as z:
    names=set(z.namelist())
    for name in names:
        if not name.endswith('.md'):continue
        for target in re.findall(r'\]\(([^)\s]+)\)',z.read(name).decode()):
            if urlsplit(target).scheme or target.startswith(('#','//')):continue
            target=unquote(target.split('#',1)[0])
            import posixpath
            resolved=posixpath.normpath(posixpath.join(posixpath.dirname(name),target))
            if resolved not in names and not any(n.startswith(resolved.rstrip('/')+'/') for n in names):raise SystemExit(f'Broken package link: {name}: {target}')
outputs.append(complete)
for name,alg in [('SHA256SUMS','sha256'),('MD5SUMS','md5')]:
    (out/name).write_text(''.join(f'{hashlib.new(alg,f.read_bytes()).hexdigest()}  {f.name}\n' for f in outputs))
print('Packaged '+str(complete))
