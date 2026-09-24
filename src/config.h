// ============================================================
// config.h — CYBER CLOCK M5Stack Tab5 配置参数
// ============================================================
#pragma once

// ── 显示 (Tab5: 1280×720 MIPI DSI) ──
static const int SCREEN_W = 1280;
static const int SCREEN_H = 720;
static const int SCREEN_ROTATION = 3;  // 横屏 1280×720

// ── 亮度档位 (7级, Tab5 背光 GPIO22 PWM) ──
static const int BRIGHTNESS_DEFAULT_IDX = 4;  // 默认50%
static const uint8_t BRIGHTNESS_LEVELS[] = { 3, 13, 26, 76, 128, 204, 255 };
static const uint8_t BRIGHTNESS_PCT[]   = { 1,  5, 10, 30,  50,  80, 100 };
static const int BRIGHTNESS_N = sizeof(BRIGHTNESS_LEVELS) / sizeof(BRIGHTNESS_LEVELS[0]);

// ── 省电 (Tab5 有 BMI270 中断唤醒 + RX8130 RTC) ──
static const int IDLE_POWERSAVE_MS  =  60000;   // 60s → 降频
static const int IDLE_SLEEP_MS      = 180000;   // 180s → Light Sleep
static const int SLEEP_POLL_US      = 200000;

// ── 矩阵雨 (列式, 160列 × 16字符) ──
static const char MATRIX_CHARS[] = "01#*+:.ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz<>/|~@%&$";

// ── IMU ──
static const float IMU_TAU = 1.0f;

// ── NTP ──
static const int NTP_RETRIES = 20;
static const int NTP_RETRY_MS = 250;

// ── Glitch ──
static const int GLITCH_INTERVAL_MIN = 800;
static const int GLITCH_INTERVAL_MAX = 2000;
static const int GLITCH_DUR_MIN     = 60;
static const int GLITCH_DUR_MAX     = 80;

// ── WiFi SDIO 管脚 (Tab5: ESP32-P4 ↔ ESP32-C6) ──
#define TAB5_SDIO_CLK 12
#define TAB5_SDIO_CMD 13
#define TAB5_SDIO_D0  11
#define TAB5_SDIO_D1  10
#define TAB5_SDIO_D2  9
#define TAB5_SDIO_D3  8
#define TAB5_SDIO_RST 15

// ── 触控 ──
static const int SWIPE_THRESHOLD   = 80;    // 像素
