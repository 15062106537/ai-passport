// main/demo_voice.c —— AI 语音助手(对接 jarvis-brain 后端)。
// 流程: 按住 OK 录音 → 松开打包 WAV → POST /speak → 收裸 PCM → 直接播放。
//
// 线程规则: 录音(bsp_audio_read)、网络、HTTP 都是阻塞操作,全在独立任务里做,
// 按键回调只发事件; 跨线程访问 LVGL 一律 bsp_lvgl_lock()/unlock()。
#include "demo.h"
#include "demo_radio.h"
#include "bsp_audio.h"
#include "bsp_display.h"
#include "ui_pixel.h"
#include "lvgl.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_http_client.h"

#include <string.h>
#include <stdlib.h>

static const char *TAG = "demo_voice";

#define SAMPLE_RATE   16000
#define RECORD_CHUNK  512
#define HTTP_BUF      2048

typedef enum { VOICE_EV_OK_DOWN = 1, VOICE_EV_OK_UP } voice_ev_t;
typedef enum { VS_CONNECTING, VS_READY, VS_RECORDING, VS_SENDING, VS_ERROR } voice_state_t;

static lv_obj_t *s_scr, *s_status, *s_mascot;
static volatile voice_state_t s_state;

static int16_t *s_rec;
static size_t   s_rec_cap, s_rec_len;

static esp_netif_t *s_sta;
static bool s_wifi_started;
static esp_event_handler_instance_t s_wifi_h, s_ip_h;
static SemaphoreHandle_t s_ip_got;
static QueueHandle_t s_evq;
static TaskHandle_t s_task;

static void status_text(const char *t) {
    if (!bsp_lvgl_lock(500)) return;
    if (s_status) lv_label_set_text(s_status, t);
    if (s_mascot) ui_pixel_mascot_jump(s_mascot);
    bsp_lvgl_unlock();
}

// 打包 44 字节标准 WAV 头(16kHz/16bit/单声道 PCM)
static void wav_header(uint8_t *h, uint32_t data_len) {
    uint32_t byte_rate = SAMPLE_RATE * 1 * 2;
    memcpy(h + 0,  "RIFF", 4);
    uint32_t riff = 36 + data_len;  memcpy(h + 4, &riff, 4);
    memcpy(h + 8,  "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);
    uint32_t fmt_len = 16;          memcpy(h + 16, &fmt_len, 4);
    uint16_t fmt = 1;               memcpy(h + 20, &fmt, 2);
    uint16_t ch = 1;                memcpy(h + 22, &ch, 2);
    uint32_t rate = SAMPLE_RATE;    memcpy(h + 24, &rate, 4);
    memcpy(h + 28, &byte_rate, 4);
    uint16_t block = 2;             memcpy(h + 32, &block, 2);
    uint16_t bits = 16;             memcpy(h + 34, &bits, 2);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &data_len, 4);
}

// 上传 WAV → 收裸 PCM → 流式播放
static esp_err_t upload_and_play(const uint8_t *wav, size_t wav_len) {
    esp_http_client_config_t cfg = {
        .url = CONFIG_VOICE_SERVER_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 20000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    esp_http_client_set_header(client, "Content-Type", "audio/wav");
    esp_http_client_set_header(client, "X-SN", CONFIG_VOICE_SN);
    esp_http_client_set_post_field(client, (const char *)wav, (int)wav_len);

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "http perform fail: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }
    if (esp_http_client_get_status_code(client) != 200) {
        ESP_LOGW(TAG, "http status %d", esp_http_client_get_status_code(client));
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    bsp_audio_set_format(SAMPLE_RATE, 16, 1);
    bsp_audio_set_volume(80);
    uint8_t buf[HTTP_BUF];
    int len;
    while ((len = esp_http_client_read(client, (char *)buf, sizeof(buf))) > 0) {
        bsp_audio_write(buf, (size_t)len);   // 裸 PCM,直接播放
    }
    esp_http_client_cleanup(client);
    return ESP_OK;
}

// ---------- Wi-Fi ----------
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = data;
        ESP_LOGI(TAG, "Got IP " IPSTR, IP2STR(&evt->ip_info.ip));
        if (s_ip_got) xSemaphoreGive(s_ip_got);
    }
}

