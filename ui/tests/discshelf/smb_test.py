#!/usr/bin/env python3
"""SMB3 wire fixture plus actual backend: bounded browsing, safe copies and credentials.
The fixture uses upstream libsmb2's server API, not an independent NAS implementation.
Samba/device interoperability is a separate hardware check.
"""
from pathlib import Path
import hashlib,os,socket,subprocess,tempfile,time
ui=Path(__file__).resolve().parents[2];here=Path(__file__).resolve().parent
subprocess.run(['python3',str(ui/'tools/build_smb.py'),'--cc','cc','--out',str(ui/'vendor/smb-build')],check=True)
lib=ui/'vendor/smb-build/libsmb2.a';env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'}
with tempfile.TemporaryDirectory(prefix='disco-smb-test-') as temp:
 root=Path(temp);exe=root/'client';server=root/'server';connection=root/'connection'
 subprocess.run(['cc','-g','-pthread','-fsanitize=address,undefined','-I'+str(ui),'-I'+str(ui/'vendor/libsmb2/include'),str(here/'smb_driver.c'),*[str(ui/'discshelf'/x) for x in ['engine.c','net.c','json.c','connection.c','smb.c','smb_publish.c','lifecycle.c']],str(ui/'md5.c'),str(ui/'sqlite3.o'),str(lib),'-lm','-ldl','-o',str(exe)],check=True)
 subprocess.run(['cc','-g','-I'+str(ui/'vendor/libsmb2/include'),str(here/'smb_fixture_server.c'),str(lib),'-o',str(server)],check=True)
 subprocess.run(['cc','-g','-pthread','-fsanitize=address,undefined','-I'+str(ui),str(here/'connection_test.c'),str(ui/'discshelf/connection.c'),'-o',str(connection)],check=True)
 cdir=root/'config-test';cdir.mkdir();subprocess.run([str(connection),str(cdir)],env=env,check=True)
 share=root/'share';dest=root/'dest';share.mkdir();dest.mkdir();(share/'Album').mkdir();(share/'Album/Empty directory').mkdir();(share/'Album/Skipped link').symlink_to(root/'outside',target_is_directory=False)
 (share/'Album/01.flac').write_bytes(b'fLaC'+b'a'*90000);(share/'Album/empty.txt').write_bytes(b'');(share/'Album/Artist – 日本語.txt').write_text('Unicode')
 # Complete multiple large files on one connection, including a final short read.
 # The separate 32 MiB cancellation fixture below never tests successful completion.
 large_hashes={}
 for index,mib in enumerate((21,33),2):
  relative=f'Album/{index:02d}-large.m4a';digest=hashlib.sha256()
  with (share/relative).open('wb') as f:
   for sequence in range(mib*16):
    block=sequence.to_bytes(4,'little')+bytes(range(256))*255+bytes([index])*252
    f.write(block);digest.update(block)
   tail=f'file-{index}-tail'.encode();f.write(tail);digest.update(tail)
  large_hashes[relative]=((share/relative).stat().st_size,digest.hexdigest())
 for i in range(85):(share/f'Track {i:03}.flac').write_bytes(b'fLaC'+bytes([i])*256)
 (share/'Unowned.flac').write_bytes(b'fLaCremote');(dest/'Unowned.flac').write_bytes(b'KEEP ME')
 with (share/'ZZ-large.bin').open('wb') as f:
  for _ in range(512):f.write(b'X'*65536)
 def run_case(mode,password='DiscShelfTest42!',version='0x300'):
  with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
  conf=root/(mode+'-'+version+'.conf');conf.write_text(f'source=smb\nsmb_server=127.0.0.1:{port}\nsmb_share=Music\nsmb_username=root\nsmb_password={password}\nsmb_download_dir={dest}\n');conf.chmod(0o600)
  log=root/'server.log'
  with log.open('w') as output:
   proc=subprocess.Popen([str(server),str(share),str(port),version],stdout=output,stderr=output,env=env)
   try:
    time.sleep(.15)
    result=subprocess.run([str(exe),str(conf),mode,str(dest),str(share)],env=env,text=True,capture_output=True,timeout=100)
    if result.returncode:print(result.stdout,result.stderr,log.read_text());result.check_returncode()
    print(result.stdout.strip())
   finally:
    proc.terminate();proc.wait(timeout=10)
 run_case('happy')
 assert (dest/'Unowned.flac').read_bytes()==b'KEEP ME'
 assert (dest/'Album/01.flac').read_bytes()==b'fLaCNEW CONTENT'
 assert (dest/'Album/empty.txt').stat().st_size==0
 assert (dest/'Album/Empty directory').is_dir()
 assert not (dest/'Album/Skipped link').exists()
 assert (dest/'Album/Artist – 日本語.txt').read_text()=='Unicode'
 for relative,(size,wanted) in large_hashes.items():
  assert (dest/relative).stat().st_size==size
  digest=hashlib.sha256()
  with (dest/relative).open('rb') as f:
   for block in iter(lambda:f.read(65536),b''):digest.update(block)
  assert digest.hexdigest()==wanted,relative
 print('PASS completed 21/33 MiB album tracks and final short reads match exact SHA-256')
 assert (dest/'ZZ-large.bin').read_bytes()==b'ORIGINAL'
 assert not list(dest.rglob('*.part'))
 run_case('reject',version='0x210')
 run_case('reject',password='wrong-test-password')
 print('PASS SMB2-only and bad-password connections rejected; no protocol downgrade')
