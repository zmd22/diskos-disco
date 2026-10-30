/* SPDX-License-Identifier: OFL-1.1 (Inter) */
/* The colon for font_inter_bold_44 (Braun 2.0's big times, e.g. 1:32): Inter Bold 44 px, 4 bpp, one glyph.
 * The digits-only font has no ':' and fell back to a 20 px font; br_font() chains this one first.
 * Generated from Inter Bold with the same metrics (advance 235/16 px, baseline 0, line height 32). */
#include "lvgl/lvgl.h"
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    0x00, 0x8e, 0xfd, 0x50, 0x00, 0xbf, 0xff, 0xff, 0x60, 0x3f, 0xff, 0xff,
    0xfe, 0x05, 0xff, 0xff, 0xff, 0xf0, 0x3f, 0xff, 0xff, 0xfe, 0x00, 0xbf,
    0xff, 0xff, 0x60, 0x00, 0x7d, 0xfc, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x8e, 0xfd, 0x50, 0x00, 0xbf, 0xff, 0xff, 0x60, 0x3f, 0xff, 0xff,
    0xfe, 0x05, 0xff, 0xff, 0xff, 0xf0, 0x3f, 0xff, 0xff, 0xfe, 0x00, 0xbf,
    0xff, 0xff, 0x60, 0x00, 0x7d, 0xfc, 0x40, 0x00
};
static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 0, .adv_w = 235, .box_w = 9, .box_h = 23, .ofs_x = 3, .ofs_y = 0}
};
static const lv_font_fmt_txt_cmap_t cmaps[] = {
    {.range_start = 58, .range_length = 1, .glyph_id_start = 1, .unicode_list = NULL, .glyph_id_ofs_list = NULL,
      .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY}
};
static const lv_font_fmt_txt_dsc_t font_dsc = {
    .glyph_bitmap = glyph_bitmap, .glyph_dsc = glyph_dsc, .cmaps = cmaps, .kern_dsc = NULL, .kern_scale = 0,
    .cmap_num = 1, .bpp = 4, .kern_classes = 0, .bitmap_format = 0,
};
const lv_font_t font_inter_bold_44_colon = {
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt, .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = 32, .base_line = 0, .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = -7, .underline_thickness = 4, .dsc = &font_dsc, .fallback = NULL, .user_data = NULL,
};
