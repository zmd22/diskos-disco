/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Timed lyric glyph clipping; shared by the list and Immersive renderers. */
#include "lyrics_fx.h"
#include "lvgl/src/misc/lv_area_private.h"
#include "lvgl/src/misc/lv_text_private.h"
#include <string.h>

static void fx_event(lv_event_t *e);

void lyrics_fx_unbind(lyrics_fx_t *fx)
{
    if(!fx) return;
    if(fx->label) lv_obj_remove_event_cb_with_user_data(fx->label, fx_event, fx);
    memset(fx, 0, sizeof *fx);
}

static int add_fragment(lyrics_fx_t *fx, int word, int x, int y, int w, int h)
{
    if(w <= 0) return 1;
    if(fx->fragment_count){
        lyrics_fx_fragment_t *last = &fx->fragments[fx->fragment_count - 1];
        if(last->word == word && last->area.y1 == y){
            /* Unsupported visual reordering keeps the normal whole-line view. */
            if(x < last->area.x1) return 0;
            if(x + w - 1 > last->area.x2) last->area.x2 = x + w - 1;
            return 1;
        }
    }
    if(fx->fragment_count == LYRICS_FX_FRAGMENTS) return 0;
    lyrics_fx_fragment_t *f = &fx->fragments[fx->fragment_count++];
    f->word = word;
    f->area = (lv_area_t){x, y, x + w - 1, y + h - 1};
    return 1;
}

int lyrics_fx_bind(lyrics_fx_t *fx, lv_obj_t *label, const lrc_word_t *words,
                   int count, lv_color_t color)
{
    if(!fx) return 0;
    lyrics_fx_unbind(fx);
    if(!label || !words || count <= 0 || count > LYRICS_FX_FRAGMENTS) return 0;
    const char *text = lv_label_get_text(label);
    if(!text || !text[0]) return 0;
    size_t length = strlen(text);
    lv_obj_update_layout(label);
    lv_area_t box, content;
    lv_obj_get_coords(label, &box);
    lv_obj_get_content_coords(label, &content);
    int px = content.x1 - box.x1, py = content.y1 - box.y1;
    const lv_font_t *font = lv_obj_get_style_text_font(label, 0);
    int height = lv_font_get_line_height(font);
    int spacing = lv_obj_get_style_text_letter_space(label, 0);
    fx->words = words;
    fx->word_count = count;
    fx->color = color;
    fx->position = -1;
    fx->draw_enabled = 1;
    int previous_end = 0;
    for(int i = 0; i < count; i++){
        int first = words[i].start_byte, end = words[i].end_byte;
        if(first < previous_end || first < 0 || end <= first || (size_t)end > length ||
           ((unsigned char)text[first] & 0xc0) == 0x80 ||
           ((unsigned char)text[end] & 0xc0) == 0x80 ||
           words[i].start_ms < 0 || (i && words[i].start_ms < words[i-1].start_ms)) goto fallback;
        previous_end = end;
        uint32_t byte = (uint32_t)first;
        uint32_t letter = lv_text_encoded_get_char_id(text, byte);
        while(byte < (uint32_t)end){
            uint32_t next = byte;
            uint32_t cp = lv_text_encoded_next(text, &next);
            if(next <= byte || next > (uint32_t)end) goto fallback;
            uint32_t look = next;
            uint32_t following = lv_text_encoded_next(text, &look);
            lv_point_t pos;
            lv_label_get_letter_pos(label, letter, &pos);
            int width = lv_font_get_glyph_width(font, cp, following);
            if(cp != '\n' && cp != '\r' &&
               !add_fragment(fx, i, pos.x + px, pos.y + py, width + spacing, height)) goto fallback;
            byte = next;
            letter++;
        }
    }
    if(!fx->fragment_count) goto fallback;
    int rows = 0;
    for(int i = 0; i < fx->fragment_count; i++){
        int seen = 0;
        for(int j = 0; j < i; j++)
            if(fx->fragments[j].area.y1 == fx->fragments[i].area.y1) { seen = 1; break; }
        if(!seen && ++rows > LYRICS_FX_CLIPS) goto fallback;
    }
    fx->label = label;
    lv_obj_add_event_cb(label, fx_event, LV_EVENT_DRAW_MAIN_END, fx);
    lv_obj_add_event_cb(label, fx_event, LV_EVENT_DELETE, fx);
    return 1;
fallback:
    memset(fx, 0, sizeof *fx);
    return 0;
}

