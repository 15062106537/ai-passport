// main/demo_ble.c —— 可连接 BLE + GATT 服务。
// 手机可连上 "FoloPassport"：
//   读 0xab01 特征 -> 返回设备信息字符串
//   写 0xab02 特征 -> 文本显示到屏幕
#include "demo.h"
#include "demo_radio.h"
#include "ui_pixel.h"
#include "bsp_display.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "lvgl.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>

static const char *TAG = "demo_ble";
static const char *DEVICE_NAME = "FoloPassport";

typedef enum {
    BLE_OFF = 0,
    BLE_STARTING,
    BLE_ADVERTISING,
    BLE_CONNECTED,
    BLE_FAILED,
} ble_state_t;

static lv_obj_t *s_scr, *s_status, *s_rx_label;
static lv_timer_t *s_timer;
static SemaphoreHandle_t s_host_stopped;
static volatile ble_state_t s_state;
static volatile int s_error;
static uint8_t s_addr_type;
static bool s_initialized;
static bool s_connected;

// GATT 数据
static char s_info[32] = "FoloPassport v1.0";
static char s_rx[64];

// 自定义 128-bit UUID(服务 + 两个特征)
static const ble_uuid128_t svc_uuid = BLE_UUID128_INIT(
    0xab, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xee);
static const ble_uuid128_t info_uuid = BLE_UUID128_INIT(
    0xab, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xee);
static const ble_uuid128_t write_uuid = BLE_UUID128_INIT(
    0xab, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xee);

static int gap_event(struct ble_gap_event *event, void *arg);

// 读特征：返回设备信息
static int info_access(uint16_t conn, uint16_t attr,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    int rc = os_mbuf_append(ctxt->om, s_info, strlen(s_info));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

// 写特征：把手机发来的文本显示到屏幕
static int write_access(uint16_t conn, uint16_t attr,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len >= sizeof(s_rx)) len = sizeof(s_rx) - 1;
    if (len > 0) {
        ble_hs_mbuf_to_flat(ctxt->om, s_rx, sizeof(s_rx) - 1, &len);
        s_rx[len] = '\0';
        ESP_LOGI(TAG, "收到手机数据: %s", s_rx);
        if (bsp_lvgl_lock(100)) {
            if (s_rx_label) lv_label_set_text_fmt(s_rx_label, "RX: %s", s_rx);
            bsp_lvgl_unlock();
        }
    }
    return 0;
}

// GATT 服务定义
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = &info_uuid.u, .access_cb = info_access, .flags = BLE_GATT_CHR_F_READ, },
            { .uuid = &write_uuid.u, .access_cb = write_access, .flags = BLE_GATT_CHR_F_WRITE, },
            { 0 },
        },
    },
    { 0 },
};

static int advertise(void)
{
    struct ble_hs_adv_fields fields = { 0 };
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (const uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) return rc;

    struct ble_gap_adv_params params = { 0 };
    params.conn_mode = BLE_GAP_CONN_MODE_UND;   // 可连接
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc == 0) s_state = BLE_ADVERTISING;
    return rc;
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        s_connected = true;
        s_state = BLE_CONNECTED;
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        s_connected = false;
        s_state = BLE_ADVERTISING;
        advertise();               // 断开后重新广播
        break;
    default:
        break;
    }
    return 0;
}

static void on_reset(int reason)
{
    s_error = reason;
    s_state = BLE_FAILED;
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &s_addr_type);
    if (rc == 0) rc = advertise();
    if (rc != 0) { s_error = rc; s_state = BLE_FAILED; }
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    if (s_host_stopped) xSemaphoreGive(s_host_stopped);
    nimble_port_freertos_deinit();
}

static esp_err_t ble_start(void)
{
    s_state = BLE_STARTING;
    esp_err_t err = demo_radio_nvs_prepare();
    if (err != ESP_OK) { s_error = err; s_state = BLE_FAILED; return err; }

    err = nimble_port_init();
    if (err != ESP_OK) { s_error = err; s_state = BLE_FAILED; return err; }
    s_initialized = true;

    s_host_stopped = xSemaphoreCreateBinary();
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(DEVICE_NAME);

    // 注册自定义 GATT 服务
    int rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc == 0) rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0) { s_error = rc; s_state = BLE_FAILED; return ESP_FAIL; }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

static void ble_stop(void)
{
    if (!s_initialized) return;
    ble_gap_adv_stop();
    int rc = nimble_port_stop();
    if (rc == 0 && s_host_stopped) xSemaphoreTake(s_host_stopped, portMAX_DELAY);
    nimble_port_deinit();
    s_initialized = false;
    s_state = BLE_OFF;
}

static void tick(lv_timer_t *timer)
{
    (void)timer;
    switch (s_state) {
    case BLE_STARTING:
        lv_label_set_text(s_status, "Starting BLE...");
        break;
    case BLE_ADVERTISING:
        lv_label_set_text(s_status, "ADVERTISING\n\nName: FoloPassport\n\nConnectable\nOK: RESTART");
        break;
    case BLE_CONNECTED:
        lv_label_set_text(s_status, "CONNECTED");
        break;
    case BLE_FAILED:
        lv_label_set_text_fmt(s_status, "BLE failed: %d", s_error);
        s_state = BLE_OFF;
        break;
    default:
        break;
    }
}

void demo_ble_enter(void)
{
    s_rx[0] = '\0';
    s_connected = false;
    s_scr = ui_pixel_screen_create("BLUETOOTH LE");
    lv_obj_t *panel = ui_pixel_panel_create(s_scr, 22, 58, 196, 180, UI_PAPER);

    s_status = lv_label_create(panel);
    lv_obj_set_width(s_status, 168);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 12);
    lv_label_set_text(s_status, "Starting...");

    s_rx_label = lv_label_create(panel);
    lv_obj_set_width(s_rx_label, 168);
    lv_obj_set_style_text_font(s_rx_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_rx_label, lv_color_hex(UI_SKY_DARK), 0);
    lv_obj_set_style_text_align(s_rx_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_rx_label, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_label_set_text(s_rx_label, "");

    ui_pixel_mascot_create(s_scr, 101, 244);
    s_timer = lv_timer_create(tick, 200, NULL);
    lv_screen_load(s_scr);
    ble_start();
}

void demo_ble_exit(void)
{
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    ble_stop();
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; s_status = s_rx_label = NULL; }
}

void demo_ble_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (btn != BSP_BTN_OK || ev != BSP_BTN_CLICK || !s_initialized) return;
    if (s_connected) return;
    ble_gap_adv_stop();
    int rc = advertise();
    if (rc != 0) { s_error = rc; s_state = BLE_FAILED; }
}
