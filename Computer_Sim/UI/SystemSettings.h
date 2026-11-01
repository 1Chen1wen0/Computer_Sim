#ifndef __SYSTEM_SETTINGS_H
#define __SYSTEM_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#define SYSTEM_SETTINGS_MOUSE_MIN       1U
#define SYSTEM_SETTINGS_MOUSE_MAX       10U
#define SYSTEM_SETTINGS_BRIGHTNESS_MIN  10U
#define SYSTEM_SETTINGS_BRIGHTNESS_MAX  100U
#define SYSTEM_SETTINGS_VOLUME_MIN      0U
#define SYSTEM_SETTINGS_VOLUME_MAX      100U

typedef struct {
    uint8_t mouse_sensitivity;
    uint8_t brightness;
    uint8_t volume;
} SystemSettingsValues;

void SystemSettings_Init(void);
const SystemSettingsValues * SystemSettings_Get(void);
bool SystemSettings_SaveAndApply(const SystemSettingsValues * values);
int32_t SystemSettings_ScaleMouseDelta(int32_t delta);

#endif
