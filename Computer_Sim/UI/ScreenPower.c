#include "ScreenPower.h"

#include "FreeRTOS.h"
#include "task.h"
#include "lvgl.h"
#include "main.h"
#include "usbh_usr.h"

#define SCREEN_POWER_MONITOR_PERIOD_MS 50U

typedef enum {
    SCREEN_POWER_ON = 0,
    SCREEN_POWER_OFF
} ScreenPowerState;

static ScreenPowerState screen_power_state = SCREEN_POWER_ON;
static lv_obj_t * screen_off_cover;
static lv_timer_t * monitor_timer;
static TickType_t absence_started_at;
static TickType_t screen_off_timeout_ticks;
static bool absence_timing;

static void screen_power_monitor_cb(lv_timer_t * timer)
{
    (void)timer;
    ScreenPower_Process(ScreenPower_ControlDeviceConnected());
}

__weak bool ScreenPower_ControlDeviceConnected(void)
{
    return bDeviceState != 0U;
}

__weak void ScreenPower_BacklightSet(bool enabled)
{
    (void)enabled;
}

void ScreenPower_Init(uint32_t timeout_ms)
{
    if(monitor_timer == NULL) {
        screen_power_state = SCREEN_POWER_ON;
        screen_off_cover = NULL;
        absence_started_at = 0;
        absence_timing = false;
        monitor_timer = lv_timer_create(screen_power_monitor_cb,
                                        SCREEN_POWER_MONITOR_PERIOD_MS,
                                        NULL);
    }

    ScreenPower_SetTimeout(timeout_ms);
}

void ScreenPower_SetTimeout(uint32_t timeout_ms)
{
    screen_off_timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    if(timeout_ms != 0U && screen_off_timeout_ticks == 0U) {
        screen_off_timeout_ticks = 1U;
    }

    absence_timing = false;
}

void ScreenPower_Sleep(void)
{
    if(screen_power_state == SCREEN_POWER_OFF) {
        return;
    }

    screen_off_cover = lv_obj_create(lv_layer_top());
    lv_obj_set_size(screen_off_cover, LV_PCT(100), LV_PCT(100));
    lv_obj_center(screen_off_cover);
    lv_obj_set_style_bg_color(screen_off_cover, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen_off_cover, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_off_cover, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(screen_off_cover, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen_off_cover, 0, LV_PART_MAIN);
    lv_obj_remove_flag(screen_off_cover, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(screen_off_cover, LV_OBJ_FLAG_CLICKABLE);

    /* Draw black before an optional backlight GPIO is switched off. */
    lv_refr_now(NULL);
    ScreenPower_BacklightSet(false);
    screen_power_state = SCREEN_POWER_OFF;
}

void ScreenPower_Wake(void)
{
    if(screen_power_state == SCREEN_POWER_ON) {
        absence_timing = false;
        return;
    }

    if(screen_off_cover != NULL) {
        lv_obj_delete(screen_off_cover);
        screen_off_cover = NULL;
    }

    /* Refresh the restored UI before an optional backlight is enabled. */
    lv_refr_now(NULL);
    ScreenPower_BacklightSet(true);
    screen_power_state = SCREEN_POWER_ON;
    absence_timing = false;
}

void ScreenPower_Process(bool control_device_connected)
{
    TickType_t now = xTaskGetTickCount();

    if(control_device_connected) {
        absence_timing = false;

        if(screen_power_state == SCREEN_POWER_OFF) {
            ScreenPower_Wake();
        }
        return;
    }

    if(screen_off_timeout_ticks == 0U || screen_power_state == SCREEN_POWER_OFF) {
        return;
    }

    if(!absence_timing) {
        absence_started_at = now;
        absence_timing = true;
        return;
    }

    if((TickType_t)(now - absence_started_at) >= screen_off_timeout_ticks) {
        ScreenPower_Sleep();
    }
}

bool ScreenPower_IsSleeping(void)
{
    return screen_power_state == SCREEN_POWER_OFF;
}
