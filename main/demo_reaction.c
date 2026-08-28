// main/demo_reaction.c —— 反应力测试。
// 屏幕先"WAIT..."(红), 随机 1~4 秒后变"GO!"(绿), 第一时间按 OK, 测毫秒反应。
#include "demo.h"
#include "bsp_display.h"
#include "ui_pixel.h"
#include "lvgl.h"
#include "esp_timer.h"
#include "esp_random.h"
#include <stdio.h>

static lv_obj_t *s_scr, *s_panel, *s_label, *s_mascot;
static lv_timer_t *s_timer;
static int64_t s_go_at;      // 变绿的微秒时间戳
static bool s_waiting;       // true=等待变绿, false=已变绿等按
static int s_best;           // 最快记录(毫秒)

static void wait_start(void) {
    s_waiting = true;
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(UI_RED), 0);
    lv_label_set_text(s_label, "WAIT...");
    // 1~4 秒后变绿
    s_go_at = esp_timer_get_time() + (int64_t)(esp_random() % 3000 + 1000) * 1000;
}

static void tick(lv_timer_t *t) {
    (void)t;
    if (s_waiting && esp_timer_get_time() >= s_go_at) {
        s_waiting = false;
        s_go_at = esp_timer_get_time();
        lv_obj_set_style_bg_color(s_panel, lv_color_hex(0x1FA32B), 0);
        lv_label_set_text(s_label, "GO!");
    }
}

static void press_ok(void) {
    if (s_waiting) {                 // 抢跑
        lv_label_set_text(s_label, "TOO SOON!\nOK: RETRY");
        s_waiting = false;
        return;
    }
    int ms = (int)((esp_timer_get_time() - s_go_at) / 1000);
    if (s_best == 0 || ms < s_best) s_best = ms;
    lv_label_set_text_fmt(s_label, "%d ms\nBEST %d\nOK: AGAIN", ms, s_best);
}

void demo_reaction_enter(void) {
    s_best = 0;
    s_scr = ui_pixel_screen_create("REACTION");
    s_panel = ui_pixel_panel_create(s_scr, 18, 58, 204, 150, UI_RED);
    s_label = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_label, "OK: START");
    lv_obj_center(s_label);
    s_mascot = ui_pixel_mascot_create(s_scr, 101, 238);
    s_timer = lv_timer_create(tick, 10, NULL);
    s_waiting = false;
    lv_screen_load(s_scr);
}

void demo_reaction_exit(void) {
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; s_panel = s_label = s_mascot = NULL; }
}

void demo_reaction_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev != BSP_BTN_CLICK || btn != BSP_BTN_OK) return;
    if (!s_waiting) { wait_start(); }
    else { press_ok(); }
}
