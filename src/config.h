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

// ── USB-C 接入检测 (Tab5: 充电芯片的状态线挂在 IO 扩展器 #2 的 P6) ──
// 用途：插着线时整条省电链路停摆（不降亮度/不灭屏），见 main.cpp 的 idle 状态机。
// P6 是 charger 的输入脚、没有 VBUS 分压，所以它是"插线/在充"而不是"5V 存在"的证据；
// 三个来源对它的叫法不一（BSP 叫 USB_C_DET、M5Unified 注释叫 CHG_STAT、ESPHome 板级
// 定义叫 charging status），但一致认为它由充电芯片驱动 —— 真实极性靠开机那行日志定。
static const int USB_DET_EXPANDER = 1;      // PI4IOE @0x44 (M5Unified 的索引)
static const int USB_DET_PIN      = 6;      // IN_STA(0x0F) bit 6
static const int USB_DET_POLL_MS  = 500;    // 采样周期（不是每帧）
static const int USB_DET_SAMPLES  = 2;      // 连续 N 次同值才改状态（滤波）
static const int USB_DET_DISCHARGE_MA = 40; // 电包放电超过这个值(mA)才认为"没市电"

// ── 充电使能 (扩展器 #2 @0x44) ──
// 厂商 BSP/demo 的口径：P7=CHG_EN(高=使能充电)、P5=QC_EN(低=使能快充)
static const int CHG_IOEXPANDER = 1;
static const int CHG_EN_PIN     = 7;
static const int CHG_QC_PIN     = 5;

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
