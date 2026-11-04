#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""3.4.0 codec step after a connection (production bt_codec_upgrade_step and the crash guard from bt.c):
- LDAC only on FiiO's BlueALSA, aptX only on the aptX build: a pair whose best codec the running BlueALSA can't run is
  reconnected through the other one (paused, once per 2 minutes); AAC never causes a switch.
- A codec that doesn't confirm steps down for that connection only: nothing is remembered as bad.
- Crash guard: a codec that hasn't played on the pair is noted while it starts playing, known-good after 20 s; a
  restart with the note left skips it on that pair until Bluetooth Codec is chosen again. Old "bad" marks are dropped.
BlueALSA's answers are faked; the probe runs synchronously."""
import subprocess, tempfile
from pathlib import Path
ui = Path(__file__).resolve().parents[1]; src = (ui / "bt.c").read_text()
def fn(sig):
    start = src.index(sig)
    while src[start:src.index("\n", start)].split("/*")[0].rstrip().endswith(";"): start = src.index(sig, start + 1)
    o = src.index("{", start); d = 1; e = o + 1
    while d: d += (src[e] == "{") - (src[e] == "}"); e += 1
    return src[start:e]
def block(a, b): return src[src.index(a):src.index(b)]
C = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#define BT_CODEC_N 7
#define BTC_SBC  1u
#define BTC_AAC  2u
#define BTC_LDAC 4u
#define BTC_APTX 8u
#define BTC_APTXHD 16u
#define BT_FULL_APTX 2
typedef struct { int x; } lv_timer_t;
static lv_timer_t the_timer; static int timers;
static lv_timer_t *lv_timer_create(void (*cb)(lv_timer_t *), int ms, void *u){ (void)cb; (void)ms; (void)u; timers++; return &the_timer; }
static void lv_timer_del(lv_timer_t *t){ (void)t; timers--; }
static uint32_t now = 1000;
static uint32_t lv_tick_get(void){ return now; }
static uint32_t lv_tick_elaps(uint32_t t){ return now - t; }
/* config */
static char keys[64][32], svals[64][40]; static int vals[64], nk;
static int ki(const char *k){ for(int i = 0; i < nk; i++) if(!strcmp(keys[i], k)) return i; return -1; }
static int kn(const char *k){ int i = ki(k); if(i < 0){ i = nk++; snprintf(keys[i], 32, "%s", k); vals[i] = 0; svals[i][0] = 0; } return i; }
static int cfg_get_int(const char *k, int d){ int i = ki(k); return i < 0 ? d : vals[i]; }
static int cfg_set_int(const char *k, int v){ vals[kn(k)] = v; return 0; }
static const char *cfg_get_str(const char *k, const char *d){ int i = ki(k); return i < 0 ? d : svals[i]; }
static int cfg_set_str(const char *k, const char *v){ snprintf(svals[kn(k)], 40, "%s", v); return 0; }
static int cfg_remove_if(int (*m)(const char *)){ int j = 0; for(int i = 0; i < nk; i++) if(!m(keys[i])){ if(i != j){ memcpy(keys[j], keys[i], 32); vals[j] = vals[i]; memcpy(svals[j], svals[i], 40); } j++; } nk = j; return 0; }
/* the Disc */
static int fw_os_ver(void){ return 257; }
static int aptx_on = 1, kind_running = 1, gate = 1, playing = 0, ipc_sent, directs, toasts;
static char route[18], sys_cmd[1600], ipc_last[64], direct_last[200], fake_info[600];
static const char *bt_alsa_aptx_bin(void){ return aptx_on ? "/usr/data/diskos-aptx/bluealsa" : NULL; }
static int bt_alsa_kind_running(void){ return kind_running; }
static const char *ui_route_mac(void){ return route; }
int ui_codec_switch_gate(const char *mac){ (void)mac; return gate; }
static int ui_is_playing(void){ return playing; }
static void ui_toast(const char *m){ (void)m; toasts++; }
void ui_route_hold_cancel(void){}
static void bt_announce(const char *mac, const char *codec){ (void)mac; (void)codec; }
static int ipc_send_cmd(const char *f){ ipc_sent++; snprintf(ipc_last, sizeof ipc_last, "%s", f); return 0; }
static int system_(const char *c){ if(strstr(c, "bluealsa-cli codec")){ directs++; snprintf(direct_last, sizeof direct_last, "%s", c); }
                                    else snprintf(sys_cmd, sizeof sys_cmd, "%s", c); return 0; }
