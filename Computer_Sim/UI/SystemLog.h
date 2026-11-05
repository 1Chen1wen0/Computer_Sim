#ifndef SYSTEM_LOG_H
#define SYSTEM_LOG_H

#include <stdbool.h>
#include <stdint.h>

#define SYSTEM_LOG_CAPACITY 64U
#define SYSTEM_LOG_PATH_MAX 256U

typedef enum {
    SYSTEM_LOG_OPEN_REQUEST,
    SYSTEM_LOG_OPEN,
    SYSTEM_LOG_CREATE_FILE,
    SYSTEM_LOG_CREATE_FOLDER,
    SYSTEM_LOG_SAVE,
    SYSTEM_LOG_PLAY_REQUEST,
    SYSTEM_LOG_PLAY_STARTED,
    SYSTEM_LOG_PLAY_FINISHED,
    SYSTEM_LOG_PLAY_STOPPED,
    SYSTEM_LOG_PLAY_REPLACED,
    SYSTEM_LOG_PLAY_ERROR
} SystemLogAction;

/* 0 is success; 1..19 are FatFs results. Audio errors retain 0x80..0x83. */
#define SYSTEM_LOG_ERROR_MEMORY      0x100U
#define SYSTEM_LOG_ERROR_TOO_LARGE   0x101U
#define SYSTEM_LOG_ERROR_UI          0x102U
#define SYSTEM_LOG_ERROR_UNAVAILABLE 0x103U
#define SYSTEM_LOG_ERROR_QUEUE       0x104U
#define SYSTEM_LOG_ERROR_PLAYLIST    0x105U

typedef struct {
    uint32_t seconds;
    SystemLogAction action;
    uint16_t result;
    char path[SYSTEM_LOG_PATH_MAX];
} SystemLogEntry;

/* Call once after external SRAM and its allocator are initialized, before
 * starting tasks. Other APIs are task-context only; never call from an ISR. */
bool SystemLog_Init(void);
bool SystemLog_IsReady(void);
void SystemLog_Write(SystemLogAction action, const char * path, uint16_t result);
void SystemLog_Clear(void);
uint32_t SystemLog_GetVersion(void);
uint16_t SystemLog_GetCount(void);
/* Index 0 is the newest entry. Returns a protected copy, not a live pointer. */
bool SystemLog_Read(uint16_t newest_index, SystemLogEntry * entry);
const char * SystemLog_ActionText(SystemLogAction action);
const char * SystemLog_ResultText(uint16_t result);

#endif
