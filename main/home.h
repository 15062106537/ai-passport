// main/home.h —— 首页(身份卡风格):两个大入口,像素风。
#pragma once

#include "bsp_button.h"
#include "lvgl.h"

// 首页入口
typedef enum { HOME_APPS = 0, HOME_VOICE } home_entry_t;

// 创建首页屏幕(在 app_main 里、外设初始化后调用一次)
void home_init(void);

// 显示首页
void home_show(void);

// 首页按键处理(UP/DOWN 切换入口),返回 true 表示已消费
bool home_key(bsp_btn_t btn, bsp_btn_ev_t ev);

// 当前选中的入口
home_entry_t home_get_selected(void);
