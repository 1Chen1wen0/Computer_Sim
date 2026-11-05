#include "SystemLog.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static CRITICAL_SECTION lock;
static int fail_allocation = 1;
void test_enter(void) { EnterCriticalSection(&lock); }
void test_exit(void) { LeaveCriticalSection(&lock); }
uint32_t xTaskGetTickCount(void) { return 12345000U; }
void *mymalloc(uint8_t bank, uint32_t size)
{
    assert(bank == 1U);
    return fail_allocation ? NULL : malloc(size);
}

static DWORD WINAPI producer(LPVOID argument)
{
    unsigned int i;
    uint16_t code = (uint16_t)(uintptr_t)argument;
    const char *path = code == 1U ? "0:/thread1.txt" : "0:/thread2.txt";
    for(i = 0; i < 10000U; i++) SystemLog_Write(SYSTEM_LOG_SAVE, path, code);
    return 0;
}

int main(void)
{
    unsigned int i;
    uint32_t version;
    SystemLogEntry entry;
    char path[300];
    HANDLE threads[2];
    InitializeCriticalSection(&lock);
    assert(!SystemLog_Init());
    SystemLog_Write(SYSTEM_LOG_OPEN, "ignored", 0U);
    assert(SystemLog_GetCount() == 0U);
    assert(!SystemLog_Read(0U, &entry));
    fail_allocation = 0;
    assert(SystemLog_Init() && SystemLog_Init());
    for(i = 0; i < 70U; i++) {
        snprintf(path, sizeof(path), "0:/file%u.txt", i);
        SystemLog_Write(SYSTEM_LOG_OPEN, path, (uint16_t)i);
    }
    assert(SystemLog_GetCount() == 64U);
    assert(SystemLog_Read(0U, &entry) && entry.result == 69U);
    assert(strcmp(entry.path, "0:/file69.txt") == 0 && entry.seconds == 12345U);
    assert(SystemLog_Read(63U, &entry) && entry.result == 6U);
    assert(!SystemLog_Read(64U, &entry) && !SystemLog_Read(0U, NULL));
    version = SystemLog_GetVersion();
    SystemLog_Clear();
    assert(SystemLog_GetVersion() == version + 1U);
    assert(SystemLog_GetCount() == 0U && !SystemLog_Read(0U, &entry));
    memset(path, 'A', sizeof(path));
    SystemLog_Write(SYSTEM_LOG_SAVE, path, 0U);
    memset(path, 'B', sizeof(path));
    assert(SystemLog_Read(0U, &entry) && strlen(entry.path) == 255U);
    assert(entry.path[0] == 'A');
    SystemLog_Write(SYSTEM_LOG_OPEN, NULL, 0U);
    assert(SystemLog_Read(0U, &entry) && entry.path[0] == '\0');
    SystemLog_Clear();
    version = SystemLog_GetVersion();
    threads[0] = CreateThread(NULL, 0, producer, (void *)(uintptr_t)1U, 0, NULL);
    threads[1] = CreateThread(NULL, 0, producer, (void *)(uintptr_t)2U, 0, NULL);
    assert(threads[0] && threads[1]);
    for(i = 0; i < 10000U; i++) {
        if(SystemLog_Read(0U, &entry)) {
            assert(entry.result == 1U || entry.result == 2U);
            assert(strcmp(entry.path, entry.result == 1U
                          ? "0:/thread1.txt" : "0:/thread2.txt") == 0);
        }
    }
    assert(WaitForMultipleObjects(2, threads, TRUE, 10000) == WAIT_OBJECT_0);
    CloseHandle(threads[0]);
    CloseHandle(threads[1]);
    assert(SystemLog_GetVersion() == version + 20000U);
    assert(SystemLog_GetCount() == 64U);
    DeleteCriticalSection(&lock);
    puts("PASS: allocation failure, wrap/overwrite, clear, bounded path copy, concurrent writers/readers");
    return 0;
}
