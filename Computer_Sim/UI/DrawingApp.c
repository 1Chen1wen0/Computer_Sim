#include "DrawingApp.h"
#include "SystemLog.h"

#include "ff.h"
#include "malloc.h"

#include <stdint.h>
#include <string.h>

#define DRAWING_CANVAS_WIDTH 308
#define DRAWING_CANVAS_HEIGHT 344
#define DRAWING_CANVAS_X 6
#define DRAWING_CANVAS_Y 128
#define DRAWING_BRUSH_MIN 1
#define DRAWING_BRUSH_MAX 17
#define DRAWING_BRUSH_STEP 2
#define DRAWING_BMP_HEADER_SIZE 54U
#define DRAWING_BMP_ROW_SIZE (((DRAWING_CANVAS_WIDTH * 3U) + 3U) & ~3U)
#define DRAWING_PATH_MAX 256U

typedef enum {
    DRAWING_TOOL_PEN,
    DRAWING_TOOL_ERASER
} DrawingTool;

static lv_obj_t * drawing_root;
static lv_obj_t * drawing_canvas;
static lv_obj_t * drawing_status_label;
static lv_obj_t * drawing_width_label;
static lv_obj_t * drawing_pen_button;
static lv_obj_t * drawing_eraser_button;
static lv_obj_t * drawing_color_buttons[6];
static uint8_t * drawing_canvas_buffer;
static uint32_t drawing_canvas_stride;
static DrawingTool drawing_tool = DRAWING_TOOL_PEN;
static lv_color_t drawing_color;
static uint8_t drawing_brush_width = 5U;
static bool drawing_pointer_active;
static lv_point_t drawing_previous_point;
static char drawing_current_path[DRAWING_PATH_MAX];
static bool drawing_modified;

static const uint32_t drawing_palette[] = {
    0x17211D,
    0xD64545,
    0xF08A24,
    0x26885B,
    0x2878C7,
    0x7651B5
};

static void drawing_put_u16(uint8_t * destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
}

static void drawing_put_u32(uint8_t * destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
    destination[2] = (uint8_t)(value >> 16);
    destination[3] = (uint8_t)(value >> 24);
}

static uint16_t drawing_get_u16(const uint8_t * source)
{
    return (uint16_t)((uint16_t)source[0] |
                      ((uint16_t)source[1] << 8));
}

static uint32_t drawing_get_u32(const uint8_t * source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8) |
           ((uint32_t)source[2] << 16) |
           ((uint32_t)source[3] << 24);
}

static void drawing_set_status(const char * text, lv_color_t color)
{
    if(drawing_status_label == NULL) return;

    lv_label_set_text(drawing_status_label, text);
    lv_obj_set_style_text_color(drawing_status_label, color, LV_PART_MAIN);
}

static void drawing_update_tool_buttons(void)
{
    lv_color_t active_color = lv_color_hex(0x26885B);
    lv_color_t inactive_color = lv_color_hex(0x66756F);

    if(drawing_pen_button != NULL) {
        lv_obj_set_style_bg_color(drawing_pen_button,
                                  drawing_tool == DRAWING_TOOL_PEN ? active_color : inactive_color,
                                  LV_PART_MAIN);
    }
    if(drawing_eraser_button != NULL) {
        lv_obj_set_style_bg_color(drawing_eraser_button,
                                  drawing_tool == DRAWING_TOOL_ERASER ? active_color : inactive_color,
                                  LV_PART_MAIN);
    }
}

static void drawing_update_color_buttons(void)
{
    uint32_t selected = lv_color_to_int(drawing_color) & 0x00FFFFFFU;
    uint32_t index;

    for(index = 0U; index < 6U; index++) {
        bool is_selected = selected == drawing_palette[index];

        if(drawing_color_buttons[index] == NULL) continue;
        lv_obj_set_style_border_width(drawing_color_buttons[index],
                                      is_selected ? 3 : 1,
                                      LV_PART_MAIN);
        lv_obj_set_style_border_color(drawing_color_buttons[index],
                                      is_selected ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x82928C),
                                      LV_PART_MAIN);
    }
}

static void drawing_update_width_label(void)
{
    if(drawing_width_label != NULL) {
        lv_label_set_text_fmt(drawing_width_label, "%u", (unsigned int)drawing_brush_width);
    }
}

