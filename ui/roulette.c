/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "screens.h"
#include "theme.h"
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>

extern const unsigned char album_roulette_blob_start[], album_roulette_blob_end[];
static int app_write(int fd, const unsigned char *p, size_t n){
    while(n){ ssize_t w = write(fd, p, n > 32768 ? 32768 : n);
        if(w < 0 && errno == EINTR) continue;
        if(w <= 0) return 0;
        p += w; n -= (size_t)w;
    }
    return 1;
}
static int app_dir(const char *path){
    if(mkdir(path, 0755) && errno != EEXIST) return 0;
    struct stat st; return !lstat(path, &st) && S_ISDIR(st.st_mode);
}
int roulette_install_bundled(void){
    const char *app = "/usr/data/apps/album-roulette/app";
    if(!app_dir("/usr/data/apps") || !app_dir("/usr/data/apps/album-roulette")) return 0;
    struct stat st;
    if(lstat(app, &st) == 0){
        /* An existing user installation wins; never downgrade or overwrite it. */
        if(!S_ISREG(st.st_mode) || access(app, X_OK)) return 0;
    } else {
        if(errno != ENOENT) return 0;
        char tmp[] = "/usr/data/apps/album-roulette/.app-bundled-XXXXXX";
        int fd = mkstemp(tmp); if(fd < 0) return 0;
        size_t size = (uintptr_t)album_roulette_blob_end - (uintptr_t)album_roulette_blob_start;
        int ok = app_write(fd, album_roulette_blob_start, size) && !fchmod(fd, 0755) && !fsync(fd);
        if(close(fd)) ok = 0;
        if(ok && link(tmp, app)) ok = errno == EEXIST && !access(app, X_OK);
        unlink(tmp);
        if(!ok) return 0;
    }
    /* Only install missing metadata; preserve custom names and settings. */
    const char *conf = "/usr/data/apps/album-roulette/app.conf";
    int fd = open(conf, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if(fd >= 0){
        static const unsigned char text[] = "name=Album Roulette\nexec=/usr/data/apps/album-roulette/app\n";
        int ok = app_write(fd, text, sizeof text - 1) && !fsync(fd);
        if(close(fd)) ok = 0;
        if(!ok){ unlink(conf); return 0; }
    } else if(errno != EEXIST) return 0;
    return 1;
}

static void launch_cb(lv_event_t *e){
    (void)e;
    const char *app = "/usr/data/apps/album-roulette/app";
    if(!roulette_install_bundled()){ ui_toast("Cannot install Album Roulette. Check free storage."); return; }
    app_launch_direct(app);
}
static lv_obj_t *text(lv_obj_t *root, const char *s, int y, int width, const lv_font_t *font, lv_color_t color){
    lv_obj_t *l = lv_label_create(root);
    lv_label_set_text(l, s); lv_obj_set_width(l, width);
    lv_obj_set_style_text_font(l, font, 0); lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    return l;
}
void roulette_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    /* Static vector vinyl: no bitmap, rotation timer or idle redraws. */
    lv_obj_t *disc = lv_obj_create(root); lv_obj_remove_style_all(disc);
    lv_obj_set_size(disc, 128, 128); lv_obj_align(disc, LV_ALIGN_TOP_MID, 0, 38);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc, TC(SURFACE), 0); lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(disc, 2, 0); lv_obj_set_style_border_color(disc, TC(TEXT_SECONDARY), 0);
    lv_obj_remove_flag(disc, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    for(int n = 0; n < 4; n++){
        lv_obj_t *ring = lv_obj_create(disc); lv_obj_remove_style_all(ring);
        lv_obj_set_size(ring, 110 - n * 14, 110 - n * 14); lv_obj_center(ring);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(ring, 1, 0); lv_obj_set_style_border_color(ring, TC(BORDER_STRONG), 0);
        lv_obj_remove_flag(ring, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_t *hub = lv_obj_create(disc); lv_obj_remove_style_all(hub);
    lv_obj_set_size(hub, 42, 42); lv_obj_center(hub); lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(hub, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, 0);
    lv_obj_remove_flag(hub, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *hole = lv_obj_create(hub); lv_obj_remove_style_all(hole);
    lv_obj_set_size(hole, 8, 8); lv_obj_center(hole); lv_obj_set_style_radius(hole, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(hole, TC(CANVAS), 0); lv_obj_set_style_bg_opa(hole, LV_OPA_COVER, 0);
    lv_obj_remove_flag(hole, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    text(root, "Album Roulette", 181, 272, TF(UI_22), TC(TEXT_PRIMARY));
    text(root, "Let chance choose.", 215, 260, TF(UI_16), TC(ACCENT_PRIMARY));
    text(root, "Spin your collection and\nrediscover a favourite.", 243, 258, TF(UI_16), TC(TEXT_SECONDARY));
    lv_obj_t *button = lv_button_create(root); lv_obj_remove_style_all(button);
    lv_obj_set_size(button, 174, 42); lv_obj_align(button, LV_ALIGN_TOP_MID, 0, 296);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, TC(PRIMARY_PRESSED), LV_STATE_PRESSED);
    lv_obj_add_event_cb(button, launch_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(button); lv_label_set_text(l, "I feel lucky!");
    lv_obj_set_style_text_font(l, TF(UI_18), 0); lv_obj_set_style_text_color(l, TC(ON_ACCENT), 0); lv_obj_center(l);
}
