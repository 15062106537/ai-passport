// main/home.h —— 首页:身份识别卡卡包,上下翻卡,像素风。
#pragma once

#include "bsp_button.h"
#include "lvgl.h"

// 首页卡片
typedef enum {
    HOME_CARD_STUDENT = 0,   // 学生卡
    HOME_CARD_DEV,           // 开发者卡
    HOME_CARD_EVENT,         // 活动胸牌
    HOME_CARD_APPS,          // 功能卡:进入应用集合
    HOME_CARD_VOICE,         // 功能卡:进入语音助手
    HOME_CARD_COUNT
} home_card_t;

// 创建首页屏幕(在 app_main 里、外设初始化后调用一次)
void home_init(void);

// 显示首页
void home_show(void);

// 首页按键处理(UP/DOWN 翻卡;信息卡按 OK 仅跳动提示),返回 true 表示已消费
bool home_key(bsp_btn_t btn, bsp_btn_ev_t ev);

// 当前选中的卡片
home_card_t home_get_selected(void);

// 最后浏览的身份卡(学生/开发者/活动),语音助手按此身份应答
home_card_t home_get_identity(void);