static void drawing_set_pixel(int32_t x, int32_t y, uint16_t color)
{
    uint16_t * row;

    if(x < 0 || x >= DRAWING_CANVAS_WIDTH ||
       y < 0 || y >= DRAWING_CANVAS_HEIGHT) {
        return;
    }

    row = (uint16_t *)(drawing_canvas_buffer + ((uint32_t)y * drawing_canvas_stride));
    row[x] = color;
}

static void drawing_stamp(int32_t center_x, int32_t center_y, uint16_t color)
{
    int32_t radius = (int32_t)drawing_brush_width / 2;
    int32_t x;
    int32_t y;

    for(y = -radius; y <= radius; y++) {
        for(x = -radius; x <= radius; x++) {
            if((x * x) + (y * y) <= (radius * radius) + radius) {
                drawing_set_pixel(center_x + x, center_y + y, color);
            }
        }
    }
}

static void drawing_draw_line(lv_point_t from, lv_point_t to)
{
    int32_t x = from.x;
    int32_t y = from.y;
    int32_t delta_x = LV_ABS(to.x - from.x);
    int32_t step_x = from.x < to.x ? 1 : -1;
    int32_t delta_y = -LV_ABS(to.y - from.y);
    int32_t step_y = from.y < to.y ? 1 : -1;
    int32_t error = delta_x + delta_y;
    uint16_t color = lv_color_to_u16(drawing_tool == DRAWING_TOOL_ERASER
                                     ? lv_color_white()
                                     : drawing_color);

    while(true) {
        int32_t doubled_error;

        drawing_stamp(x, y, color);
        if(x == to.x && y == to.y) break;

        doubled_error = error * 2;
        if(doubled_error >= delta_y) {
            error += delta_y;
            x += step_x;
        }
        if(doubled_error <= delta_x) {
            error += delta_x;
            y += step_y;
        }
    }
}

static bool drawing_get_canvas_point(lv_point_t * point)
{
    lv_indev_t * input = lv_indev_active();
    lv_area_t canvas_area;

    if(input == NULL || point == NULL || drawing_canvas == NULL) return false;

    lv_indev_get_point(input, point);
    lv_obj_get_coords(drawing_canvas, &canvas_area);
    point->x -= canvas_area.x1;
    point->y -= canvas_area.y1;

    return point->x >= 0 && point->x < DRAWING_CANVAS_WIDTH &&
           point->y >= 0 && point->y < DRAWING_CANVAS_HEIGHT;
}

static void drawing_canvas_event_cb(lv_event_t * event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_point_t point;

    if(code == LV_EVENT_PRESSED) {
        if(drawing_get_canvas_point(&point)) {
            drawing_pointer_active = true;
            drawing_previous_point = point;
            drawing_draw_line(point, point);
            drawing_modified = true;
            lv_obj_invalidate(drawing_canvas);
            drawing_set_status("Drawing", lv_color_hex(0x294A42));
        }
    }
    else if(code == LV_EVENT_PRESSING) {
        if(!drawing_get_canvas_point(&point)) {
            drawing_pointer_active = false;
            return;
        }

        if(drawing_pointer_active) {
            drawing_draw_line(drawing_previous_point, point);
        }
        else {
            drawing_draw_line(point, point);
            drawing_pointer_active = true;
        }
        drawing_previous_point = point;
        drawing_modified = true;
        lv_obj_invalidate(drawing_canvas);
    }
    else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        drawing_pointer_active = false;
    }
}

static void drawing_pen_event_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;

    drawing_tool = DRAWING_TOOL_PEN;
    drawing_update_tool_buttons();
    drawing_set_status("Pen", lv_color_hex(0x294A42));
}

static void drawing_eraser_event_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;

    drawing_tool = DRAWING_TOOL_ERASER;
    drawing_update_tool_buttons();
    drawing_set_status("Eraser", lv_color_hex(0x294A42));
}

static void drawing_width_event_cb(lv_event_t * event)
{
    int32_t direction;
    int32_t next_width;

    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;

    direction = (int32_t)(intptr_t)lv_event_get_user_data(event);
    next_width = (int32_t)drawing_brush_width + (direction * DRAWING_BRUSH_STEP);
    if(next_width < DRAWING_BRUSH_MIN) next_width = DRAWING_BRUSH_MIN;
    if(next_width > DRAWING_BRUSH_MAX) next_width = DRAWING_BRUSH_MAX;
    drawing_brush_width = (uint8_t)next_width;
    drawing_update_width_label();
    drawing_set_status("Brush size", lv_color_hex(0x294A42));
}

