#!/usr/bin/env python3
"""Check renderer bounds and stationary fade against bright covers at several angles."""
from pathlib import Path
import subprocess,tempfile,os
app=Path(__file__).resolve().parents[1]
h=r'''
#include "imm_record.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
int main(void){
 uint32_t *src=malloc(364*364*4),*dst=calloc(360*360,4);assert(src&&dst);
 for(int i=0;i<364*364;i++)src[i]=0xFFFFFFFFu;
 int angles[]={0,450,900,1367,1800,2700,3599};
 for(int smooth=0;smooth<=1;smooth++){
 imm_record_set_smoothing(smooth);
 for(unsigned a=0;a<sizeof angles/sizeof angles[0];a++){
  imm_record_render(src,364,364,dst,angles[a],170);
  assert(dst[189*360+180]==0xFFFFFFFFu);
  assert(dst[190*360+180]==0xFFFFFFFFu);
  assert(dst[300*360+180]==0xFF313131u);
  assert(dst[359*360+180]==0xFF313131u);
  imm_record_render(src,364,364,dst,angles[a],240);
  assert(dst[120*360+180]==0xFFFFFFFFu);
  assert(dst[190*360+180]!=0xFFFFFFFFu);
  assert(dst[359*360+180]==0xFF313131u);
 }
 }
 /* Sharp cover changes replace the image, including an old frame's bright pixels. */
 for(int i=0;i<364*364;i++)src[i]=0xFF000000u;
 imm_record_render(src,364,364,dst,900,170);assert(dst[180*360+180]==0xFF000000u);
 free(src);free(dst);puts("PASS: original fade at 7 angles and two lyric heights, both quality modes, cover refresh; ASan/UBSan bounds");
}
'''
with tempfile.TemporaryDirectory(prefix='diskos-imm-record-') as tmp:
 src=Path(tmp)/'test.c';src.write_text(h);exe=Path(tmp)/'test'
 subprocess.run(['gcc','-std=gnu11','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(app),str(src),str(app/'imm_record.c'),'-lm','-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,env={**os.environ,"ASAN_OPTIONS":"detect_leaks=0"})
