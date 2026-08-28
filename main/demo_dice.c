// main/demo_dice.c —— 骰子 / 双骰 / 硬币 / 抽号 聚会小工具。
// UP/DOWN 切换模式, OK 摇一次。用硬件随机数 esp_random。
#include "demo.h"
#include "bsp_display.h"
#include "ui_pixel.h"
#include "lvgl.h"
#include "esp_random.h"
#include <stdio.h>

static lv_obj_t *s_scr, *s_mode_label, *s_result, *s_mascot;
static int s_mode;

static const char *MODES[] = {"DICE", "2 DICE", "COIN", "PICK 1-100"};
#define MODE_COUNT (sizeof(MODES) / sizeof(MODES[0]))

static void refresh(void) {
    lv_label_set_text(s_mode_label, MODES[s_mode]);
}

static void roll(void) {
    char buf[32];
    switch (s_mode) {
    case 0: snprintf(buf, sizeof(buf), "%u", (unsigned)(esp_random() % 6 + 1)); break;
    case 1: snprintf(buf, sizeof(buf), "%u", (unsigned)(esp_random() % 6 + 1
                                                        + esp_random() % 6 + 1)); break;
    case 2: snprintf(buf, sizeof(buf), "%s", (esp_random() & 1) ? "HEADS" : "TAILS"); break;
    default: snprintf(buf, sizeof(buf), "%u", (unsigned)(esp_random() % 100 + 1)); break;
    }
    lv_label_set_text(s_result, buf);
    ui_pixel_mascot_jump(s_mascot);
}

void demo_dice_enter(void) {
    s_mode = 0;
    s_scr = ui_pixel_screen_create("DICE");
    lv_obj_t *panel = ui_pixel_panel_create(s_scr, 18, 58, 204, 150, UI_PAPER);

    s_result = lv_label_create(panel);
    lv_obj_set_style_text_font(s_result, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_result, lv_color_hex(UI_INK), 0);
    lv_label_set_text(s_result, "?");
    lv_obj_align(s_result, LV_ALIGN_CENTER, 0, -10);

    s_mode_label = lv_label_create(panel);
    lv_obj_set_style_text_font(s_mode_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_mode_label, lv_color_hex(UI_SKY_DARK), 0);
    lv_obj_align(s_mode_label, LV_ALIGN_BOTTOM_MID, 0, -6);

    s_mascot = ui_pixel_mascot_create(s_scr, 101, 238);
    refresh();
    lv_screen_load(s_scr);
}

void demo_dice_exit(void) {
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; s_mode_label = s_result = s_mascot = NULL; }
}

void demo_dice_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_OK) {
        roll();
    } else if (btn == BSP_BTN_UP) {
        s_mode = (s_mode + MODE_COUNT - 1) % MODE_COUNT; refresh();
    } else if (btn == BSP_BTN_DOWN) {
        s_mode = (s_mode + 1) % MODE_COUNT; refresh();
    }
}