static void drawing_color_event_cb(lv_event_t * event)
{
    const uint32_t * color_value;

    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;

    color_value = (const uint32_t *)lv_event_get_user_data(event);
    if(color_value == NULL) return;

    drawing_color = lv_color_hex(*color_value);
    drawing_tool = DRAWING_TOOL_PEN;
    drawing_update_tool_buttons();
    drawing_update_color_buttons();
    drawing_set_status("Color selected", lv_color_hex(0x294A42));
}

static void drawing_clear_event_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED ||
       drawing_canvas_buffer == NULL) {
        return;
    }

    memset(drawing_canvas_buffer, 0xFF, drawing_canvas_stride * DRAWING_CANVAS_HEIGHT);
    drawing_pointer_active = false;
    drawing_modified = true;
    lv_obj_invalidate(drawing_canvas);
    drawing_set_status("Canvas cleared", lv_color_hex(0x294A42));
}

static const char * drawing_fatfs_error_text(FRESULT result)
{
    switch(result) {
        case FR_NOT_READY:
            return "SD not ready";
        case FR_WRITE_PROTECTED:
            return "Write protected";
        case FR_DENIED:
            return "Access denied";
        case FR_DISK_ERR:
            return "SD write error";
        case FR_NOT_ENOUGH_CORE:
            return "No memory";
        case FR_INVALID_OBJECT:
            return "Unsupported BMP format";
        case FR_INVALID_PARAMETER:
            return "BMP must be 308 x 344";
        default:
            return "Save failed";
    }
}

static FRESULT drawing_find_save_path(char * path, uint32_t path_size)
{
    FILINFO info;
    FRESULT result;
    uint32_t index;

    result = f_mkdir("0:/Pictures");
    if(result == FR_OK) {
        SystemLog_Write(SYSTEM_LOG_CREATE_FOLDER, "0:/Pictures", result);
    }
    if(result != FR_OK && result != FR_EXIST) return result;

    for(index = 1U; index <= 9999U; index++) {
        lv_snprintf(path,
                    path_size,
                    "0:/Pictures/DRAW%04lu.BMP",
                    (unsigned long)index);
        result = f_stat(path, &info);
        if(result == FR_NO_FILE) return FR_OK;
        if(result != FR_OK) return result;
    }

    return FR_EXIST;
}

static FRESULT drawing_read_bmp(const char * path)
{
    FIL file;
    FRESULT result;
    UINT bytes_read;
    uint8_t header[DRAWING_BMP_HEADER_SIZE];
    uint8_t row[DRAWING_BMP_ROW_SIZE];
    uint32_t pixel_offset;
    int32_t width;
    int32_t height;
    uint32_t source_row;

    memset(header, 0, sizeof(header));
    result = f_open(&file, path, FA_READ);
    if(result != FR_OK) return result;

    result = f_read(&file, header, sizeof(header), &bytes_read);
    if(result == FR_OK && bytes_read != sizeof(header)) result = FR_INVALID_OBJECT;

    if(result == FR_OK &&
       (header[0] != 'B' || header[1] != 'M' ||
        drawing_get_u32(&header[14]) < 40U ||
        drawing_get_u16(&header[26]) != 1U ||
        drawing_get_u16(&header[28]) != 24U ||
        drawing_get_u32(&header[30]) != 0U)) {
        result = FR_INVALID_OBJECT;
    }

    width = (int32_t)drawing_get_u32(&header[18]);
    height = (int32_t)drawing_get_u32(&header[22]);
    if(result == FR_OK &&
       (width != DRAWING_CANVAS_WIDTH ||
        (height != DRAWING_CANVAS_HEIGHT && height != -DRAWING_CANVAS_HEIGHT))) {
        result = FR_INVALID_PARAMETER;
    }

    pixel_offset = drawing_get_u32(&header[10]);
    if(result == FR_OK) result = f_lseek(&file, pixel_offset);

    for(source_row = 0U;
        result == FR_OK && source_row < DRAWING_CANVAS_HEIGHT;
        source_row++) {
        uint32_t target_y = height > 0
                            ? (DRAWING_CANVAS_HEIGHT - 1U - source_row)
                            : source_row;
        uint16_t * destination = (uint16_t *)(drawing_canvas_buffer +
                                              (target_y * drawing_canvas_stride));
        uint32_t x;

        result = f_read(&file, row, sizeof(row), &bytes_read);
        if(result == FR_OK && bytes_read != sizeof(row)) {
            result = FR_INVALID_OBJECT;
            break;
        }

        for(x = 0U; x < DRAWING_CANVAS_WIDTH; x++) {
            uint8_t blue = row[(x * 3U) + 0U];
            uint8_t green = row[(x * 3U) + 1U];
            uint8_t red = row[(x * 3U) + 2U];
            destination[x] = (uint16_t)(((uint16_t)(red & 0xF8U) << 8) |
                                        ((uint16_t)(green & 0xFCU) << 3) |
                                        ((uint16_t)blue >> 3));
        }
    }

    {
        FRESULT close_result = f_close(&file);
        if(result == FR_OK) result = close_result;
    }
    return result;
}

