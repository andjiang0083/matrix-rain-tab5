// sdkconfig_override.h — 强制覆盖 sdkconfig.h 中的 PSRAM 配置
// 因为 sdkconfig.h 在 build_flags 之后才被包含，会覆盖 -D 参数
// 所以用 -include 强制在后置阶段重新定义

#ifdef CONFIG_SPIRAM_SPEED
#undef CONFIG_SPIRAM_SPEED
#define CONFIG_SPIRAM_SPEED 200
#endif
