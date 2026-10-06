#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Parse real upstream schema-1 palettes with the merged store/validator; verify optional-role fallback."""
from pathlib import Path
import json
import re
import subprocess
import tempfile

app=Path(__file__).resolve().parents[1]
roles=re.findall(r'^\s*THEME_CLR_(\w+)\s*,', (app/'theme_roles.h').read_text(),re.M)
keys=dict(re.findall(r'\[THEME_CLR_(\w+)\]\s*=\s*"([a-z_0-9]+)"',(app/'theme_validate.c').read_text()))
optional=dict(re.findall(r'case THEME_CLR_(\w+): return THEME_CLR_(\w+);',(app/'theme_validate.c').read_text()))
def palette(name):
    src=(app/'theme_presets.inc').read_text(); block=src[src.index('static const uint32_t '+name):];block=block[:block.index('};')]
    return dict(re.findall(r'\[THEME_CLR_(\w+)\]\s*=\s*0x([A-Fa-f0-9]{6})',block))
base={'schema':1,'revision':1,'id':'custom','name':'Compatibility','accent_policy':'allow_album',
      'traits':{'background':'solid','now_playing':'standard','quick_settings':'fill'}}
for variant in ('dark','light'):
    pal=palette('PRESET_SOMETHING_'+variant.upper())
    base[variant]={key:pal[role] for role,key in keys.items() if role not in optional}
source=r'''
#include "theme_model.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv){
    assert(argc==3); char input[THEME_CUSTOM_MAX+1],output[THEME_CUSTOM_MAX+1],err[256];
    FILE *f=fopen(argv[1],"rb"); assert(f); int len=fread(input,1,sizeof input-1,f); fclose(f);
    theme_model_t m,again;
    int rc=theme_model_parse(input,len,&m,err,sizeof err);
    if(atoi(argv[2])){ assert(rc!=THEME_OK); return 0; }
    if(rc!=THEME_OK) fprintf(stderr,"parse: %s\n",err);
    assert(rc==THEME_OK);
    assert(theme_model_validate(&m,err,sizeof err)==THEME_OK);
    for(int v=0;v<2;v++) for(int r=0;r<THEME_CLR_COUNT;r++){
        int fallback=theme_role_fallback(r);
        if(fallback>=0) assert(m.pal[v][r]==m.pal[v][fallback]);
    }
    len=theme_model_serialize(&m,output,sizeof output); assert(len>0 && len<THEME_CUSTOM_MAX);
    assert(theme_model_parse(output,len,&again,err,sizeof err)==THEME_OK);
    assert(!memcmp(&m,&again,sizeof m));
    printf("PASS: upstream schema-1 fallback and full %d-byte component-role round trip\n",len);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='diskos-theme-compat-') as tmp:
    p=Path(tmp); (p/'test.c').write_text(source)
    subprocess.run(['cc','-D_GNU_SOURCE','-std=gnu11','-O1','-I'+str(app),str(p/'test.c'),
                    str(app/'theme_store.c'),str(app/'theme_validate.c'),'-lm','-o',str(p/'test')],check=True)
    def run(doc,bad=False):
        (p/'theme.json').write_text(json.dumps(doc))
        subprocess.run([str(p/'test'),str(p/'theme.json'),str(int(bad))],check=True)
    run(base)
    missing=json.loads(json.dumps(base)); del missing['dark']['canvas']; run(missing,True)
    unknown=json.loads(json.dumps(base)); unknown['light']['unknown_colour']='123456'; run(unknown,True)
    fixed=json.loads(json.dumps(base)); fixed['dark']['fixed_media_white']='000000';run(fixed,True)
print('PASS: missing original roles, unknown colours and immersive identity overrides still rejected')
print('COMPLETE theme_compat_test.py')
