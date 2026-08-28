// main/main.c —— 手机式桌面启动器:初始化外设 + 应用注册 + 三键分发。
//
// 按键语义(全局统一):
//   桌面: 上/下 短按 = 移动图标焦点; 确定 短按 = 进入应用
//   应用: 确定 长按 = 返回桌面; 其余按键转发给当前应用
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_pins.h"
#include "demo.h"
#include "launcher.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_sleep.h"

static const char *TAG = "main";

// 应用表(桌面图标顺序)。新增应用 = 实现 enter/exit/key 后在这里加一行。
static const app_entry_t APPS[] = {
    { "Display",  "D", 0xE43B2F, demo_display_enter,     demo_display_exit,     demo_display_key     },
    { "Button",   "B", 0xFFB23E, demo_button_enter,      demo_button_exit,      demo_button_key      },
    { "Audio",    "A", 0x7557D9, demo_audio_enter,       demo_audio_exit,       demo_audio_key       },
    { "Battery",  "%", 0x82BE2D, demo_battery_enter,     demo_battery_exit,     demo_battery_key     },
    { "Wi-Fi",    "W", 0x1689E8, demo_wifi_enter,        demo_wifi_exit,        demo_wifi_key        },
    { "BLE",      "L", 0x00BCD4, demo_ble_enter,         demo_ble_exit,         demo_ble_key         },
    { "Sleep",    "Z", 0x0872C9, demo_low_power_enter,   demo_low_power_exit,   demo_low_power_key   },
    { "Voice AI", "V", 0xFFD928, demo_voice_enter,       demo_voice_exit,       demo_voice_key       },
    { "Dice",     "6", 0xF57C00, demo_dice_enter,         demo_dice_exit,         demo_dice_key         },
    { "Reaction", "R", 0xC62828, demo_reaction_enter,     demo_reaction_exit,     demo_reaction_key     },
    { "Simon",    "S", 0x9C27B0, demo_simon_enter,        demo_simon_exit,        demo_simon_key        },
};
#define APP_COUNT (sizeof(APPS) / sizeof(APPS[0]))

static bool s_ok[APP_COUNT];
static int  s_active = -1;   // -1 = 在桌面

// 按键回调运行在 button 组件任务里,操作 LVGL 必须加锁。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    if (!bsp_lvgl_lock(500)) return;

    if (s_active >= 0) {
        if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {   // 统一返回桌面
            APPS[s_active].exit();
            s_active = -1;
            launcher_show();
        } else {
            APPS[s_active].key(btn, ev);
        }
    } else {
        if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
            int sel = launcher_get_selected();
            if (sel >= 0 && sel < (int)APP_COUNT && s_ok[sel]) {
                s_active = sel;
                APPS[sel].enter();
            }
        } else {
            launcher_key(btn, ev);
        }
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

    // 屏幕是 UI 的硬依赖,失败就没有桌面可言。
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败,无法启动。");
        return;
    }
    bsp_display_backlight(100);

    // 各外设可用性:失败的应用图标灰显、不可进入。
    for (size_t i = 0; i < APP_COUNT; i++) s_ok[i] = true;
    s_ok[1] = (bsp_button_init(on_key, NULL) == ESP_OK);  // Button
    s_ok[2] = (bsp_audio_init() == ESP_OK);               // Audio
    s_ok[3] = (bsp_battery_init() == ESP_OK);             // Battery

    launcher_init(APPS, APP_COUNT);
    for (size_t i = 0; i < APP_COUNT; i++) launcher_set_available(i, s_ok[i]);
    if (bsp_lvgl_lock(1000)) { launcher_show(); bsp_lvgl_unlock(); }

    ESP_LOGI(TAG, "就绪:Button=%d Audio=%d Battery=%d", s_ok[1], s_ok[2], s_ok[3]);
}
