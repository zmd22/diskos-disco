#!/usr/bin/env python3
"""Keep MIPS objects and native objects separate; retain all incremental build state."""
import argparse, hashlib, json, os, shutil, subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--toolchain',required=True,type=Path);p.add_argument('--cache',required=True,type=Path);p.add_argument('--out',required=True,type=Path);p.add_argument('--approved',action='store_true');a=p.parse_args()
source=Path(__file__).resolve().parents[1];cache=a.cache.resolve();cache.mkdir(parents=True,exist_ok=True)
skip={'host_build','__pycache__','.git','config.mk','mq_ui','mq_ui.debug','mq_ui.manifest','diskos-artdec','diskos-bootprobe'}
digest=hashlib.sha256()
# Use publishable inputs when in git; an extracted GitHub tree also builds without .git.
listed=subprocess.run(['git','-C',str(source.parent),'ls-files','--cached','--others','--exclude-standard','-z','--','ui'],capture_output=True)
paths=[source.parent / os.fsdecode(x) for x in listed.stdout.split(b'\0') if x] if listed.returncode==0 else list(source.rglob('*'))
for path in sorted(paths):
 rel=path.relative_to(source)
 if not path.is_file() or any(x in skip for x in rel.parts) or path.suffix in ('.o','.d'):continue
 data=path.read_bytes();digest.update(str(rel).encode()+b'\0'+data);dest=cache/rel;dest.parent.mkdir(parents=True,exist_ok=True)
 if not dest.exists() or dest.read_bytes()!=data:shutil.copy2(path,dest);os.utime(dest,None)
prefix=str(a.toolchain.resolve()/'bin/mipsel-linux-musl-')
subprocess.run(['make','-C',str(cache),'CROSS='+prefix,'-j8','mq_ui','diskos-artdec','diskos-bootprobe'],check=True)
a.out.mkdir(parents=True,exist_ok=True);artifacts={}
for name in ('mq_ui','diskos-artdec','diskos-bootprobe'):
 binary=cache/name;hdr=subprocess.check_output([prefix+'readelf','-h','-A',str(binary)],text=True)
 assert 'MIPS' in hdr and 'nan2008' in hdr and '32r2' in hdr and 'Hard float (32-bit CPU, 64-bit FPU)' in hdr,hdr
 segments=subprocess.check_output([prefix+'readelf','-l',str(binary)],text=True);assert 'INTERP' not in segments
 data=binary.read_bytes();target=a.out/name
 if target.exists() and target.read_bytes()!=data:raise SystemExit('Refusing to overwrite a different release: '+str(target))
 shutil.copy2(binary,target);artifacts[name]={'sha256':hashlib.sha256(data).hexdigest(),'md5':hashlib.md5(data).hexdigest(),'bytes':len(data)}
 subprocess.run([prefix+'readelf','-h','-A',str(binary)],stdout=(a.out/(name+'.elf.txt')).open('w'),check=True)
ref=subprocess.run(['git','-C',str(source),'rev-parse','HEAD'],capture_output=True,text=True)
manifest={'source_sha256':digest.hexdigest(),'base_commit':ref.stdout.strip() if ref.returncode==0 else None,'working_changes':True,'compiler':subprocess.check_output([prefix+'gcc','--version'],text=True).splitlines()[0],'toolchain_recipe_commit':'227df8b99103f9c59f6570babf892978e293082f','visual_approval':'approved' if a.approved else 'pending','artifacts':artifacts}
(a.out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(manifest,indent=2))