static FRESULT drawing_write_bmp(const char * path, bool replace_existing)
{
    FIL file;
    FRESULT result;
    UINT bytes_written;
    uint8_t header[DRAWING_BMP_HEADER_SIZE];
    uint8_t row[DRAWING_BMP_ROW_SIZE];
    uint32_t image_size = DRAWING_BMP_ROW_SIZE * DRAWING_CANVAS_HEIGHT;
    int32_t y;

    memset(header, 0, sizeof(header));
    header[0] = 'B';
    header[1] = 'M';
    drawing_put_u32(&header[2], DRAWING_BMP_HEADER_SIZE + image_size);
    drawing_put_u32(&header[10], DRAWING_BMP_HEADER_SIZE);
    drawing_put_u32(&header[14], 40U);
    drawing_put_u32(&header[18], DRAWING_CANVAS_WIDTH);
    drawing_put_u32(&header[22], DRAWING_CANVAS_HEIGHT);
    drawing_put_u16(&header[26], 1U);
    drawing_put_u16(&header[28], 24U);
    drawing_put_u32(&header[34], image_size);
    drawing_put_u32(&header[38], 2835U);
    drawing_put_u32(&header[42], 2835U);

    result = f_open(&file,
                    path,
                    (replace_existing ? FA_CREATE_ALWAYS : FA_CREATE_NEW) |
                    FA_WRITE);
    if(result != FR_OK) return result;

    result = f_write(&file, header, sizeof(header), &bytes_written);
    if(result == FR_OK && bytes_written != sizeof(header)) result = FR_DISK_ERR;

    for(y = DRAWING_CANVAS_HEIGHT - 1; result == FR_OK && y >= 0; y--) {
        const uint16_t * source = (const uint16_t *)(drawing_canvas_buffer +
                                                    ((uint32_t)y * drawing_canvas_stride));
        uint32_t x;

        memset(row, 0, sizeof(row));
        for(x = 0U; x < DRAWING_CANVAS_WIDTH; x++) {
            uint16_t pixel = source[x];
            row[(x * 3U) + 0U] = (uint8_t)(((pixel & 0x001FU) * 255U) / 31U);
            row[(x * 3U) + 1U] = (uint8_t)((((pixel >> 5) & 0x003FU) * 255U) / 63U);
            row[(x * 3U) + 2U] = (uint8_t)((((pixel >> 11) & 0x001FU) * 255U) / 31U);
        }

        result = f_write(&file, row, sizeof(row), &bytes_written);
        if(result == FR_OK && bytes_written != sizeof(row)) result = FR_DISK_ERR;
    }

    if(result == FR_OK) result = f_sync(&file);
    {
        FRESULT close_result = f_close(&file);
        if(result == FR_OK) result = close_result;
    }

    if(result != FR_OK) {
        (void)f_unlink(path);
    }
    return result;
}