#define system system_
static void bt_alsa_restart_steps(char *out, int cap, int born, int kind){ snprintf(out, cap, "RESTART(kind=%d,born=%d);", kind, born); }
static int bt_info_prop(const char *buf, const char *key, char *out, int cap){
    const char *p = strstr(buf, key); if(!p) return 0; p += strlen(key); while(*p == ' ') p++;
    int k = 0; while(p[k] && p[k] != '\n' && k < cap - 1){ out[k] = p[k]; k++; } out[k] = 0; return 1; }
#define pthread_create(t, a, f, arg) ((f)(arg), 0)
#define pthread_detach(t) ((void)(t))
''' + fn("static void bt_mac_cfg_key(") + "\n" + fn("static void bt_caps_key(") + "\n" + fn("static int bt_caps_get(") + "\n" + \
    fn("static int bt_caps_full(") + "\n" + fn("static void bt_caps_write(") + "\n" + fn("static void bt_caps_set(") + "\n" + \
    fn("static void bt_caps_add(") + "\n" + fn("static int bt_good_get(") + "\n" + fn("static void bt_good_add(") + "\n" + \
    fn("static unsigned bt_skip_get(") + "\n" + fn("static void bt_skip_add(") + "\n" + fn("static int bt_mac_valid(") + "\n" + \
    fn("static unsigned bt_codec_need(") + "\n" + fn("static const char *bt_codec_word(") + "\n" + fn("int bt_codec_pick(") + "\n" + \
    fn("static unsigned bt_codec_below(") + "\n" + fn("unsigned bt_codec_parse_avail(") + "\n" + fn("static unsigned bt_usable(") + "\n" + \
    fn("static unsigned bt_kind_can(") + "\n" + fn("static int bt_kind_for_v(") + "\n" + fn("static int bt_codec_frame_value(") + "\n" + \
    block("static struct { char mac[18]; int state;", "#define BT_VERIFY_TRIES") + r'''
#define BT_VERIFY_TRIES 8
#define BT_PCM_TRIES 20
static pthread_mutex_t g_cq_mu = PTHREAD_MUTEX_INITIALIZER;
struct bt_probe_arg { unsigned gen; char mac[18]; };
static void *bt_codec_probe_thread(void *arg){   /* fake probe: BlueALSA's answer is fake_info */
    struct bt_probe_arg *pa = arg; int known = 0; unsigned avail = bt_codec_parse_avail(fake_info, &known);
    char sel[16] = "", v[64];
    if(bt_info_prop(fake_info, "Selected codec:", v, sizeof v)){ int k = 0; while(v[k] && v[k] != ' ' && k < 15){ sel[k] = (char)toupper((unsigned char)v[k]); k++; } sel[k] = 0; }
    if(pa->gen == g_cq.gen){ g_cq.pcm = strstr(fake_info, "PCM=") != NULL; g_cq.known = known; g_cq.avail = avail; g_cq.state = 2;
        snprintf(g_cq.sel, sizeof g_cq.sel, "%s", sel); snprintf(g_cq.pcm_path, sizeof g_cq.pcm_path, "/org/bluealsa/hci0/dev_X/a2dpsrc/sink"); }
    free(pa); return NULL;
}
''' + fn("static int bt_codec_select_direct(") + "\n" + block("#ifndef BT_SWITCH_MARK", "static int bt_daemon_switch_allowed(") + fn("static int bt_daemon_switch_allowed(") + "\n" + fn("static void bt_daemon_switch(") + "\n" + \
    block("#define BT_GUARD_SECS", "void ui_resume_guard_arm(int reason, const char *mac);\nint bt_codec_upgrade_step") + \
    fn("int bt_codec_upgrade_step(") + r'''
