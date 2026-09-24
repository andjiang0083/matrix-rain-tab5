// sdkconfig.h — 覆盖 Arduino libs 的默认配置
// 这个文件放在 include 路径优先位置，让 M5GFX 看到正确的 PSRAM 速度
// 原始文件: framework-arduinoespressif32-libs/esp32p4/qio_qspi/include/sdkconfig.h

// 先包含原始的
#include_next "sdkconfig.h"

// 然后覆盖 PSRAM 速度为 200MHz (Tab5 需要)
#undef CONFIG_SPIRAM_SPEED
#define CONFIG_SPIRAM_SPEED 200

// 其他必要的覆盖