static void drawing_save_event_cb(lv_event_t * event)
{
    char path[DRAWING_PATH_MAX] = "0:/Pictures";
    FRESULT result;
    bool replace_existing;

    if(lv_event_get_code(event) != LV_EVENT_CLICKED ||
       drawing_canvas_buffer == NULL) {
        return;
    }

    drawing_set_status("Saving...", lv_color_hex(0x294A42));
    lv_refr_now(NULL);

    replace_existing = drawing_current_path[0] != '\0';
    if(replace_existing && !drawing_modified) {
        drawing_set_status("No changes", lv_color_hex(0x294A42));
        return;
    }

    if(replace_existing) {
        strcpy(path, drawing_current_path);
        result = FR_OK;
    }
    else {
        result = drawing_find_save_path(path, sizeof(path));
    }
    if(result == FR_OK) {
        result = drawing_write_bmp(path, replace_existing);
    }
    SystemLog_Write(replace_existing ? SYSTEM_LOG_SAVE : SYSTEM_LOG_CREATE_FILE,
                    path, result);

    if(result == FR_OK) {
        const char * filename = strrchr(path, '/');
        strcpy(drawing_current_path, path);
        drawing_modified = false;
        drawing_set_status(filename != NULL ? filename + 1 : path,
                           lv_palette_main(LV_PALETTE_GREEN));
    }
    else {
        drawing_set_status(drawing_fatfs_error_text(result),
                           lv_palette_main(LV_PALETTE_RED));
    }
}

static void drawing_delete_event_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) != LV_EVENT_DELETE) return;

    drawing_root = NULL;
    drawing_canvas = NULL;
    drawing_status_label = NULL;
    drawing_width_label = NULL;
    drawing_pen_button = NULL;
    drawing_eraser_button = NULL;
    memset(drawing_color_buttons, 0, sizeof(drawing_color_buttons));
    drawing_pointer_active = false;
    drawing_current_path[0] = '\0';
    drawing_modified = false;
}