static void arm(const char *mac, int choice){
    pthread_mutex_lock(&g_cq_mu);
    g_cq.gen++; g_cq.armed = 1; g_cq.applied = 0; g_cq.state = 0; g_cq.misses = 0; g_cq.verifying = 0; g_cq.pcm_path[0] = 0;
    snprintf(g_cq.mac, sizeof g_cq.mac, "%s", mac); g_cq.allow = ~0u; g_cq.choice = choice;
    pthread_mutex_unlock(&g_cq_mu); snprintf(route, sizeof route, "%s", mac); sys_cmd[0] = 0; ipc_sent = directs = 0;
}
static int run(int ticks){ for(int i = 0; i < ticks; i++){ now += 700; if(bt_codec_upgrade_step()) return i + 1; } return 0; }
static void info(const char *avail, const char *sel){
    snprintf(fake_info, sizeof fake_info, "PCM=/org/bluealsa/hci0/dev_X/a2dpsrc/sink\nAvailable codecs: %s\nSelected codec: %s\n", avail, sel); }
static int has(const char *a, const char *s){ return strstr(a, s) != NULL; }
static void guard_ticks(int n){ for(int i = 0; i < n && g_gd_timer; i++) bt_guard_tick(&the_timer); }
int main(void){
    const char *eh = "40:ED:98:1E:F2:DD", *tw = "80:C3:BA:72:32:A9", *ax = "33:44:55:66:77:88", *ac = "54:84:50:8F:9B:17";
    /* the EH11 on our aptX BlueALSA (it lists LDAC, can't run it): reconnect through FiiO's */
    kind_running = 1; arm(eh, 3); info("SBC AAC LDAC", "SBC");
    assert(run(3) && has(sys_cmd, "bluetoothctl disconnect 40:ED:98:1E:F2:DD") && has(sys_cmd, "RESTART(kind=0")
           && has(sys_cmd, "bluetoothctl connect 40:ED:98:1E:F2:DD; touch /tmp/bt_switch_done") && ipc_sent == 0 && directs == 0);
    assert(bt_caps_get(eh) == (int)(BTC_SBC | BTC_AAC | BTC_LDAC) && bt_caps_full(eh) == 2);   /* whole list learned on ours */
    arm(eh, 3); info("SBC AAC LDAC", "SBC"); now += 1000;                    /* again within 2 minutes: no loop */
    assert(!run(2) && !sys_cmd[0] && ipc_sent == 1 && has(ipc_last, "06b3001D0001"));  /* best ours can do: AAC */
    /* ...and on FiiO's it simply gets LDAC with the stock request */
    kind_running = 0; arm(eh, 3); info("SBC AAC LDAC", "SBC"); assert(!run(2) && ipc_sent == 1 && has(ipc_last, "06b3001D0003") && !sys_cmd[0]);
    info("SBC AAC LDAC", "LDAC"); assert(run(2));
    puts("PASS LDAC runs on FiiO's BlueALSA: on ours the pair is reconnected through FiiO's, once per 2 minutes");
    /* the MTW4 on FiiO's (no aptX listed) with its whole list remembered: reconnect through ours */
    bt_caps_set(tw, BTC_SBC | BTC_AAC | BTC_APTX); now += 200000;
    kind_running = 0; arm(tw, 3); info("SBC AAC", "AAC");
    assert(run(3) && has(sys_cmd, "RESTART(kind=1") && has(sys_cmd, "connect 80:C3:BA:72:32:A9"));
    /* a song is starting: wait, don't switch under it */
    now += 200000; gate = 0; arm(tw, 3); info("SBC AAC", "AAC"); assert(!run(3) && !sys_cmd[0]); gate = 1; assert(run(2) && sys_cmd[0]);
    /* AAC is the best: no switch, whichever BlueALSA runs */
    now += 200000; kind_running = 1; arm(ac, 3); info("SBC AAC", "AAC"); assert(run(2) && !sys_cmd[0] && ipc_sent == 0);
    /* aptX off: nothing ever switches (3.3.1) */
    aptx_on = 0; kind_running = 0; now += 200000; arm(tw, 3); info("SBC AAC", "SBC"); assert(!run(2) && ipc_sent == 1 && has(ipc_last, "06b3001D0001") && !sys_cmd[0]);
    aptx_on = 1;
    puts("PASS aptX runs on ours: reconnected through it; never under a starting song; AAC and aptX-off never switch");
    /* aptX HD on ours, confirmed: crash guard */
    kind_running = 1; arm(ax, 3); info("SBC AAC aptX aptX-HD", "SBC");
    assert(!run(2) && directs == 1 && has(direct_last, "aptX-HD"));
    info("SBC AAC aptX aptX-HD", "aptX-HD"); assert(run(2) && g_gd_timer && g_gd_bit == BTC_APTXHD);
    assert(!cfg_get_str("bt_try", "")[0]); playing = 1; guard_ticks(1);
    assert(!strcmp(cfg_get_str("bt_try", ""), "33:44:55:66:77:88,16"));        /* noted while it starts playing */
    playing = 0; guard_ticks(1); assert(!cfg_get_str("bt_try", "")[0]);          /* paused: note cleared */
    playing = 1; guard_ticks(25); assert(!g_gd_timer && !cfg_get_str("bt_try", "")[0] && (bt_good_get(ax) & BTC_APTXHD));
    puts("PASS crash guard: noted only while playing, cleared on pause, known-good after 20 s of music");
    /* a restart with the note left: skipped on that pair until the codec is chosen again */
    bt_caps_set(eh, BTC_SBC | BTC_AAC | BTC_LDAC);
    kind_running = 0; arm(eh, 3); info("SBC AAC LDAC", "LDAC"); assert(run(2) && g_gd_bit == BTC_LDAC);
    playing = 1; guard_ticks(3); assert(!strcmp(cfg_get_str("bt_try", ""), "40:ED:98:1E:F2:DD,4"));
    cfg_set_int("btbad_80C3BA7232A9", 8); cfg_set_int("btbad_40ED981EF2DD", 0);
    g_gd_timer = NULL; g_gd_bit = 0; g_gd_armed = 0;                          /* ...the Disc restarts */
    bt_guard_boot();
    assert(bt_skip_get(eh) == BTC_LDAC && !cfg_get_str("bt_try", "")[0] && has(g_gd_msg, "LDAC restarted the Disc"));
    assert(ki("btbad_80C3BA7232A9") < 0 && ki("btbad_40ED981EF2DD") < 0);       /* old "bad" marks dropped */
    arm(eh, 3); info("SBC AAC LDAC", "SBC"); assert(!run(2) && has(ipc_last, "06b3001D0001"));   /* LDAC skipped: AAC */
    bt_codec_setting_changed(); assert(bt_skip_get(eh) == 0);
    arm(eh, 3); info("SBC AAC LDAC", "SBC"); assert(!run(2) && has(ipc_last, "06b3001D0003"));   /* chosen again: LDAC */
    bt_guard_boot(); assert(bt_skip_get(eh) == 0);                            /* no note: nothing skipped */
    puts("PASS a restart while a codec started playing skips it on that pair until Bluetooth Codec is chosen again");
    /* not confirmed: next one down for this connection only, nothing remembered */
    playing = 0; g_gd_timer = NULL; g_gd_bit = 0;
    kind_running = 1; now += 200000; arm(tw, 3); info("SBC AAC aptX aptX-HD", "SBC");
    assert(!run(2) && directs == 1 && has(direct_last, "aptX-HD"));
    assert(!run(9) && directs == 2 && has(direct_last, " aptX "));           /* HD never confirmed: aptX, same connection */
    info("SBC AAC aptX aptX-HD", "aptX"); assert(run(2));
    assert(bt_skip_get(tw) == 0 && ki("btbad_80C3BA7232A9") < 0);
    arm(tw, 3); info("SBC AAC aptX aptX-HD", "SBC"); assert(!run(2) && has(direct_last, "aptX-HD"));   /* next connection: HD again */
    puts("PASS a codec that doesn't confirm steps down for that connection only; the next one tries the best again");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    p = Path(d) / "t.c"; p.write_text(C)
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wno-unused-function", "-o", str(Path(d) / "t"), str(p), "-lpthread"], check=True)
    r = subprocess.run([str(Path(d) / "t")], capture_output=True, text=True)
    print(r.stdout.strip(), r.stderr.strip()); r.check_returncode()