static esp_err_t wifi_connect(void) {
    esp_err_t err = demo_radio_nvs_prepare();
    if (err != ESP_OK) return err;
    err = demo_radio_network_prepare();
    if (err != ESP_OK) return err;

    s_ip_got = xSemaphoreCreateBinary();
    s_sta = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return err;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL, &s_wifi_h);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL, &s_ip_h);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);

    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid, CONFIG_VOICE_WIFI_SSID, sizeof(wc.sta.ssid) - 1);
    if (strlen(CONFIG_VOICE_WIFI_PASSWORD) > 0)
        strncpy((char *)wc.sta.password, CONFIG_VOICE_WIFI_PASSWORD, sizeof(wc.sta.password) - 1);
    err = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (err != ESP_OK) return err;
    err = esp_wifi_start();
    if (err != ESP_OK) return err;
    s_wifi_started = true;
    esp_wifi_connect();

    if (xSemaphoreTake(s_ip_got, pdMS_TO_TICKS(15000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    return ESP_OK;
}

// ---------- 录音与发送 ----------
static void begin_record(void) {
    if (bsp_audio_set_format(SAMPLE_RATE, 16, 1) != ESP_OK) {
        status_text("audio fail"); s_state = VS_ERROR; return;
    }
    s_rec_len = 0;
    s_state = VS_RECORDING;
    status_text("REC...");
}

static void stop_and_send(void) {
    s_state = VS_SENDING;
    status_text("thinking...");
    size_t pcm_bytes = s_rec_len * sizeof(int16_t);
    uint8_t *wav = malloc(44 + pcm_bytes);
    if (!wav) { status_text("mem fail"); s_state = VS_READY; return; }
    wav_header(wav, (uint32_t)pcm_bytes);
    memcpy(wav + 44, s_rec, pcm_bytes);

    if (upload_and_play(wav, 44 + pcm_bytes) == ESP_OK) {
        status_text("READY. hold OK = talk");
    } else {
        status_text("server fail");
    }
    free(wav);
    s_state = VS_READY;
}

static void voice_task(void *arg) {
    (void)arg;
    status_text("Wi-Fi...");
    if (wifi_connect() != ESP_OK) {
        status_text("wifi fail"); vTaskDelete(NULL); return;
    }
    s_state = VS_READY;
    status_text("READY. hold OK = talk");

    int16_t tmp[RECORD_CHUNK];
    for (;;) {
        voice_ev_t ev;
        if (xQueueReceive(s_evq, &ev, pdMS_TO_TICKS(20)) == pdTRUE) {
            if (ev == VOICE_EV_OK_DOWN && s_state == VS_READY) begin_record();
            else if (ev == VOICE_EV_OK_UP && s_state == VS_RECORDING) { stop_and_send(); continue; }
        }
        if (s_state == VS_RECORDING) {
            if (s_rec_len + RECORD_CHUNK <= s_rec_cap) {
                if (bsp_audio_read(tmp, RECORD_CHUNK * sizeof(int16_t)) == ESP_OK) {
                    memcpy(s_rec + s_rec_len, tmp, RECORD_CHUNK * sizeof(int16_t));
                    s_rec_len += RECORD_CHUNK;
                }
            } else {
                stop_and_send();   // 录满自动发送
            }
        }
    }
}

// ---------- app 接口 ----------
void demo_voice_enter(void) {
    s_rec_cap = (size_t)CONFIG_VOICE_RECORD_SEC * SAMPLE_RATE;
    s_rec = malloc(s_rec_cap * sizeof(int16_t));
    if (!s_rec) { ESP_LOGE(TAG, "rec buf alloc failed"); return; }

    s_scr = ui_pixel_screen_create("VOICE AI");
    lv_obj_t *panel = ui_pixel_panel_create(s_scr, 12, 54, 216, 150, UI_PAPER);
    s_status = lv_label_create(panel);
    lv_obj_set_width(s_status, 196);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_status, "starting...");
    lv_obj_center(s_status);
    s_mascot = ui_pixel_mascot_create(s_scr, 101, 238);

    s_state = VS_CONNECTING;
    s_evq = xQueueCreate(8, sizeof(voice_ev_t));
    if (!s_task) xTaskCreate(voice_task, "demo_voice", 8192, NULL, 4, &s_task);
    lv_screen_load(s_scr);
}

void demo_voice_exit(void) {
    if (s_task) { vTaskDelete(s_task); s_task = NULL; }
    if (s_evq) { vQueueDelete(s_evq); s_evq = NULL; }
    if (s_wifi_h) { esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_h); s_wifi_h = NULL; }
    if (s_ip_h)   { esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_h); s_ip_h = NULL; }
    if (s_wifi_started) { esp_wifi_stop(); esp_wifi_deinit(); s_wifi_started = false; }
    if (s_sta) { esp_netif_destroy_default_wifi(s_sta); s_sta = NULL; }
    if (s_ip_got) { vSemaphoreDelete(s_ip_got); s_ip_got = NULL; }
    if (s_rec) { free(s_rec); s_rec = NULL; }
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; s_status = s_mascot = NULL; }
}

// 按键回调在 button 组件任务里运行,只发事件,不阻塞。OK 长按返回由 main.c 拦截。
void demo_voice_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (btn != BSP_BTN_OK) return;
    if (ev == BSP_BTN_PRESS && s_evq) {
        voice_ev_t e = VOICE_EV_OK_DOWN; xQueueSend(s_evq, &e, 0);
    } else if (ev == BSP_BTN_CLICK && s_evq) {
        voice_ev_t e = VOICE_EV_OK_UP; xQueueSend(s_evq, &e, 0);
    }
}
