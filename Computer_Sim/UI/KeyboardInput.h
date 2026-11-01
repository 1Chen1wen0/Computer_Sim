#ifndef __KEYBOARD_INPUT_H
#define __KEYBOARD_INPUT_H

#include <stdint.h>

#define KEYBOARD_INPUT_MAX_LEN 63U

void KeyboardInput_Init(void);
void KeyboardInput_ProcessKey(uint8_t data);
void KeyboardInput_Clear(void);
uint32_t KeyboardInput_GetKey(void);

#endif
