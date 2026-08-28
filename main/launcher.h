// main/launcher.h —— 手机式"桌面 + 应用"框架。
// 把每个功能抽象成一个 app_entry_t:桌面显示为一个彩色图标,
// 三个物理键负责:UP/DOWN 移动焦点、OK 进入应用、长按 OK 返回桌面。
#pragma once

#include "bsp_button.h"
#include "lvgl.h"
#include <stddef.h>
#include <stdint.h>

// 一个"应用" = 桌面上的一个图标 + 三个生命周期回调。
typedef struct {
    const char *name;    // 应用名(屏上显示,英文)
    const char *glyph;   // 图标字符(单个,montserrat 字体支持)
    uint32_t    color;   // 图标底色
    void (*enter)(void);
    void (*exit)(void);
    void (*key)(bsp_btn_t btn, bsp_btn_ev_t ev);
} app_entry_t;

// 初始化桌面(创建屏幕、状态栏与图标网格)。在 app_main 里、外设初始化后调用。
void launcher_init(const app_entry_t *apps, size_t count);

// 标记某个应用是否可用(外设初始化失败时置 false,图标灰显且不可进入)。
void launcher_set_available(size_t index, bool avail);

// 加载并显示桌面(返回首页时调用)。
void launcher_show(void);

// 桌面按键处理(UP/DOWN 移动焦点)。返回 true 表示已消费该按键。
bool launcher_key(bsp_btn_t btn, bsp_btn_ev_t ev);

// 返回当前焦点应用下标。
int  launcher_get_selected(void);
