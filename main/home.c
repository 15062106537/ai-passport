// main/home.c —— 首页:身份识别卡卡包。
// 一屏一张卡,UP/DOWN 翻卡(带滑动动画),底部圆点指示。
// 信息卡(学生卡/开发者卡/活动胸牌)按 OK 只让吉祥物跳一下;
// 功能卡(APPS/VOICE)按 OK 由 main.c 统一进入对应功能。
#include "home.h"
#include "ui_pixel.h"
#include "lvgl.h"

#define N        HOME_CARD_COUNT
#define VIEW_X   14
#define VIEW_Y   50
#define VIEW_W   212
#define VIEW_H   202
#define CARD_X   6
#define CARD_W   200
#define CARD_H   196
#define PITCH    208

typedef struct {
    const char *header;   // 卡头
    const char *icon;     // 照片区大字
    const char *line1;    // 主信息
    const char *line2;    // 副信息
    const char *id;       // 底部编号/提示
    uint32_t accent;      // 照片区颜色
    uint32_t bg;          // 卡面底色
    bool action;          // true: OK 进入功能
} card_def_t;

static const card_def_t CARDS[N] = {
    { "STUDENT CARD", "G", "CHANG'AN", "GEOLOGY",      "ID NO.001", 0x82BE2D, 0xEAF6D8, false },
    { "DEVELOPER",    "D", "MAKER",    "TRAE x ESP32", "ID NO.002", 0x7557D9, 0xE9E4F8, false },
    { "EVENT BADGE",  "T", "TRAE",     "AI PASSPORT",  "ID NO.003", 0xE43B2F, 0xFBE3DC, false },
    { "APPS",         "A", "OPEN",     "APPS",         "PRESS OK",  0x1689E8, 0xDAEDFF, true  },
    { "VOICE AI",     "V", "TALK",     "TO ME",        "PRESS OK",  0xFFB23E, 0xFFF3C4, true  },
};

static const uint8_t BAR_W[] = { 3, 2, 5, 2, 3, 4, 2, 6, 2, 3, 5, 2, 4, 3, 2, 6, 3, 2, 4, 2 };

static lv_obj_t *s_scr;
static lv_obj_t *s_film;
static lv_obj_t *s_mascot;
static lv_obj_t *s_dot[N];
static int s_sel;
static int s_identity = HOME_CARD_STUDENT;   // 最后浏览的身份卡

static lv_obj_t *flat(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    return obj;
}

static void build_card(int i)
{
    const card_def_t *c = &CARDS[i];
    lv_obj_t *card = ui_pixel_panel_create(s_film, CARD_X, i * PITCH, CARD_W, CARD_H, c->bg);

    lv_obj_t *header = ui_pixel_label(card, c->header, &lv_font_montserrat_14, UI_INK);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);

    // 照片区(像素小人头像位)
    flat(card, 10, 26, 56, 56, UI_INK);
    lv_obj_t *photo = flat(card, 6, 22, 56, 56, c->accent);
    lv_obj_set_style_border_color(photo, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_border_width(photo, 3, 0);
    lv_obj_t *icon = ui_pixel_label(photo, c->icon, &lv_font_montserrat_20, 0xFFFFFF);
    lv_obj_center(icon);

    lv_obj_t *l1 = ui_pixel_label(card, c->line1, &lv_font_montserrat_20, UI_INK);
    lv_obj_align(l1, LV_ALIGN_TOP_LEFT, 74, 26);
    lv_obj_t *l2 = ui_pixel_label(card, c->line2, &lv_font_montserrat_14, UI_INK);
    lv_obj_align(l2, LV_ALIGN_TOP_LEFT, 74, 56);

    // 像素条码
    int x = 10;
    for (size_t b = 0; b < sizeof(BAR_W); b++) {
        flat(card, x, 92, BAR_W[b], 28, UI_INK);
        x += BAR_W[b] + 3;
    }

    lv_obj_t *id = ui_pixel_label(card, c->id, &lv_font_montserrat_14, UI_INK);
    lv_obj_align(id, LV_ALIGN_BOTTOM_MID, 0, -4);
}

static void refresh_dots(void)
{
    for (int i = 0; i < N; i++) {
        bool sel = (i == s_sel);
        lv_obj_set_style_bg_color(s_dot[i], lv_color_hex(sel ? UI_YELLOW : 0xFFFFFF), 0);
        lv_obj_set_style_border_color(s_dot[i], lv_color_hex(sel ? UI_INK : UI_INK), 0);
    }
}

static void film_y_cb(void *obj, int32_t v)
{
    lv_obj_set_y((lv_obj_t *)obj, v);
}

static void goto_card(void)
{
    lv_anim_delete(s_film, film_y_cb);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_film);
    lv_anim_set_exec_cb(&a, film_y_cb);
    lv_anim_set_values(&a, lv_obj_get_y(s_film), VIEW_Y - s_sel * PITCH);
    lv_anim_set_duration(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

void home_init(void)
{
    s_sel = 0;
    s_scr = ui_pixel_screen_create("ID CARDS");

    // 卡片胶片(裁切视口,只露出当前卡)
    s_film = lv_obj_create(s_scr);
    lv_obj_remove_flag(s_film, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_film, VIEW_X, VIEW_Y);
    lv_obj_set_size(s_film, VIEW_W, VIEW_H);
    lv_obj_set_style_bg_opa(s_film, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_film, 0, 0);
    lv_obj_set_style_pad_all(s_film, 0, 0);

    for (int i = 0; i < N; i++) build_card(i);

    // 圆点指示
    int x0 = (240 - (N * 8 + (N - 1) * 8)) / 2;
    for (int i = 0; i < N; i++) {
        s_dot[i] = lv_obj_create(s_scr);
        lv_obj_remove_flag(s_dot[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(s_dot[i], x0 + i * 16, 260);
        lv_obj_set_size(s_dot[i], 8, 8);
        lv_obj_set_style_radius(s_dot[i], 0, 0);
        lv_obj_set_style_border_color(s_dot[i], lv_color_hex(UI_INK), 0);
        lv_obj_set_style_border_width(s_dot[i], 2, 0);
        lv_obj_set_style_pad_all(s_dot[i], 0, 0);
    }

    s_mascot = ui_pixel_mascot_create(s_scr, 4, 234);
    refresh_dots();
}

void home_show(void)
{
    refresh_dots();
    lv_obj_set_y(s_film, VIEW_Y - s_sel * PITCH);
    lv_screen_load(s_scr);
}

bool home_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return false;
    if (btn == BSP_BTN_UP) {
        s_sel = (s_sel + N - 1) % N;
        if (s_sel <= HOME_CARD_EVENT) s_identity = s_sel;
        goto_card();
        refresh_dots();
        return true;
    }
    if (btn == BSP_BTN_DOWN) {
        s_sel = (s_sel + 1) % N;
        if (s_sel <= HOME_CARD_EVENT) s_identity = s_sel;
        goto_card();
        refresh_dots();
        return true;
    }
    if (btn == BSP_BTN_OK) {
        if (CARDS[s_sel].action) return false;   // 交给 main.c 进入功能
        ui_pixel_mascot_jump(s_mascot);
        return true;
    }
    return false;
}

home_card_t home_get_selected(void)
{
    return (home_card_t)s_sel;
}

home_card_t home_get_identity(void)
{
    return (home_card_t)s_identity;
}
