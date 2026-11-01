#ifndef __DESKTOP_UI_H
#define __DESKTOP_UI_H

#include "lvgl.h"

#include <stdbool.h>

void DesktopUI_Create(void);
void DesktopUI_Destroy(void);
bool DesktopUI_IsCreated(void);
lv_obj_t * DesktopUI_GetRoot(void);

lv_obj_t * DesktopUI_AddShortcut(const char * symbol,
                                 const char * label,
                                 int32_t x,
                                 int32_t y,
                                 lv_event_cb_t event_cb,
                                 void * user_data);

#endif
