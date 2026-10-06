/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "theme_kit.h"
#include "theme.h"
#include "config.h"
#include "command_spawn.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <crypt.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PWFILE "/usr/data/sshd/current_pw"   /* persisted plaintext (0600) so the screen survives an mq_ui restart */

/* Debug Mode: enable/disable diskOS remote debug access from the UI.
 *   - SSH (dropbear over WiFi) with a RANDOM per-enable password shown ONLY here (never the stock
 *     password, never anything shipped in the repo). We hash it with crypt() and hand the hash to
 *     /usr/project/diskos-debug.sh, which overlays a private shadow over /etc/shadow.
 *   - a USB serial root shell on /dev/ttyGS0 (local-USB only).
 * State is intentionally NOT persisted across reboots: debug access comes back OFF after a restart. */

#define DBG "/usr/project/diskos-debug.sh"

static lv_obj_t *g_ssh, *g_pw, *g_serial, *g_warn, *g_btnlbl, *g_btn;
static lv_timer_t *g_debug_timer;
static int g_busy;
static _Atomic int g_done;
static struct { int enable,on; char pw[16],error[96]; } g_job;
static int  g_on = 0;
static char g_pwtext[16] = "";


/* Returns 0 on success, -1 if secure entropy is unavailable. A network-root password must NEVER fall
 * back to a fixed/weak value, so the caller refuses to enable SSH on failure. */
static int rnd_alnum(char *out, int n){
    static const char cs[] = "abcdefghijkmnpqrstuvwxyz23456789";   /* 32 chars, no ambiguous 0/o/1/l */
    unsigned char b[32];
    int fd = open("/dev/urandom", O_RDONLY);
    if(fd < 0) return -1;
    ssize_t got = read(fd, b, sizeof b);
    close(fd);
    if(got != (ssize_t)sizeof b) return -1;
    if(n > (int)sizeof b) n = sizeof b;
    for(int i = 0; i < n; i++) out[i] = cs[b[i] & 31];
    out[n] = 0;
    return 0;
}

static int wlan_ip(char *out, int n){
    int s = socket(AF_INET, SOCK_DGRAM, 0); if(s < 0) return 0;
    struct ifreq ifr; memset(&ifr, 0, sizeof ifr);
    strncpy(ifr.ifr_name, "wlan0", IFNAMSIZ-1);
    int ok = (ioctl(s, SIOCGIFADDR, &ifr) == 0);
    if(ok){ struct sockaddr_in *a = (struct sockaddr_in *)&ifr.ifr_addr;
            snprintf(out, n, "%s", inet_ntoa(a->sin_addr)); }
    close(s);
    return ok;
}

/* Is dropbear actually running? Scan /proc/<pid>/comm - so the screen reflects REAL state (e.g. after
 * an mq_ui restart while SSH kept running), not a stale in-memory flag. */
