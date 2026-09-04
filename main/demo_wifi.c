// main/demo_wifi.c —— Wi-Fi 状态页:只展示常驻连接(wifi_keep)的状态与 IP。
// 连接归 wifi_keep 管:进页幂等启动,退出界面不断开、下次进来仍是连着的。
#include "demo.h"
#include "wifi_keep.h"
#include "ui_pixel.h"

#include "lvgl.h"

static lv_obj_t *s_scr, *s_status, *s_ip_label, *s_mascot;
static lv_timer_t *s_timer;
static int s_shown = -1;   // 上次显示的状态,避免每 200ms 重绘

static void tick(lv_timer_t *t)
{
    (void)t;
    int up = wifi_keep_is_up() ? 1 : 0;
    if (up == s_shown) return;
    s_shown = up;

    if (up) {
        char ip[24];
        wifi_keep_get_ip(ip, sizeof(ip));
        lv_label_set_text(s_status, "CONNECTED");
        lv_label_set_text_fmt(s_ip_label, "IP: %s", ip);
    } else {
        lv_label_set_text(s_status, "Connecting...");
        lv_label_set_text(s_ip_label, "IP: --");
    }
}

void demo_wifi_enter(void)
{
    s_shown = -1;
    s_scr = ui_pixel_screen_create("WI-FI");
    lv_obj_t *panel = ui_pixel_panel_create(s_scr, 22, 58, 196, 180, UI_PAPER);

    s_status = lv_label_create(panel);
    lv_obj_set_width(s_status, 168);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 20);
    lv_label_set_text(s_status, "Starting...");

    s_ip_label = lv_label_create(panel);
    lv_obj_set_style_text_font(s_ip_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ip_label, lv_color_hex(UI_SKY_DARK), 0);
    lv_obj_align(s_ip_label, LV_ALIGN_TOP_MID, 0, 60);
    lv_label_set_text(s_ip_label, "IP: --");

    s_mascot = ui_pixel_mascot_create(s_scr, 101, 244);
    s_timer = lv_timer_create(tick, 200, NULL);
    lv_screen_load(s_scr);
    wifi_keep_start();   // 幂等:已在网就直接显示状态
}

void demo_wifi_exit(void)
{
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    // Wi-Fi 常驻:退出只拆 UI,连接保持
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; s_status = s_ip_label = s_mascot = NULL; }
}

void demo_wifi_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (btn != BSP_BTN_OK || ev != BSP_BTN_CLICK) return;
    wifi_keep_start();   // 未连上时手动触发重连(已连则无操作)
}
