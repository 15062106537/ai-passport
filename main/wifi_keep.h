// main/wifi_keep.h —— Wi-Fi 常驻管理:任一应用第一次用网时连接,之后一直在线。
// 掉线自动重连;退出任何界面都不断开(要省电可后续加电源管理)。
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

esp_err_t wifi_keep_start(void);            // 幂等:已启动直接返回;断线时触发重连
bool       wifi_keep_is_up(void);           // 拿到 IP = true
esp_err_t  wifi_keep_wait_ip(uint32_t ms); // 阻塞等 IP(已在线立即返回)
void       wifi_keep_get_ip(char *buf, size_t len);   // 未连接时填 "--"
