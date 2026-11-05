#ifndef __DRAWING_APP_H
#define __DRAWING_APP_H

#include "lvgl.h"

#include <stdbool.h>

bool DrawingApp_Create(lv_obj_t * parent);
bool DrawingApp_CreateFromFile(lv_obj_t * parent, const char * path);

#endif
