#ifndef __SCREEN_POWER_H
#define __SCREEN_POWER_H

#include <stdbool.h>
#include <stdint.h>

#define SCREEN_POWER_DEFAULT_TIMEOUT_MS 60000U

void ScreenPower_Init(uint32_t timeout_ms);
void ScreenPower_Process(bool control_device_connected);
void ScreenPower_Sleep(void);
void ScreenPower_Wake(void);
void ScreenPower_SetTimeout(uint32_t timeout_ms);
bool ScreenPower_IsSleeping(void);

/* Override these weak hooks when the board-specific behavior differs. */
bool ScreenPower_ControlDeviceConnected(void);
void ScreenPower_BacklightSet(bool enabled);

#endif
