// main/wifi_keep.c —— Wi-Fi 常驻模块(全局唯一 STA)。
// 之前 Wi-Fi app 和语音 app 各自启停协议栈,退出即断网;
// 现在统一收口到这里:起一次,一直连,掉线自动重连,界面只读状态。
#include "wifi_keep.h"

#include "demo_radio.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "wifi_keep";

static esp_netif_t *s_sta;
static esp_event_handler_instance_t s_wifi_h, s_ip_h;
static SemaphoreHandle_t s_ip_sem;
static volatile bool s_inited;   // 协议栈已启动
static volatile bool s_up;       // 有 IP
static char s_ip[24];

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_inited) {
            s_up = false;
            esp_wifi_connect();   // 常驻:掉线即重连
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&evt->ip_info.ip));
        ESP_LOGI(TAG, "Wi-Fi 在线 IP=%s", s_ip);
        s_up = true;
        if (s_ip_sem) xSemaphoreGive(s_ip_sem);
    }
}

esp_err_t wifi_keep_start(void)
{
    if (s_inited) {
        if (!s_up) esp_wifi_connect();   // 手动触发一次重连
        return ESP_OK;
    }

    esp_err_t err = demo_radio_nvs_prepare();
    if (err != ESP_OK) return err;
    err = demo_radio_network_prepare();
    if (err != ESP_OK) return err;

    if (!s_ip_sem) s_ip_sem = xSemaphoreCreateBinary();
    if (!s_ip_sem) return ESP_ERR_NO_MEM;

    s_sta = esp_netif_create_default_wifi_sta();
    if (!s_sta) return ESP_ERR_NO_MEM;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return err;

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL, &s_wifi_h);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL, &s_ip_h);

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

    s_inited = true;
    ESP_LOGI(TAG, "Wi-Fi 常驻启动");
    return ESP_OK;
}

bool wifi_keep_is_up(void)
{
    return s_up;
}

esp_err_t wifi_keep_wait_ip(uint32_t ms)
{
    if (s_up) return ESP_OK;
    if (!s_inited) {
        esp_err_t err = wifi_keep_start();
        if (err != ESP_OK) return err;
    }
    // 清掉历史 give,只等新 IP 事件
    while (xSemaphoreTake(s_ip_sem, 0) == pdTRUE) {}
    return xSemaphoreTake(s_ip_sem, pdMS_TO_TICKS(ms)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

void wifi_keep_get_ip(char *buf, size_t len)
{
    if (!buf || !len) return;
    if (s_up) snprintf(buf, len, "%s", s_ip);
    else      snprintf(buf, len, "--");
}