static lv_obj_t * drawing_create_button(lv_obj_t * parent,
                                        int32_t x,
                                        int32_t width,
                                        const char * text,
                                        lv_event_cb_t callback,
                                        void * user_data)
{
    lv_obj_t * button = lv_button_create(parent);
    lv_obj_t * label;

    lv_obj_set_pos(button, x, 49);
    lv_obj_set_size(button, width, 32);
    lv_obj_set_style_radius(button, 4, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
    if(callback != NULL) {
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    }

    label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

static bool drawing_create(lv_obj_t * parent, const char * path)
{
    uint32_t index;
    uint32_t buffer_size;
    lv_obj_t * button;

    if(parent == NULL || drawing_root != NULL ||
       (path != NULL && strlen(path) >= sizeof(drawing_current_path))) {
        if(path != NULL) SystemLog_Write(SYSTEM_LOG_OPEN, path, SYSTEM_LOG_ERROR_UI);
        return false;
    }

    drawing_root = parent;
    drawing_canvas_stride = LV_DRAW_BUF_STRIDE(DRAWING_CANVAS_WIDTH,
                                               LV_COLOR_FORMAT_RGB565);
    buffer_size = drawing_canvas_stride * DRAWING_CANVAS_HEIGHT;
    if(drawing_canvas_buffer == NULL) {
        drawing_canvas_buffer = (uint8_t *)mymalloc(SRAMEX, buffer_size);
    }
    if(drawing_canvas_buffer == NULL) {
        lv_obj_t * error_label = lv_label_create(parent);
        lv_label_set_text(error_label, "Not enough external SRAM for the canvas");
        lv_obj_set_width(error_label, 280);
        lv_obj_set_style_text_align(error_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_color(error_label,
                                    lv_palette_main(LV_PALETTE_RED),
                                    LV_PART_MAIN);
        lv_obj_align(error_label, LV_ALIGN_CENTER, 0, 0);
        drawing_root = NULL;
        if(path != NULL) SystemLog_Write(SYSTEM_LOG_OPEN, path, SYSTEM_LOG_ERROR_MEMORY);
        return false;
    }

    memset(drawing_canvas_buffer, 0xFF, buffer_size);
    drawing_tool = DRAWING_TOOL_PEN;
    drawing_color = lv_color_hex(drawing_palette[0]);
    drawing_brush_width = 5U;
    drawing_pointer_active = false;
    drawing_modified = false;
    if(path != NULL) {
        strcpy(drawing_current_path, path);
    }
    else {
        drawing_current_path[0] = '\0';
    }
    lv_obj_add_event_cb(parent, drawing_delete_event_cb, LV_EVENT_DELETE, NULL);

    drawing_pen_button = drawing_create_button(parent,
                                               6,
                                               40,
                                               "Pen",
                                               drawing_pen_event_cb,
                                               NULL);
    drawing_eraser_button = drawing_create_button(parent,
                                                  50,
                                                  52,
                                                  "Erase",
                                                  drawing_eraser_event_cb,
                                                  NULL);
    (void)drawing_create_button(parent,
                                106,
                                28,
                                LV_SYMBOL_MINUS,
                                drawing_width_event_cb,
                                (void *)(intptr_t)-1);

    drawing_width_label = lv_label_create(parent);
    lv_obj_set_width(drawing_width_label, 22);
    lv_obj_set_style_text_align(drawing_width_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(drawing_width_label, 138, 58);

    (void)drawing_create_button(parent,
                                164,
                                28,
                                LV_SYMBOL_PLUS,
                                drawing_width_event_cb,
                                (void *)(intptr_t)1);
    button = drawing_create_button(parent,
                                   202,
                                   48,
                                   LV_SYMBOL_TRASH,
                                   drawing_clear_event_cb,
                                   NULL);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xA54444), LV_PART_MAIN);
    button = drawing_create_button(parent,
                                   254,
                                   60,
                                   LV_SYMBOL_SAVE,
                                   drawing_save_event_cb,
                                   NULL);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x2878C7), LV_PART_MAIN);

    for(index = 0U; index < 6U; index++) {
        drawing_color_buttons[index] = lv_button_create(parent);
        lv_obj_set_pos(drawing_color_buttons[index], 8 + ((int32_t)index * 34), 89);
        lv_obj_set_size(drawing_color_buttons[index], 28, 28);
        lv_obj_set_style_radius(drawing_color_buttons[index], 4, LV_PART_MAIN);
        lv_obj_set_style_bg_color(drawing_color_buttons[index],
                                  lv_color_hex(drawing_palette[index]),
                                  LV_PART_MAIN);
        lv_obj_set_style_shadow_width(drawing_color_buttons[index], 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(drawing_color_buttons[index], 0, LV_PART_MAIN);
        lv_obj_add_event_cb(drawing_color_buttons[index],
                            drawing_color_event_cb,
                            LV_EVENT_CLICKED,
                            (void *)&drawing_palette[index]);
    }

    drawing_status_label = lv_label_create(parent);
    lv_obj_set_pos(drawing_status_label, 214, 96);
    lv_obj_set_width(drawing_status_label, 100);
    lv_label_set_long_mode(drawing_status_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(drawing_status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(drawing_status_label, lv_color_hex(0x294A42), LV_PART_MAIN);
    lv_label_set_text(drawing_status_label, "Ready");

    drawing_canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(drawing_canvas,
                         drawing_canvas_buffer,
                         DRAWING_CANVAS_WIDTH,
                         DRAWING_CANVAS_HEIGHT,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(drawing_canvas, DRAWING_CANVAS_X, DRAWING_CANVAS_Y);
    lv_obj_add_flag(drawing_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(drawing_canvas, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_remove_flag(drawing_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(drawing_canvas, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(drawing_canvas, lv_color_hex(0xAAB8B3), LV_PART_MAIN);
    lv_obj_add_event_cb(drawing_canvas, drawing_canvas_event_cb, LV_EVENT_ALL, NULL);

    drawing_update_tool_buttons();
    drawing_update_color_buttons();
    drawing_update_width_label();

    if(path != NULL) {
        FRESULT result = drawing_read_bmp(path);
        SystemLog_Write(SYSTEM_LOG_OPEN, path, result);
        if(result == FR_OK) {
            const char * filename = strrchr(path, '/');
            drawing_set_status(filename != NULL ? filename + 1 : path,
                               lv_palette_main(LV_PALETTE_GREEN));
            lv_obj_invalidate(drawing_canvas);
        }
        else {
            memset(drawing_canvas_buffer,
                   0xFF,
                   drawing_canvas_stride * DRAWING_CANVAS_HEIGHT);
            drawing_current_path[0] = '\0';
            drawing_set_status(drawing_fatfs_error_text(result),
                               lv_palette_main(LV_PALETTE_RED));
        }
    }
    return true;
}

bool DrawingApp_Create(lv_obj_t * parent)
{
    return drawing_create(parent, NULL);
}

bool DrawingApp_CreateFromFile(lv_obj_t * parent, const char * path)
{
    if(path == NULL || path[0] == '\0') return false;
    return drawing_create(parent, path);
}
