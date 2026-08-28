// main/demo_wifi.c —— Wi-Fi 联网:连接 AP(SSID/密码取自 Kconfig),DHCP 拿 IP 并显示。
#include "demo.h"
#include "demo_radio.h"
#include "ui_pixel.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lvgl.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "demo_wifi";

typedef enum {
    WIFI_OFF = 0,
    WIFI_STARTING,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_FAILED,
} wifi_state_t;

static lv_obj_t *s_scr, *s_status, *s_ip_label, *s_mascot;
static lv_timer_t *s_timer;
static esp_netif_t *s_sta;
static esp_event_handler_instance_t s_wifi_h, s_ip_h;
static volatile wifi_state_t s_state;
static bool s_wifi_started;
static char s_ip[24];

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        s_state = WIFI_CONNECTING;
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_state == WIFI_CONNECTED) {
            s_state = WIFI_CONNECTING;
            esp_wifi_connect();
        } else if (s_state == WIFI_CONNECTING) {
            s_state = WIFI_FAILED;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&evt->ip_info.ip));
        ESP_LOGI(TAG, "联网成功 IP=%s", s_ip);
        s_state = WIFI_CONNECTED;
    }
}

static esp_err_t wifi_start(void)
{
    s_state = WIFI_STARTING;
    esp_err_t err = demo_radio_nvs_prepare();
    if (err != ESP_OK) goto fail;
    err = demo_radio_network_prepare();
    if (err != ESP_OK) goto fail;

    s_sta = esp_netif_create_default_wifi_sta();
    if (!s_sta) { err = ESP_ERR_NO_MEM; goto fail; }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) goto fail;

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL, &s_wifi_h);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL, &s_ip_h);

    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);

    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid, CONFIG_VOICE_WIFI_SSID, sizeof(wc.sta.ssid) - 1);
    if (strlen(CONFIG_VOICE_WIFI_PASSWORD) > 0)
        strncpy((char *)wc.sta.password, CONFIG_VOICE_WIFI_PASSWORD, sizeof(wc.sta.password) - 1);
    err = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_start();
    if (err != ESP_OK) goto fail;
    s_wifi_started = true;
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "Wi-Fi 启动失败: %s", esp_err_to_name(err));
    s_state = WIFI_FAILED;
    return err;
}

static void wifi_stop(void)
{
    if (s_wifi_started) { esp_wifi_stop(); esp_wifi_deinit(); s_wifi_started = false; }
    if (s_wifi_h) { esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_h); s_wifi_h = NULL; }
    if (s_ip_h) { esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_h); s_ip_h = NULL; }
    if (s_sta) { esp_netif_destroy_default_wifi(s_sta); s_sta = NULL; }
    s_state = WIFI_OFF;
}

static void tick(lv_timer_t *t)
{
    (void)t;
    switch (s_state) {
    case WIFI_STARTING:
        lv_label_set_text(s_status, "Starting Wi-Fi...");
        break;
    case WIFI_CONNECTING:
        lv_label_set_text(s_status, "Connecting...");
        break;
    case WIFI_CONNECTED:
        lv_label_set_text(s_status, "CONNECTED");
        lv_label_set_text_fmt(s_ip_label, "IP: %s", s_ip);
        break;
    case WIFI_FAILED:
        lv_label_set_text(s_status, "Connect failed\n\nOK: RETRY");
        s_state = WIFI_OFF;
        break;
    default:
        break;
    }
}

void demo_wifi_enter(void)
{
    s_ip[0] = '\0';
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
    wifi_start();
}

void demo_wifi_exit(void)
{
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    wifi_stop();
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; s_status = s_ip_label = s_mascot = NULL; }
}

void demo_wifi_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (btn != BSP_BTN_OK || ev != BSP_BTN_CLICK || s_state != WIFI_OFF) return;
    wifi_start();
}