static int merge_clip(lv_area_t *clips, int *count, const lv_area_t *a)
{
    for(int i = 0; i < *count; i++){
        if(clips[i].y1 == a->y1 && clips[i].y2 == a->y2){
            if(a->x1 < clips[i].x1) clips[i].x1 = a->x1;
            if(a->x2 > clips[i].x2) clips[i].x2 = a->x2;
            return 1;
        }
    }
    if(*count >= LYRICS_FX_CLIPS) return 0;
    clips[(*count)++] = *a;
    return 1;
}

int lyrics_fx_update(lyrics_fx_t *fx, long position)
{
    if(!fx || !fx->label) return 0;
    fx->position = position;
    lv_area_t clips[LYRICS_FX_CLIPS];
    int n = 0;
    for(int word = 0; word < fx->word_count; word++){
        const lrc_word_t *w = &fx->words[word];
        if(position < w->start_ms) continue;
        int total = 0;
        for(int j = 0; j < fx->fragment_count; j++)
            if(fx->fragments[j].word == word) total += lv_area_get_width(&fx->fragments[j].area);
        int lit = total;
        if(w->end_ms > w->start_ms && position < w->end_ms)
            lit = (int)((int64_t)total * (position - w->start_ms) / (w->end_ms - w->start_ms));
        for(int j = 0; j < fx->fragment_count && lit > 0; j++){
            if(fx->fragments[j].word != word) continue;
            lv_area_t area = fx->fragments[j].area;
            int width = lv_area_get_width(&area);
            if(lit < width) area.x2 = area.x1 + lit - 1;
            if(!merge_clip(clips, &n, &area)) break;
            lit -= width;
        }
    }
    if(n == fx->clip_count && (!n || !memcmp(clips, fx->clips, (size_t)n * sizeof clips[0]))) return 0;
    fx->clip_count = n;
    if(n) memcpy(fx->clips, clips, (size_t)n * sizeof clips[0]);
    if(fx->draw_enabled) lv_obj_invalidate(fx->label);
    return 1;
}

int lyrics_fx_get_clips(const lyrics_fx_t *fx, lv_area_t *out, int capacity)
{
    if(!fx || !out || capacity <= 0) return 0;
    int n = fx->clip_count < capacity ? fx->clip_count : capacity;
    if(n) memcpy(out, fx->clips, (size_t)n * sizeof *out);
    return n;
}

void lyrics_fx_set_draw(lyrics_fx_t *fx, int enabled)
{
    if(fx) fx->draw_enabled = !!enabled;
}

static void fx_event(lv_event_t *e)
{
    lyrics_fx_t *fx = lv_event_get_user_data(e);
    if(lv_event_get_code(e) == LV_EVENT_DELETE){ memset(fx, 0, sizeof *fx); return; }
    if(!fx->draw_enabled || !fx->clip_count || !fx->label) return;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t box, content;
    lv_obj_get_coords(fx->label, &box);
    lv_obj_get_content_coords(fx->label, &content);
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    lv_obj_init_draw_label_dsc(fx->label, LV_PART_MAIN, &dsc);
    dsc.text = lv_label_get_text(fx->label);
    dsc.color = fx->color;
    dsc.flag = LV_TEXT_FLAG_NONE;
    lv_area_t original = layer->_clip_area;
    for(int i = 0; i < fx->clip_count; i++){
        lv_area_t area = fx->clips[i], clipped;
        lv_area_move(&area, box.x1, box.y1);
        if(!lv_area_intersect(&clipped, &area, &original)) continue;
        if(!lv_area_intersect(&layer->_clip_area, &clipped, &content)) continue;
        lv_draw_label(layer, &dsc, &content);
    }
    layer->_clip_area = original;
}
