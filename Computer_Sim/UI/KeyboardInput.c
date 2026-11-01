#include "KeyboardInput.h"

#include "FreeRTOS.h"
#include "queue.h"

#define KEYBOARD_QUEUE_LENGTH 16U

static QueueHandle_t keyboard_queue;
static StaticQueue_t keyboard_queue_control;
static uint8_t keyboard_queue_storage[KEYBOARD_QUEUE_LENGTH * sizeof(uint32_t)];

void KeyboardInput_Init(void)
{
    if(keyboard_queue == NULL) {
        keyboard_queue = xQueueCreateStatic(KEYBOARD_QUEUE_LENGTH,
                                            sizeof(uint32_t),
                                            keyboard_queue_storage,
                                            &keyboard_queue_control);
    }
}

void KeyboardInput_ProcessKey(uint8_t data)
{
    uint32_t key = data;

    if(keyboard_queue == NULL || data == 0U) {
        return;
    }

    (void)xQueueSend(keyboard_queue, &key, 0U);
}

void KeyboardInput_Clear(void)
{
    if(keyboard_queue != NULL) {
        (void)xQueueReset(keyboard_queue);
    }
}

uint32_t KeyboardInput_GetKey(void)
{
    uint32_t key;

    if(keyboard_queue != NULL &&
       xQueueReceive(keyboard_queue, &key, 0U) == pdPASS) {
        return key;
    }

    return 0U;
}
