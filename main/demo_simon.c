// main/demo_simon.c —— 记忆大师(Simon Says)。
// 两个色块随机序列, 闪色 + 喇叭发不同音调, 玩家用 UP=左 / DOWN=右 复现。
// 序列越来越长, 记错即结束显示等级。OK 开始/重开。
#include "demo.h"
#include "bsp_audio.h"
#include "bsp_display.h"
#include "ui_pixel.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_random.h"

#define SAMPLE_RATE 16000
#define MAX_SEQ     32
#define CMD_LEFT    0
#define CMD_RIGHT   1
#define CMD_START   2

static lv_obj_t *s_scr, *s_left, *s_right, *s_status, *s_mascot;
static QueueHandle_t s_evq;
static TaskHandle_t s_task;
static int s_seq[MAX_SEQ];
static int s_len;
static int s_level;

static void set_status(const char *t) {
    if (!bsp_lvgl_lock(500)) return;
    if (s_status) lv_label_set_text(s_status, t);
    bsp_lvgl_unlock();
}

// 短音:方波(阻塞,只在本任务里调用)
static void beep(int hz, int ms) {
    if (bsp_audio_set_format(SAMPLE_RATE, 16, 1) != ESP_OK) return;
    bsp_audio_set_volume(70);
    int16_t buf[512];
    int total = SAMPLE_RATE * ms / 1000;
    int period = SAMPLE_RATE / hz;
    int phase = 0;
    while (total > 0) {
        int n = total < 512 ? total : 512;
        for (int i = 0; i < n; i++) {
            buf[i] = (phase < period / 2) ? 4000 : -4000;
            if (++phase >= period) phase = 0;
        }
        bsp_audio_write(buf, (size_t)n * sizeof(int16_t));
        total -= n;
    }
}

static void flash(int idx) {
    lv_obj_t *b = (idx == 0) ? s_left : s_right;
    uint32_t normal = (idx == 0) ? UI_RED : UI_SKY;
    if (bsp_lvgl_lock(500)) {
        lv_obj_set_style_bg_color(b, lv_color_hex(0xFFFFFF), 0);
        bsp_lvgl_unlock();
    }
    beep(idx == 0 ? 500 : 1000, 240);
    if (bsp_lvgl_lock(500)) {
        lv_obj_set_style_bg_color(b, lv_color_hex(normal), 0);
        bsp_lvgl_unlock();
    }
    vTaskDelay(pdMS_TO_TICKS(160));
}

static void play_sequence(void) {
    for (int i = 0; i < s_len; i++) flash(s_seq[i]);
}

static void simon_task(void *arg) {
    (void)arg;
    for (;;) {
        int cmd;
        // 等开始
        if (xQueueReceive(s_evq, &cmd, portMAX_DELAY) != pdTRUE) continue;
        if (cmd != CMD_START) continue;

        s_len = 1;
        s_level = 0;
        for (;;) {
            s_seq[s_len - 1] = esp_random() % 2;
            set_status("WATCH...");
            vTaskDelay(pdMS_TO_TICKS(400));
            play_sequence();

            set_status("YOUR TURN");
            bool ok = true;
            for (int i = 0; i < s_len; i++) {
                int in;
                if (xQueueReceive(s_evq, &in, pdMS_TO_TICKS(5000)) != pdTRUE) { ok = false; break; }
                if (in == CMD_START) { ok = false; break; }
                flash(in);          // 按键反馈:闪一下对应色块
                if (in != s_seq[i]) { ok = false; break; }
            }
            if (!ok) break;
            s_len++;
            s_level++;
            if (s_len > MAX_SEQ) break;
        }
        char buf[40];
        snprintf(buf, sizeof(buf), "GAME OVER\nLEVEL %d\nOK: AGAIN", s_level);
        set_status(buf);
    }
}

void demo_simon_enter(void) {
    s_scr = ui_pixel_screen_create("SIMON");
    s_left = ui_pixel_panel_create(s_scr, 18, 62, 96, 120, UI_RED);
    s_right = ui_pixel_panel_create(s_scr, 126, 62, 96, 120, UI_SKY);
    s_status = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_label_set_text(s_status, "UP=L  DOWN=R\nOK: START");
    s_mascot = ui_pixel_mascot_create(s_scr, 101, 238);

    s_evq = xQueueCreate(16, sizeof(int));
    if (!s_task) xTaskCreate(simon_task, "demo_simon", 4096, NULL, 4, &s_task);
    lv_screen_load(s_scr);
}

void demo_simon_exit(void) {
    if (s_task) { vTaskDelete(s_task); s_task = NULL; }
    if (s_evq) { vQueueDelete(s_evq); s_evq = NULL; }
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; s_left = s_right = s_status = s_mascot = NULL; }
}

void demo_simon_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev != BSP_BTN_CLICK || !s_evq) return;
    int c = -1;
    if (btn == BSP_BTN_OK)   c = CMD_START;
    else if (btn == BSP_BTN_UP)   c = CMD_LEFT;
    else if (btn == BSP_BTN_DOWN) c = CMD_RIGHT;
    if (c >= 0) xQueueSend(s_evq, &c, 0);
    if (s_mascot && btn == BSP_BTN_OK) { if (bsp_lvgl_lock(500)) { ui_pixel_mascot_jump(s_mascot); bsp_lvgl_unlock(); } }
}
