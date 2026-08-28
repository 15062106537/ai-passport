// main/home.c —— 首页:身份卡风格(天空+草地+小机器人),两个大入口 Apps / Voice AI。
// UP/DOWN 切换入口, OK 由 main.c 统一进入。
#include "home.h"
#include "ui_pixel.h"
#include "lvgl.h"

#define COUNT 2

static const char *NAMES[COUNT] = { "Settings", "Voice AI" };
static const uint32_t COLORS[COUNT] = { 0x1689E8, 0xFFD928 };   // Apps 蓝, Voice 黄

static lv_obj_t *s_scr;
static lv_obj_t *s_card[COUNT];
static lv_obj_t *s_label[COUNT];
static int s_sel;

static void refresh(void) {
    for (int i = 0; i < COUNT; i++) {
        bool sel = (i == s_sel);
        lv_obj_set_style_bg_color(s_card[i], lv_color_hex(sel ? COLORS[i] : UI_PAPER), 0);
        lv_obj_set_style_border_color(s_card[i], lv_color_hex(sel ? 0xFFFFFF : UI_INK), 0);
        lv_obj_set_style_border_width(s_card[i], sel ? 4 : 2, 0);
        lv_obj_set_style_text_color(s_label[i], lv_color_hex(sel ? 0xFFFFFF : UI_INK), 0);
    }
}

void home_init(void) {
    s_sel = 0;
    s_scr = ui_pixel_screen_create("FoloToy");

    for (int i = 0; i < COUNT; i++) {
        int y = 62 + i * 90;
        s_card[i] = ui_pixel_panel_create(s_scr, 32, y, 176, 74, UI_PAPER);
        s_label[i] = lv_label_create(s_card[i]);
        lv_obj_set_style_text_font(s_label[i], &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(s_label[i], lv_color_hex(UI_INK), 0);
        lv_label_set_text(s_label[i], NAMES[i]);
        lv_obj_center(s_label[i]);
    }

    ui_pixel_mascot_create(s_scr, 101, 240);
    refresh();
}

void home_show(void) {
    refresh();
    lv_screen_load(s_scr);
}

bool home_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev != BSP_BTN_CLICK) return false;
    if (btn == BSP_BTN_UP)   { s_sel = (s_sel + COUNT - 1) % COUNT; refresh(); return true; }
    if (btn == BSP_BTN_DOWN) { s_sel = (s_sel + 1) % COUNT;          refresh(); return true; }
    return false;
}

home_entry_t home_get_selected(void) {
    return (home_entry_t)s_sel;
}
