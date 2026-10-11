/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "screens.h"
#include "config.h"
LV_FONT_DECLARE(font_np_inter_30)
LV_FONT_DECLARE(font_np_inter_22)
LV_FONT_DECLARE(font_np_nunito_30)
LV_FONT_DECLARE(font_np_nunito_22)
/* Rasterized once at build time; international/CJK fallbacks remain available. */
const lv_font_t *ui_np_font(int size){
    int choice=cfg_get_int("np_font",0);
    if(choice<1 || choice>2)return ui_font_cjk(size);
    static lv_font_t fonts[4];static int ready;
    if(!ready){
        const lv_font_t *base[]={&font_np_inter_30,&font_np_inter_22,&font_np_nunito_30,&font_np_nunito_22};
        for(int i=0;i<4;i++){fonts[i]=*base[i];fonts[i].fallback=ui_font_cjk(i%2?20:28);}
        ready=1;
    }
    return &fonts[(choice-1)*2+(size>=24?0:1)];
}

/* Reveal a highlighted row briefly, then stop repainting and return to ellipsis. */
static void reveal_done(lv_anim_t *a){lv_label_set_long_mode((lv_obj_t *)a->var,LV_LABEL_LONG_DOT);}
void ui_reveal_title(lv_obj_t *label){
    static lv_anim_t hint;static int ready;
    if(!ready){lv_anim_init(&hint);lv_anim_set_repeat_count(&hint,2);
        lv_anim_set_delay(&hint,1000);lv_anim_set_repeat_delay(&hint,1200);
        lv_anim_set_completed_cb(&hint,reveal_done);ready=1;}
    lv_obj_set_style_anim(label,&hint,0);lv_label_set_long_mode(label,LV_LABEL_LONG_SCROLL_CIRCULAR);
}
