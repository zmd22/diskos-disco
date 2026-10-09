/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Production shortcut resolution and real configuration persistence. */
#include <assert.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../apps.c"
int main(void){
    assert(mkdir(CFG_DIR,0755)==0 || errno==EEXIST); unlink(CFG_PATH);
    cfg_load(); g_napps=2;
    snprintf(g_apps[0].name,sizeof g_apps[0].name,"Radio");
    snprintf(g_apps[1].name,sizeof g_apps[1].name,"Radio");
    md5_hex("radio-one",9,g_apps[0].id); md5_hex("radio-two",9,g_apps[1].id);
    char key[80]; snprintf(key,sizeof key,"appid:%s",g_apps[1].id);
    assert(app_for_key(key)==1); assert(app_for_key("app:Radio")==-2);
    assert(!cfg_set_str("sc1",key)); assert(!strcmp(cfg_get_str("sc1",""),key));
    puts("PASS: duplicate display names resolve to distinct apps; ambiguous legacy names are rejected");
    memset(g_apps[1].name,'x',63); g_apps[1].name[63]=0;
    const char *glyph; char name[64]; int font;
    assert(sc_describe(key,&glyph,name,sizeof name,&font)); assert(!strcmp(name,g_apps[1].name));
    assert(app_for_key(cfg_get_str("sc1",""))==1);
    puts("PASS: a 63-byte display name saves and resolves without increasing configuration limits");
    assert(app_for_key("app:Radio")==0); assert(app_for_key("appid:missing")==-1);
    puts("PASS: unique legacy name shortcuts remain compatible; missing apps do not resolve");
    unlink(CFG_PATH); rmdir(CFG_DIR); return 0;
}
