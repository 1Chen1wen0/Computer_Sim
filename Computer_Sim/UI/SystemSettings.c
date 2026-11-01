#include "SystemSettings.h"

#include "ScreenPower.h"
#include "lcd.h"
#include "w25qxx.h"
#include "wm8978.h"

#include <string.h>

#define SYSTEM_SETTINGS_MAGIC          0x53544731UL
#define SYSTEM_SETTINGS_VERSION        1U
#define SYSTEM_SETTINGS_SECTOR_SIZE    4096UL
#define SYSTEM_SETTINGS_DEFAULT_MOUSE  5U
#define SYSTEM_SETTINGS_DEFAULT_LIGHT  80U
#define SYSTEM_SETTINGS_DEFAULT_VOLUME 50U

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t mouse_sensitivity;
    uint8_t brightness;
    uint8_t volume;
    uint8_t reserved[3];
    uint32_t checksum;
} SystemSettingsRecord;

static SystemSettingsValues current_settings = {
    SYSTEM_SETTINGS_DEFAULT_MOUSE,
    SYSTEM_SETTINGS_DEFAULT_LIGHT,
    SYSTEM_SETTINGS_DEFAULT_VOLUME
};
static bool storage_ready;

static uint32_t system_settings_checksum(const SystemSettingsRecord * record)
{
    uint32_t value = 2166136261UL;

    value = (value ^ record->magic) * 16777619UL;
    value = (value ^ record->version) * 16777619UL;
    value = (value ^ record->mouse_sensitivity) * 16777619UL;
    value = (value ^ record->brightness) * 16777619UL;
    value = (value ^ record->volume) * 16777619UL;
    return value;
}

static bool system_settings_values_valid(const SystemSettingsValues * values)
{
    return values != NULL &&
           values->mouse_sensitivity >= SYSTEM_SETTINGS_MOUSE_MIN &&
           values->mouse_sensitivity <= SYSTEM_SETTINGS_MOUSE_MAX &&
           values->brightness >= SYSTEM_SETTINGS_BRIGHTNESS_MIN &&
           values->brightness <= SYSTEM_SETTINGS_BRIGHTNESS_MAX &&
           values->volume <= SYSTEM_SETTINGS_VOLUME_MAX;
}

static bool system_settings_record_valid(const SystemSettingsRecord * record)
{
    SystemSettingsValues values;

    values.mouse_sensitivity = record->mouse_sensitivity;
    values.brightness = record->brightness;
    values.volume = record->volume;
    return record->magic == SYSTEM_SETTINGS_MAGIC &&
           record->version == SYSTEM_SETTINGS_VERSION &&
           system_settings_values_valid(&values) &&
           record->checksum == system_settings_checksum(record);
}

static uint32_t system_settings_storage_address(void)
{
    return W25QXX_GetCapacity() - SYSTEM_SETTINGS_SECTOR_SIZE;
}

static void system_settings_apply_hardware(void)
{
    uint8_t codec_volume;

    if(lcddev.id == 0X1963U) {
        LCD_SSD_BackLightSet(current_settings.brightness);
    }
    else {
        LCD_LED = current_settings.brightness == 0U ? 0U : 1U;
    }

    codec_volume = (uint8_t)(((uint32_t)current_settings.volume * 63UL + 50UL) / 100UL);
    WM8978_HPvol_Set(codec_volume, codec_volume);
    WM8978_SPKvol_Set(codec_volume);
}

void SystemSettings_Init(void)
{
    SystemSettingsRecord record;

    W25QXX_Init();
    storage_ready = W25QXX_GetCapacity() >= SYSTEM_SETTINGS_SECTOR_SIZE;
    if(storage_ready) {
        W25QXX_Read((uint8_t *)&record,
                    system_settings_storage_address(),
                    (uint16_t)sizeof(record));
        if(system_settings_record_valid(&record)) {
            current_settings.mouse_sensitivity = record.mouse_sensitivity;
            current_settings.brightness = record.brightness;
            current_settings.volume = record.volume;
        }
    }

    system_settings_apply_hardware();
}

const SystemSettingsValues * SystemSettings_Get(void)
{
    return &current_settings;
}

bool SystemSettings_SaveAndApply(const SystemSettingsValues * values)
{
    SystemSettingsRecord record;
    SystemSettingsRecord verify;

    if(!storage_ready || !system_settings_values_valid(values)) {
        return false;
    }

    memset(&record, 0, sizeof(record));
    record.magic = SYSTEM_SETTINGS_MAGIC;
    record.version = SYSTEM_SETTINGS_VERSION;
    record.mouse_sensitivity = values->mouse_sensitivity;
    record.brightness = values->brightness;
    record.volume = values->volume;
    record.checksum = system_settings_checksum(&record);

    W25QXX_Write((uint8_t *)&record,
                 system_settings_storage_address(),
                 (uint16_t)sizeof(record));
    W25QXX_Read((uint8_t *)&verify,
                system_settings_storage_address(),
                (uint16_t)sizeof(verify));
    if(memcmp(&record, &verify, sizeof(record)) != 0 ||
       !system_settings_record_valid(&verify)) {
        return false;
    }

    current_settings = *values;
    system_settings_apply_hardware();
    return true;
}

int32_t SystemSettings_ScaleMouseDelta(int32_t delta)
{
    int32_t scaled;

    if(delta == 0) return 0;
    scaled = (delta * (int32_t)current_settings.mouse_sensitivity) / 5;
    if(scaled == 0) return delta > 0 ? 1 : -1;
    return scaled;
}

void ScreenPower_BacklightSet(bool enabled)
{
    if(lcddev.id == 0X1963U) {
        LCD_SSD_BackLightSet(enabled ? current_settings.brightness : 0U);
    }
    else {
        LCD_LED = enabled ? 1U : 0U;
    }
}
