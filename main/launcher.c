// main/launcher.c —— 手机式桌面:状态栏(电量) + 可滚动彩色应用图标网格。
// 复用 ui_pixel 的像素风配色与几何风格,交互走三键(UP/DOWN/OK)。
#include "launcher.h"
#include "ui_pixel.h"
#include "bsp_battery.h"
#include "esp_log.h"
#include <string.h>

#define MAX_APPS  24
#define COLS      2
#define ICON_W    100
#define ICON_H    56
#define ROW_H     72
#define GRID_LEFT 8
#define COL_GAP   24
#define GRID_TOP  40
#define GRID_H    246      // 40..286(草地前)

static const char *TAG = "launcher";

static const app_entry_t *s_apps;
static size_t s_count;
static bool s_avail[MAX_APPS];

static lv_obj_t *s_scr;
static lv_obj_t *s_grid;         // 滚动容器
static lv_obj_t *s_content;      // 内容容器(撑开滚动范围)
static lv_obj_t *s_icon[MAX_APPS];
static lv_obj_t *s_glyph[MAX_APPS];
static lv_obj_t *s_name[MAX_APPS];
static lv_obj_t *s_batt;         // 状态栏电量
static int s_sel;
static lv_timer_t *s_timer;

// 画一个纯色矩形(无边框、无内边距、不可滚动)
static lv_obj_t *rect(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    return o;
}

// 刷新第 i 个图标的高亮/可用状态
static void icon_refresh(size_t i)
{
    bool sel   = ((int)i == s_sel);
    bool avail = s_avail[i];
    lv_obj_set_style_bg_color(s_icon[i], lv_color_hex(avail ? s_apps[i].color : 0x78909C), 0);
    lv_obj_set_style_border_color(s_icon[i], lv_color_hex(sel ? 0xFFFFFF : UI_INK), 0);
    lv_obj_set_style_border_width(s_icon[i], sel ? 4 : 2, 0);
    uint32_t fg = avail ? UI_INK : 0xFFFFFF;
    lv_obj_set_style_text_color(s_glyph[i], lv_color_hex(fg), 0);
    lv_obj_set_style_text_color(s_name[i],  lv_color_hex(fg), 0);
}

static void refresh_all(void)
{
    for (size_t i = 0; i < s_count; i++) icon_refresh(i);
    if (s_count && s_scr) lv_obj_scroll_to_view(s_icon[s_sel], LV_ANIM_ON);
}

static void tick(lv_timer_t *t)
{
    (void)t;
    int soc = bsp_battery_soc();
    if (soc >= 0 && soc <= 100) lv_label_set_text_fmt(s_batt, "%d%%", soc);
    else                        lv_label_set_text(s_batt, "--");
}

void launcher_init(const app_entry_t *apps, size_t count)
{
    s_apps = apps;
    s_count = count > MAX_APPS ? MAX_APPS : count;
    memset(s_avail, 1, sizeof(s_avail));
    s_sel = 0;

    // 屏幕:像素风天空 + 草地
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(UI_SKY), 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    rect(s_scr, 0, 286, 240, 34, UI_GRASS);
    rect(s_scr, 0, 286, 240, 4, 0xA7D93E);

    // 状态栏(深色条):左标题 + 右电量
    lv_obj_t *bar = rect(s_scr, 0, 0, 240, 40, UI_INK);
    lv_obj_t *title = lv_label_create(bar);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_label_set_text(title, "APPS");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 10, 0);
    s_batt = lv_label_create(bar);
    lv_obj_set_style_text_font(s_batt, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_batt, lv_color_white(), 0);
    lv_label_set_text(s_batt, "--");
    lv_obj_align(s_batt, LV_ALIGN_RIGHT_MID, -10, 0);

    // 滚动区
    s_grid = lv_obj_create(s_scr);
    lv_obj_set_pos(s_grid, 0, GRID_TOP);
    lv_obj_set_size(s_grid, 240, GRID_H);
    lv_obj_set_style_bg_opa(s_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_grid, 0, 0);
    lv_obj_set_style_pad_all(s_grid, 0, 0);
    lv_obj_set_scrollbar_mode(s_grid, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(s_grid, LV_DIR_VER);

    size_t rows = (s_count + COLS - 1) / COLS;
    int content_h = 8 + (int)rows * ROW_H;
    // 撑高对象:让滚动范围覆盖所有图标(LVGL 绝对定位对象不会自动撑高滚动范围)
    s_content = lv_obj_create(s_grid);
    lv_obj_remove_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_content, 240, content_h);
    lv_obj_set_style_bg_opa(s_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_content, 0, 0);
    lv_obj_set_style_pad_all(s_content, 0, 0);

    for (size_t i = 0; i < s_count; i++) {
        int col = (int)(i % COLS);
        int row = (int)(i / COLS);
        int x = GRID_LEFT + col * (ICON_W + COL_GAP);
        int y = 8 + row * ROW_H;

        s_icon[i] = lv_obj_create(s_grid);
        lv_obj_remove_flag(s_icon[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(s_icon[i], x, y);
        lv_obj_set_size(s_icon[i], ICON_W, ICON_H);
        lv_obj_set_style_radius(s_icon[i], 10, 0);
        lv_obj_set_style_pad_all(s_icon[i], 0, 0);

        s_glyph[i] = lv_label_create(s_icon[i]);
        lv_obj_set_style_text_font(s_glyph[i], &lv_font_montserrat_20, 0);
        lv_label_set_text(s_glyph[i], s_apps[i].glyph);
        lv_obj_align(s_glyph[i], LV_ALIGN_CENTER, 0, -8);

        s_name[i] = lv_label_create(s_icon[i]);
        lv_obj_set_style_text_font(s_name[i], &lv_font_montserrat_14, 0);
        lv_label_set_text(s_name[i], s_apps[i].name);
        lv_obj_align(s_name[i], LV_ALIGN_BOTTOM_MID, 0, -3);
    }

    s_timer = lv_timer_create(tick, 1000, NULL);
    refresh_all();
    ESP_LOGI(TAG, "desktop ready: %u apps", (unsigned)s_count);
}

void launcher_set_available(size_t index, bool avail)
{
    if (index >= s_count) return;
    s_avail[index] = avail;
}

void launcher_show(void)
{
    refresh_all();
    lv_screen_load(s_scr);
}

void launcher_deinit(void)
{
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; }
    for (int i = 0; i < MAX_APPS; i++) {
        s_icon[i] = NULL; s_glyph[i] = NULL; s_name[i] = NULL;
    }
    s_grid = NULL; s_content = NULL; s_batt = NULL;
    s_count = 0; s_apps = NULL;
    s_sel = 0;
}

bool launcher_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return false;
    if (btn == BSP_BTN_UP && s_count) {
        s_sel = (s_sel + (int)s_count - 1) % (int)s_count;
        refresh_all();
        return true;
    }
    if (btn == BSP_BTN_DOWN && s_count) {
        s_sel = (s_sel + 1) % (int)s_count;
        refresh_all();
        return true;
    }
    return false;
}

int launcher_get_selected(void)
{
    return s_sel;
}