static int ssh_running(void){
    DIR *d = opendir("/proc"); if(!d) return 0;
    struct dirent *e; int found = 0;
    while((e = readdir(d))){
        if(e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        char p[64]; snprintf(p, sizeof p, "/proc/%s/comm", e->d_name);
        FILE *f = fopen(p, "r"); if(!f) continue;
        char c[32] = ""; if(fgets(c, sizeof c, f) && strncmp(c, "dropbearmulti", 13) == 0) found = 1;
        fclose(f);
        if(found) break;
    }
    closedir(d);
    return found;
}

/* Is our private shadow overlay currently bind-mounted over /etc/shadow? SSH is only truly OFF when
 * BOTH dropbear is gone AND this overlay is dropped - otherwise a blocked/killed ssh-off could leave
 * the overlay mounted, and reporting OFF would be a lie (and re-enable logic could misbehave). */
static int overlay_mounted(void){
    /* Fail CLOSED: if we cannot read /proc/mounts we must not claim the overlay is gone (that could
     * let the UI report OFF while stock shadow is still overlaid). Unknown -> treat as still mounted. */
    FILE *f = fopen("/proc/mounts", "r"); if(!f) return 1;
    char line[512]; int m = 0;
    while(fgets(line, sizeof line, f)){ if(strstr(line, " /etc/shadow ")){ m = 1; break; } }
    fclose(f);
    return m;
}

static void pw_load(void){
    g_pwtext[0] = 0;
    FILE *f = fopen(PWFILE, "r");
    if(f){ if(fgets(g_pwtext, sizeof g_pwtext, f)){ char *nl = strchr(g_pwtext, '\n'); if(nl) *nl = 0; } fclose(f); }
}
static int pw_store(const char *pw){
    mkdir("/usr/data/sshd",0700);
    int fd=open(PWFILE,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0)return 0;
    int ok=fchmod(fd,0600)==0 && dprintf(fd,"%s\n",pw)>0;
    if(close(fd))ok=0;return ok;

}

static void refresh(void){
    char b[96];
    if(g_on){
        char ip[40] = "";
        if(wlan_ip(ip, sizeof ip)) snprintf(b, sizeof b, "ssh root@%s", ip);
        else                       snprintf(b, sizeof b, "SSH: connect to WiFi first");
        lv_label_set_text(g_ssh, b);
        snprintf(b, sizeof b, "Password:  %s", g_pwtext);       lv_label_set_text(g_pw, b);
        /* Only claim serial when the USB gadget actually exists (dev builds); public builds have no
         * ttyGS0, so don't advertise a serial shell that can't be reached. */
        if(access("/dev/ttyGS0", F_OK) == 0) lv_label_set_text(g_serial, "Serial: USB-C (/dev/ttyACM0)");
        else                                 lv_label_set_text(g_serial, "Serial: not available (SSH only)");
        lv_label_set_text(g_warn, "This gives ROOT access to the device. Turn it OFF when finished.");
        lv_label_set_text(g_btnlbl, "Disable Debug");
        lv_obj_remove_flag(g_pw, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(g_serial, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(g_ssh, "Debug access is OFF");
        lv_obj_add_flag(g_pw, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_serial, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(g_warn, "Enables SSH over Wi-Fi with a fresh random password.");
        lv_label_set_text(g_btnlbl, "Enable Debug");
    }
}

/* Workers never touch LVGL or UI-owned state. The release/acquire flag publishes
 * one completed result; a second operation cannot overlap the current one. */
static void *debug_worker(void *unused){
    (void)unused;
    if(g_job.enable){
        char salt[16],setting[24];
        if(rnd_alnum(g_job.pw,10) || rnd_alnum(salt,8)){
            snprintf(g_job.error,sizeof g_job.error,"No secure random available - NOT enabled.");goto done;
        }
        snprintf(setting,sizeof setting,"$6$%s$",salt);
        struct crypt_data cd;memset(&cd,0,sizeof cd);
        char *h=crypt_r(g_job.pw,setting,&cd);
        if(!h || h[0]!='$'){
            snprintf(g_job.error,sizeof g_job.error,"Could not hash password - NOT enabled.");goto done;
        }
        /* Persist before starting the daemon: a UI restart must not lose the
         * newly installed credential while setup continues in the helper. */
        if(!pw_store(g_job.pw)){
            snprintf(g_job.error,sizeof g_job.error,"Could not save debug password - NOT enabled.");goto done;
        }
        char *a1[]={DBG,"ssh-on",h,NULL};command_wait(a1,12000);
        char *a2[]={DBG,"serial-on",NULL};command_wait(a2,5000);
        if(ssh_running()){g_job.on=1;}
        else{
            g_job.pw[0]=0;g_job.on=overlay_mounted();
            snprintf(g_job.error,sizeof g_job.error,g_job.on?
                "SSH setup incomplete - disable and retry.":"Could not start SSH. See the debug log.");
        }
    }else{
        char *a1[]={DBG,"ssh-off",NULL};command_wait(a1,12000);
        char *a2[]={DBG,"serial-off",NULL};command_wait(a2,5000);
        g_job.on=ssh_running() || overlay_mounted();
        if(!g_job.on){unlink(PWFILE);g_job.pw[0]=0;}
        else snprintf(g_job.error,sizeof g_job.error,"SSH did not fully stop. Try Disable again.");
    }
done:
    atomic_store_explicit(&g_done,1,memory_order_release);return NULL;
}
static void debug_poll(lv_timer_t *t){
    if(!atomic_load_explicit(&g_done,memory_order_acquire))return;
    g_busy=0;g_on=g_job.on;
    snprintf(g_pwtext,sizeof g_pwtext,"%s",g_job.pw);
    lv_obj_remove_state(g_btn,LV_STATE_DISABLED);refresh();
    if(g_job.error[0])lv_label_set_text(g_warn,g_job.error);
    memset(g_job.pw,0,sizeof g_job.pw);
    lv_timer_pause(t);
}
static void btn_cb(lv_event_t *e){
    (void)e;if(g_busy || !g_debug_timer)return;
    memset(&g_job,0,sizeof g_job);g_job.enable=!g_on;g_job.on=g_on;
    snprintf(g_job.pw,sizeof g_job.pw,"%s",g_pwtext);
    atomic_store(&g_done,0);g_busy=1;
    pthread_t th;
    if(pthread_create(&th,NULL,debug_worker,NULL)){
        g_busy=0;lv_label_set_text(g_warn,"Could not start debug setup. Try again.");return;
    }
    pthread_detach(th);lv_obj_add_state(g_btn,LV_STATE_DISABLED);
    lv_label_set_text(g_warn,g_job.enable?"Enabling debug access...":"Disabling debug access...");
    lv_timer_resume(g_debug_timer);
}

/* On open, reflect the REAL state (dropbear running?) + reload the persisted password, so a restart
 * or a re-open never offers "Enable" while SSH is live (which would rotate the password needlessly). */
void debug_open(void){
    /* ON if dropbear is up OR the overlay is still mounted (a partial/stuck state must not read as OFF,
     * so the user can Disable again to fully tear it down). */
    if(!g_busy){
        g_on = ssh_running() || overlay_mounted();
        if(g_on) pw_load(); else g_pwtext[0] = 0;
        refresh();
    }
    screen_show(SCR_DEBUG);
}

void debug_create(lv_obj_t *root){
    ui_header(root, "Debug Mode");   /* shared header: gives Debug Mode the standard back chevron so it
                                      * isn't a dead-end for users who don't know the edge-swipe gesture */

    /* The ssh command + password are what you read off the screen and type, so they get the HIGHEST
     * contrast (bright white); the password also gets the accent colour + bigger font to stand out. */
    g_ssh = lv_label_create(root);
    lv_obj_set_width(g_ssh, 320); lv_obj_set_style_text_align(g_ssh, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_ssh, TC(TEXT_PRIMARY), 0);
    lv_obj_align(g_ssh, LV_ALIGN_TOP_MID, 0, 94);

    g_pw = lv_label_create(root);
    lv_obj_set_width(g_pw, 320); lv_obj_set_style_text_align(g_pw, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_pw, TF(UI_24), 0);
    lv_obj_set_style_text_color(g_pw, ui_current_accent(), 0);
    lv_obj_align(g_pw, LV_ALIGN_TOP_MID, 0, 122);

    g_serial = lv_label_create(root);
    lv_obj_set_width(g_serial, 300); lv_obj_set_style_text_align(g_serial, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_serial, TC(TEXT_MUTED), 0);
    lv_obj_align(g_serial, LV_ALIGN_TOP_MID, 0, 158);

    g_warn = lv_label_create(root);
    lv_obj_set_width(g_warn, 280); lv_label_set_long_mode(g_warn, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(g_warn, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_warn, TC(STATUS_WARNING), 0);
    lv_obj_align(g_warn, LV_ALIGN_TOP_MID, 0, 190);

    lv_obj_t *btn = lv_button_create(root);g_btn=btn;
    lv_obj_set_size(btn, 190, 50);
    lv_obj_set_style_shadow_width(btn, 0, 0);   /* no LVGL default-theme drop shadow: every other button is flat */
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -46);
    ui_on(btn, btn_cb, LV_EVENT_CLICKED, NULL, "debug_ui.btn", UI_CORE);
    /* the accent action, like Date & Time's Set: LVGL's default button blue and an inherited text colour read poorly
     * (green on green, grey on orange) under the themes */
    lv_color_t acc = ui_current_accent();
    lv_obj_set_style_bg_color(btn, acc, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    g_btnlbl = lv_label_create(btn); lv_obj_center(g_btnlbl);
    lv_obj_set_style_text_color(g_btnlbl, theme_on_color(acc), 0);   /* the live accent can be any album colour */

    refresh();
    if(!g_debug_timer){g_debug_timer=lv_timer_create(debug_poll,100,NULL);if(g_debug_timer)lv_timer_pause(g_debug_timer);else lv_obj_add_state(g_btn,LV_STATE_DISABLED);}
}
