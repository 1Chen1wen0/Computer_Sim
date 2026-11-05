#include "SystemLog.h"

#include "FreeRTOS.h"
#include "task.h"
#include "malloc.h"

#include <string.h>

/* Keep the large ring in external SRAM; internal SRAM is nearly full. */
typedef struct {
    uint16_t next;
    uint16_t count;
    uint32_t version;
    SystemLogEntry entries[SYSTEM_LOG_CAPACITY];
} SystemLogStorage;
static SystemLogStorage * log_storage;

bool SystemLog_Init(void)
{
    if(log_storage != NULL) return true;
    log_storage = (SystemLogStorage *)mymalloc(SRAMEX, sizeof(SystemLogStorage));
    if(log_storage == NULL) return false;
    log_storage->next = 0U;
    log_storage->count = 0U;
    log_storage->version = 0U;
    return true;
}

bool SystemLog_IsReady(void)
{
    return log_storage != NULL;
}

void SystemLog_Write(SystemLogAction action, const char * path, uint16_t result)
{
    SystemLogEntry * entry;
    size_t length = 0U;

    if(log_storage == NULL) return;
    if(path != NULL) {
        while(length < SYSTEM_LOG_PATH_MAX - 1U && path[length] != '\0') length++;
    }

    /* No allocation, storage I/O, or UI work inside this bounded section. */
    taskENTER_CRITICAL();
    entry = &log_storage->entries[log_storage->next];
    entry->seconds = (uint32_t)(xTaskGetTickCount() / configTICK_RATE_HZ);
    entry->action = action;
    entry->result = result;
    if(length != 0U) memcpy(entry->path, path, length);
    entry->path[length] = '\0';
    log_storage->next = (uint16_t)((log_storage->next + 1U) % SYSTEM_LOG_CAPACITY);
    if(log_storage->count < SYSTEM_LOG_CAPACITY) log_storage->count++;
    log_storage->version++;
    taskEXIT_CRITICAL();
}

void SystemLog_Clear(void)
{
    if(log_storage == NULL) return;
    taskENTER_CRITICAL();
    log_storage->next = 0U;
    log_storage->count = 0U;
    log_storage->version++;
    taskEXIT_CRITICAL();
}

uint32_t SystemLog_GetVersion(void)
{
    uint32_t version;
    if(log_storage == NULL) return 0U;
    taskENTER_CRITICAL();
    version = log_storage->version;
    taskEXIT_CRITICAL();
    return version;
}

uint16_t SystemLog_GetCount(void)
{
    uint16_t count;
    if(log_storage == NULL) return 0U;
    taskENTER_CRITICAL();
    count = log_storage->count;
    taskEXIT_CRITICAL();
    return count;
}

bool SystemLog_Read(uint16_t newest_index, SystemLogEntry * entry)
{
    bool found = false;
    if(entry == NULL || log_storage == NULL) return false;
    taskENTER_CRITICAL();
    if(newest_index < log_storage->count) {
        uint16_t index = (uint16_t)((log_storage->next + SYSTEM_LOG_CAPACITY - 1U -
                                     newest_index) % SYSTEM_LOG_CAPACITY);
        *entry = log_storage->entries[index];
        found = true;
    }
    taskEXIT_CRITICAL();
    return found;
}

const char * SystemLog_ActionText(SystemLogAction action)
{
    switch(action) {
        case SYSTEM_LOG_OPEN_REQUEST: return "Open request";
        case SYSTEM_LOG_OPEN: return "Open";
        case SYSTEM_LOG_CREATE_FILE: return "Create file";
        case SYSTEM_LOG_CREATE_FOLDER: return "Create folder";
        case SYSTEM_LOG_SAVE: return "Save";
        case SYSTEM_LOG_PLAY_REQUEST: return "Play request";
        case SYSTEM_LOG_PLAY_STARTED: return "Playing";
        case SYSTEM_LOG_PLAY_FINISHED: return "Finished";
        case SYSTEM_LOG_PLAY_STOPPED: return "Stopped";
        case SYSTEM_LOG_PLAY_REPLACED: return "Track replaced";
        case SYSTEM_LOG_PLAY_ERROR: return "Playback error";
        default: return "File operation";
    }
}

const char * SystemLog_ResultText(uint16_t result)
{
    switch(result) {
        case 0U: return "OK";
        case 1U: return "Disk I/O error";
        case 2U: return "Internal error";
        case 3U: return "Drive not ready";
        case 4U: return "File not found";
        case 5U: return "Path not found";
        case 6U: return "Invalid name";
        case 7U: return "Access denied / disk full";
        case 8U: return "Already exists";
        case 9U: return "Invalid object / file format";
        case 10U: return "Write protected";
        case 11U: return "Invalid drive";
        case 12U: return "Drive not enabled";
        case 13U: return "No filesystem";
        case 14U: return "Format aborted";
        case 15U: return "Timeout";
        case 16U: return "File locked";
        case 17U: return "Not enough memory";
        case 18U: return "Too many open files";
        case 19U: return "Invalid parameter / format";
        case 0x80U: return "Audio buffer error";
        case 0x81U: return "Invalid / incomplete audio data";
        case 0x82U: return "Audio DMA error";
        case 0x83U: return "Audio file error";
        case SYSTEM_LOG_ERROR_MEMORY: return "Not enough memory";
        case SYSTEM_LOG_ERROR_TOO_LARGE: return "Text file exceeds 8 KB";
        case SYSTEM_LOG_ERROR_UI: return "Unable to open page";
        case SYSTEM_LOG_ERROR_UNAVAILABLE: return "Audio task unavailable";
        case SYSTEM_LOG_ERROR_QUEUE: return "Audio command rejected";
        case SYSTEM_LOG_ERROR_PLAYLIST: return "File outside playlist limit";
        default: return "Operation failed";
    }
}
