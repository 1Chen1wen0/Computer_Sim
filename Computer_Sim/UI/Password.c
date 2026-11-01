#include "Password.h"
#include "KeyboardInput.h"
#include "DesktopUI.h"
#include "lv_port_indev_template.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdbool.h>
#include <string.h>

#define PASSWORD_MAX_FAILURES 3U
#define PASSWORD_LOCKOUT_MS 30000U
#define PASSWORD_LOCKOUT_TIMER_PERIOD_MS 250U

static const char target_password[] = "123456";

static lv_obj_t * password_panel;
static lv_obj_t * password_input;
static lv_obj_t * confirm_button;
static lv_obj_t * hint_label;
static lv_obj_t * lockout_panel;
static lv_obj_t * lockout_label;
static lv_group_t * password_group;
static lv_timer_t * lockout_timer;
static TickType_t lockout_started_at;
static uint8_t password_failures;
static bool password_locked;

static void password_lockout_timer_cb(lv_timer_t * timer);

static void password_leave_lockout(void)
{
    password_locked = false;
    password_failures = 0U;

    if(lockout_panel != NULL) {
        lv_obj_delete(lockout_panel);
        lockout_panel = NULL;
        lockout_label = NULL;
    }

    if(password_input != NULL) {
        lv_textarea_set_text(password_input, "");
    }

    if(indev_keypad != NULL && password_group != NULL) {
        lv_indev_set_group(indev_keypad, password_group);
        lv_group_focus_obj(password_input);
    }

    KeyboardInput_Clear();
}

static void password_enter_lockout(void)
{
    password_locked = true;
    lockout_started_at = xTaskGetTickCount();
    KeyboardInput_Clear();

    if(indev_keypad != NULL) {
        lv_indev_set_group(indev_keypad, NULL);
    }

    lockout_panel = lv_obj_create(password_panel);
    lv_obj_set_size(lockout_panel, LV_PCT(100), LV_PCT(100));
    lv_obj_center(lockout_panel);
    lv_obj_set_style_radius(lockout_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(lockout_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(lockout_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(lockout_panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lockout_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(lockout_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(lockout_panel, LV_OBJ_FLAG_CLICKABLE);

    lockout_label = lv_label_create(lockout_panel);
    lv_label_set_text(lockout_label, "Locked\nTry again in 30 s");
    lv_obj_set_style_text_color(lockout_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_align(lockout_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(lockout_label);

    if(lockout_timer == NULL) {
        lockout_timer = lv_timer_create(password_lockout_timer_cb,
                                        PASSWORD_LOCKOUT_TIMER_PERIOD_MS,
                                        NULL);
    }
}

static void password_lockout_timer_cb(lv_timer_t * timer)
{
    TickType_t timeout_ticks = pdMS_TO_TICKS(PASSWORD_LOCKOUT_MS);
    TickType_t elapsed = xTaskGetTickCount() - lockout_started_at;

    if(elapsed >= timeout_ticks) {
        lv_timer_delete(timer);
        lockout_timer = NULL;
        password_leave_lockout();
        return;
    }

    if(lockout_label != NULL) {
        TickType_t remaining_ticks = timeout_ticks - elapsed;
        uint32_t remaining_seconds =
            ((uint32_t)remaining_ticks + (uint32_t)configTICK_RATE_HZ - 1U) /
            (uint32_t)configTICK_RATE_HZ;

        lv_label_set_text_fmt(lockout_label,
                              "Locked\nTry again in %lu s",
                              (unsigned long)remaining_seconds);
    }
}

static void password_panel_delete_event_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_DELETE) return;

    if(lockout_timer != NULL) {
        lv_timer_delete(lockout_timer);
        lockout_timer = NULL;
    }

    if(password_group != NULL) {
        lv_group_delete(password_group);
        password_group = NULL;
    }

    password_panel = NULL;
    password_input = NULL;
    confirm_button = NULL;
    hint_label = NULL;
    lockout_panel = NULL;
    lockout_label = NULL;
    password_failures = 0U;
    password_locked = false;
}

static void password_input_event_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED &&
       hint_label != NULL &&
       !password_locked) {
        lv_label_set_text(hint_label, "");
    }
}

static void confirm_button_event_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED || password_locked) return;

    if(strcmp(lv_textarea_get_text(password_input), target_password) == 0) {
        /* Delete only this interface. The SDL cursor belongs to another layer. */
        password_failures = 0U;
        KeyboardInput_Clear();
        DesktopUI_Create();
        lv_obj_delete_async(password_panel);
        password_panel = NULL;
        password_input = NULL;
        confirm_button = NULL;
        hint_label = NULL;
        return;
    }

    password_failures++;
    lv_textarea_set_text(password_input, "");

    if(password_failures >= PASSWORD_MAX_FAILURES) {
        password_enter_lockout();
        return;
    }

    lv_label_set_text_fmt(hint_label,
                          "Incorrect password. %u attempts left",
                          (unsigned int)(PASSWORD_MAX_FAILURES - password_failures));
}

void Password_ui_create(void)
{
    lv_obj_t * screen = lv_screen_active();

    if(password_panel != NULL) return;

    password_failures = 0U;
    password_locked = false;

    password_panel = lv_obj_create(screen);
    lv_obj_set_size(password_panel, LV_PCT(100), LV_PCT(100));
    lv_obj_center(password_panel);
    lv_obj_set_style_radius(password_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(password_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(password_panel, 0, LV_PART_MAIN);
    lv_obj_remove_flag(password_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(password_panel, password_panel_delete_event_cb, LV_EVENT_DELETE, NULL);

    password_input = lv_textarea_create(password_panel);
    lv_textarea_set_one_line(password_input, true);
    lv_textarea_set_password_mode(password_input, true);
    lv_textarea_set_max_length(password_input, KEYBOARD_INPUT_MAX_LEN);
    lv_textarea_set_placeholder_text(password_input, "Password");
    lv_obj_set_size(password_input, 220, 40);
    lv_obj_align(password_input, LV_ALIGN_TOP_MID, 0, 110);
    lv_obj_set_style_text_align(password_input, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_add_event_cb(password_input,
                        password_input_event_cb,
                        LV_EVENT_VALUE_CHANGED,
                        NULL);

    confirm_button = lv_button_create(password_panel);
    lv_obj_set_size(confirm_button, 120, 48);
    lv_obj_align(confirm_button, LV_ALIGN_TOP_MID, 0, 175);
    lv_obj_add_event_cb(confirm_button, confirm_button_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * confirm_label = lv_label_create(confirm_button);
    lv_label_set_text(confirm_label, "Confirm");
    lv_obj_center(confirm_label);

    /* Empty until the first failed confirmation. */
    hint_label = lv_label_create(password_panel);
    lv_label_set_text(hint_label, "");
    lv_obj_set_width(hint_label, 220);
    lv_obj_align(hint_label, LV_ALIGN_TOP_MID, 0, 245);
    lv_obj_set_style_text_align(hint_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint_label, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);

    password_group = lv_group_create();
    lv_group_add_obj(password_group, password_input);
    lv_group_add_obj(password_group, confirm_button);
    lv_indev_set_group(indev_keypad, password_group);
    lv_group_focus_obj(password_input);

    KeyboardInput_Clear();
}
