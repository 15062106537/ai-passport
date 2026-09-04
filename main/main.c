// main/main.c —— 三级导航:首页(身份识别卡卡包) → Apps集合/语音助手 → 具体应用。
//
// 按键语义:
//   首页: 上/下 翻身份卡; 确定 在功能卡(APPS/VOICE)上进入对应功能
//   Apps集合: 上/下 移动图标; 确定 进入应用; 长按确定 返回首页
//   具体应用: 长按确定 返回 Apps集合
//   语音助手: 长按确定 返回首页
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_pins.h"
#include "demo.h"
#include "home.h"
#include "launcher.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_sleep.h"

static const char *TAG = "main";

// 应用表(Apps 集合里的图标)。新增应用 = 实现 enter/exit/key 后在这里加一行。
static const app_entry_t APPS[] = {
    { "Display",  "D", 0xE43B2F, demo_display_enter,     demo_display_exit,     demo_display_key     },
    { "Button",   "B", 0xFFB23E, demo_button_enter,      demo_button_exit,      demo_button_key      },
    { "Audio",    "A", 0x7557D9, demo_audio_enter,       demo_audio_exit,       demo_audio_key       },
    { "Battery",  "%", 0x82BE2D, demo_battery_enter,     demo_battery_exit,     demo_battery_key     },
    { "Wi-Fi",    "W", 0x1689E8, demo_wifi_enter,        demo_wifi_exit,        demo_wifi_key        },
    { "BLE",      "L", 0x00BCD4, demo_ble_enter,         demo_ble_exit,         demo_ble_key         },
    { "Sleep",    "Z", 0x0872C9, demo_low_power_enter,   demo_low_power_exit,   demo_low_power_key   },
    { "Dice",     "6", 0xF57C00, demo_dice_enter,        demo_dice_exit,        demo_dice_key        },
    { "Reaction", "R", 0xC62828, demo_reaction_enter,    demo_reaction_exit,    demo_reaction_key    },
    { "Simon",    "S", 0x9C27B0, demo_simon_enter,       demo_simon_exit,       demo_simon_key       },
    { "Voice AI", "V", 0xFFD928, demo_voice_enter,       demo_voice_exit,       demo_voice_key       },
};
#define APP_COUNT (sizeof(APPS) / sizeof(APPS[0]))

typedef enum { LV_HOME, LV_APPS, LV_APP, LV_VOICE } level_t;

static bool s_ok[APP_COUNT];
static level_t s_level = LV_HOME;
static int s_active_app = -1;

// 按键回调运行在 button 组件任务里,操作 LVGL 必须加锁。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    if (!bsp_lvgl_lock(500)) return;

    switch (s_level) {
    case LV_HOME:
        if (!home_key(btn, ev) && btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
            // home_key 不消费 OK = 当前是功能卡
            if (home_get_selected() == HOME_CARD_APPS) {
                launcher_init(APPS, APP_COUNT);
                for (size_t i = 0; i < APP_COUNT; i++) launcher_set_available(i, s_ok[i]);
                s_level = LV_APPS;
                launcher_show();
            } else if (home_get_selected() == HOME_CARD_VOICE) {
                s_level = LV_VOICE;
                demo_voice_enter();
            }
        }
        break;

    case LV_APPS:
        if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
            launcher_deinit();
            s_level = LV_HOME;
            home_show();
        } else if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
            int sel = launcher_get_selected();
            if (sel >= 0 && sel < (int)APP_COUNT && s_ok[sel]) {
                s_active_app = sel;
                s_level = LV_APP;
                APPS[sel].enter();
            }
        } else {
            launcher_key(btn, ev);
        }
        break;

    case LV_APP:
        if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
            APPS[s_active_app].exit();
            s_level = LV_APPS;
            launcher_show();
        } else {
            APPS[s_active_app].key(btn, ev);
        }
        break;

    case LV_VOICE:
        if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
            demo_voice_exit();
            s_level = LV_HOME;
            home_show();
        } else {
            demo_voice_key(btn, ev);
        }
        break;
    }
    bsp_lvgl_unlock();
}

void app_main(void)
{
    ESP_LOGI(TAG, "FoloToy AI Passport 启动");
    esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();
    if (wakeup != ESP_SLEEP_WAKEUP_UNDEFINED) {
        ESP_LOGI(TAG, "休眠唤醒原因: %d", wakeup);
    }

    bsp_i2c_init();
    bsp_i2c_scan();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败,无法启动。");
        return;
    }
    bsp_display_backlight(100);

    for (size_t i = 0; i < APP_COUNT; i++) s_ok[i] = true;
    s_ok[1] = (bsp_button_init(on_key, NULL) == ESP_OK);  // Button
    s_ok[2] = (bsp_audio_init() == ESP_OK);               // Audio
    s_ok[3] = (bsp_battery_init() == ESP_OK);             // Battery

    home_init();

    s_level = LV_HOME;
    if (bsp_lvgl_lock(1000)) { home_show(); bsp_lvgl_unlock(); }

    ESP_LOGI(TAG, "就绪:Button=%d Audio=%d Battery=%d", s_ok[1], s_ok[2], s_ok[3]);
}
