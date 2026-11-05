#include "DesktopUI.h"

#include "AudioPlayer.h"
#include "DrawingApp.h"
#include "KeyboardInput.h"
#include "SystemSettings.h"
#include "SystemLog.h"
#include "desktop.h"
#include "ff.h"
#include "lv_port_indev_template.h"
#include "malloc.h"
#include "rtc.h"

#include <string.h>

#define DESKTOP_SHORTCUT_WIDTH 76
#define DESKTOP_SHORTCUT_HEIGHT 72
#define DESKTOP_PAGE_HEADER_HEIGHT 44
#define DESKTOP_ENTRY_NAME_MAX 63U
#define DESKTOP_FILES_PATH_MAX 256U
#define DESKTOP_FILES_ROOT "0:/"
#define DESKTOP_TEXT_FILE_MAX 8192U
#define DESKTOP_AUDIO_PLAYLIST_MAX 32U
#define DESKTOP_AUDIO_NAME_MAX 63U
#define DESKTOP_AUDIO_STATUS_REFRESH_MS 100U
#define DESKTOP_STATUS_WIDTH 206
#define DESKTOP_STATUS_HEIGHT 36
#define DESKTOP_STATUS_REFRESH_MS 1000U

static lv_obj_t * desktop_root;
static lv_obj_t * active_page;
static lv_obj_t * files_list;
static lv_obj_t * files_path_label;
static lv_obj_t * files_status_label;
static lv_obj_t * folder_dialog;
static lv_obj_t * folder_name_input;
static lv_obj_t * folder_dialog_status;
static lv_obj_t * delete_dialog;
static lv_obj_t * delete_dialog_status;
static lv_group_t * folder_input_group;
static char files_current_path[DESKTOP_FILES_PATH_MAX] = DESKTOP_FILES_ROOT;
static char delete_target_path[DESKTOP_FILES_PATH_MAX];
static bool create_folder_mode;
static bool delete_target_is_directory;
static lv_obj_t * text_editor;
static lv_obj_t * text_editor_status;
static char text_editor_path[DESKTOP_FILES_PATH_MAX];
static bool text_editor_loading;
static bool text_editor_modified;
static char (*audio_playlist)[DESKTOP_AUDIO_NAME_MAX + 1U];
static char audio_directory[DESKTOP_FILES_PATH_MAX];
static uint16_t audio_playlist_count;
static uint16_t audio_playlist_index;
static lv_obj_t * audio_track_label;
static lv_obj_t * audio_index_label;
static lv_obj_t * audio_state_label;
static lv_obj_t * audio_play_label;
static lv_timer_t * audio_status_timer;
static lv_obj_t * settings_mouse_slider;
static lv_obj_t * settings_brightness_slider;
static lv_obj_t * settings_volume_slider;
static lv_obj_t * settings_status_label;
static lv_obj_t * desktop_volume_label;
static lv_obj_t * desktop_brightness_label;
static lv_obj_t * desktop_time_label;
static lv_timer_t * desktop_status_timer;
typedef struct {
    lv_obj_t * list;
    lv_obj_t * status;
    lv_timer_t * timer;
    uint16_t offset;
    uint32_t version;
} DesktopLogsView;
static DesktopLogsView * logs_view;

static void desktop_logs_release(void)
{
    if(logs_view == NULL) return;
    if(logs_view->timer != NULL) lv_timer_delete(logs_view->timer);
    myfree(SRAMEX, logs_view);
    logs_view = NULL;
}

static FRESULT desktop_files_refresh(void);
static void desktop_open_delete_dialog(const char * name, bool is_directory);
static void desktop_settings_slider_event_cb(lv_event_t * e);
static void desktop_settings_percent_slider_event_cb(lv_event_t * e);
static void desktop_settings_apply_event_cb(lv_event_t * e);
static void desktop_settings_cancel_event_cb(lv_event_t * e);
static void desktop_file_open_event_cb(lv_event_t * e);
static void desktop_open_audio_file(const char * name, const char * path);

static void desktop_status_refresh(void)
{
    RTC_TimeTypeDef time;
    RTC_DateTypeDef date;
    const SystemSettingsValues * values = SystemSettings_Get();

    if(desktop_volume_label != NULL) {
        lv_label_set_text_fmt(desktop_volume_label,
                              LV_SYMBOL_VOLUME_MID " %u%%",
                              (unsigned int)values->volume);
    }

    if(desktop_brightness_label != NULL) {
        lv_label_set_text_fmt(desktop_brightness_label,
                              LV_SYMBOL_EYE_OPEN " %u%%",
                              (unsigned int)values->brightness);
    }

    if(desktop_time_label == NULL) return;

    if(HAL_RTC_GetTime(&RTC_Handler, &time, RTC_FORMAT_BIN) == HAL_OK) {
        /* Reading the date releases the RTC shadow registers for the next read. */
        (void)HAL_RTC_GetDate(&RTC_Handler, &date, RTC_FORMAT_BIN);
        lv_label_set_text_fmt(desktop_time_label,
                              "%02u:%02u:%02u",
                              (unsigned int)time.Hours,
                              (unsigned int)time.Minutes,
                              (unsigned int)time.Seconds);
    }
    else {
        lv_label_set_text(desktop_time_label, "--:--:--");
    }
}

static void desktop_status_timer_cb(lv_timer_t * timer)
{
    (void)timer;
    desktop_status_refresh();
}

static void desktop_create_status_bar(void)
{
    lv_obj_t * status_bar;

    status_bar = lv_obj_create(desktop_root);
    lv_obj_set_size(status_bar, DESKTOP_STATUS_WIDTH, DESKTOP_STATUS_HEIGHT);
    lv_obj_align(status_bar, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_set_style_radius(status_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(status_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(status_bar,
                                  lv_color_hex(0xD6E8DF),
                                  LV_PART_MAIN);
    lv_obj_set_style_bg_color(status_bar,
                              lv_color_hex(0x173B32),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(status_bar, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_pad_all(status_bar, 0, LV_PART_MAIN);
    lv_obj_remove_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

    desktop_volume_label = lv_label_create(status_bar);
    lv_obj_set_width(desktop_volume_label, 60);
    lv_obj_align(desktop_volume_label, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_set_style_text_align(desktop_volume_label,
                                LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(desktop_volume_label,
                                lv_color_white(),
                                LV_PART_MAIN);

    desktop_brightness_label = lv_label_create(status_bar);
    lv_obj_set_width(desktop_brightness_label, 60);
    lv_obj_align(desktop_brightness_label, LV_ALIGN_LEFT_MID, 65, 0);
    lv_obj_set_style_text_align(desktop_brightness_label,
                                LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(desktop_brightness_label,
                                lv_color_white(),
                                LV_PART_MAIN);

    desktop_time_label = lv_label_create(status_bar);
    lv_obj_set_width(desktop_time_label, 76);
    lv_obj_align(desktop_time_label, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_set_style_text_align(desktop_time_label,
                                LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(desktop_time_label,
                                lv_color_white(),
                                LV_PART_MAIN);

    desktop_status_refresh();
    desktop_status_timer = lv_timer_create(desktop_status_timer_cb,
                                           DESKTOP_STATUS_REFRESH_MS,
                                           NULL);
}

static void desktop_release_folder_input(void)
{
    if(indev_keypad != NULL && folder_input_group != NULL) {
        lv_indev_set_group(indev_keypad, NULL);
    }

    if(folder_input_group != NULL) {
        lv_group_delete(folder_input_group);
        folder_input_group = NULL;
    }

    KeyboardInput_Clear();
}

static void desktop_root_delete_event_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_DELETE) return;
    desktop_logs_release();

    if(desktop_status_timer != NULL) {
        lv_timer_delete(desktop_status_timer);
        desktop_status_timer = NULL;
    }

    desktop_root = NULL;
    active_page = NULL;
    files_list = NULL;
    files_path_label = NULL;
    files_status_label = NULL;
    folder_dialog = NULL;
    folder_name_input = NULL;
    folder_dialog_status = NULL;
    delete_dialog = NULL;
    delete_dialog_status = NULL;
    text_editor = NULL;
    text_editor_status = NULL;
    text_editor_path[0] = '\0';
    text_editor_loading = false;
    text_editor_modified = false;
    audio_track_label = NULL;
    audio_index_label = NULL;
    audio_state_label = NULL;
    audio_play_label = NULL;
    audio_playlist_count = 0U;
    audio_playlist_index = 0U;
    audio_directory[0] = '\0';
    if(audio_playlist != NULL) {
        myfree(SRAMEX, audio_playlist);
        audio_playlist = NULL;
    }
    if(audio_status_timer != NULL) {
        lv_timer_delete(audio_status_timer);
        audio_status_timer = NULL;
    }
    settings_mouse_slider = NULL;
    settings_brightness_slider = NULL;
    settings_volume_slider = NULL;
    settings_status_label = NULL;
    desktop_volume_label = NULL;
    desktop_brightness_label = NULL;
    desktop_time_label = NULL;
    desktop_release_folder_input();
}

static void desktop_page_delete_event_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_DELETE) {
        desktop_logs_release();
        active_page = NULL;
        files_list = NULL;
        files_path_label = NULL;
        files_status_label = NULL;
        folder_dialog = NULL;
        folder_name_input = NULL;
        folder_dialog_status = NULL;
        delete_dialog = NULL;
        delete_dialog_status = NULL;
        text_editor = NULL;
        text_editor_status = NULL;
        text_editor_path[0] = '\0';
        text_editor_loading = false;
        text_editor_modified = false;
        /* Audio resources belong to desktop_audio_page_delete_event_cb.
         * Deleting Files must preserve the playlist prepared for Audio Player. */
        settings_mouse_slider = NULL;
        settings_brightness_slider = NULL;
        settings_volume_slider = NULL;
        settings_status_label = NULL;
        desktop_release_folder_input();
    }
}

static void desktop_folder_dialog_delete_event_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_DELETE) return;

    folder_dialog = NULL;
    folder_name_input = NULL;
    folder_dialog_status = NULL;
    desktop_release_folder_input();
}

static void desktop_delete_dialog_delete_event_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_DELETE) return;

    delete_dialog = NULL;
    delete_dialog_status = NULL;
    delete_target_path[0] = '\0';
    desktop_release_folder_input();
}

static void desktop_close_page_event_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED && active_page != NULL) {
        lv_obj_delete(active_page);
    }
}

static lv_obj_t * desktop_open_page(const char * title,
                                    const char * symbol,
                                    const char * empty_message)
{
    lv_obj_t * header;
    lv_obj_t * title_label;
    lv_obj_t * close_button;
    lv_obj_t * close_label;
    lv_obj_t * content_label;

    if(desktop_root == NULL) return NULL;

    if(active_page != NULL) {
        lv_obj_delete(active_page);
    }

    active_page = lv_obj_create(desktop_root);
    lv_obj_set_size(active_page, LV_PCT(100), LV_PCT(100));
    lv_obj_center(active_page);
    lv_obj_set_style_radius(active_page, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(active_page, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(active_page, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(active_page, lv_color_hex(0xF4F6F5), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(active_page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(active_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(active_page,
                        desktop_page_delete_event_cb,
                        LV_EVENT_DELETE,
                        NULL);

    header = lv_obj_create(active_page);
    lv_obj_set_size(header, LV_PCT(100), DESKTOP_PAGE_HEADER_HEIGHT);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(header, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(header, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x173B32), LV_PART_MAIN);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    title_label = lv_label_create(header);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 14, 0);

    close_button = lv_button_create(header);
    lv_obj_set_size(close_button, 40, 40);
    lv_obj_align(close_button, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_set_style_radius(close_button, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(close_button, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(close_button, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(close_button,
                        desktop_close_page_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);

    close_label = lv_label_create(close_button);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(close_label);

    if(symbol != NULL && empty_message != NULL) {
        content_label = lv_label_create(active_page);
        lv_label_set_text_fmt(content_label, "%s\n%s", symbol, empty_message);
        lv_obj_set_width(content_label, 260);
        lv_obj_set_style_text_align(content_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_color(content_label, lv_color_hex(0x294A42), LV_PART_MAIN);
        lv_obj_align(content_label, LV_ALIGN_CENTER, 0, 12);
    }

    return active_page;
}

static const char * desktop_fatfs_error_text(FRESULT result)
{
    switch(result) {
        case FR_DISK_ERR:
            return "SD card read/write error";
        case FR_NOT_READY:
            return "SD card is not ready";
        case FR_NO_FILESYSTEM:
            return "No FAT filesystem";
        case FR_WRITE_PROTECTED:
            return "SD card is write-protected";
        case FR_DENIED:
            return "Access denied";
        case FR_INVALID_NAME:
            return "Invalid file or folder name";
        default:
            return "Filesystem operation failed";
    }
}

static bool desktop_entry_state_matches(const char * path,
                                        bool should_exist,
                                        bool should_be_directory)
{
    FILINFO info;
    FRESULT result;

    result = f_stat(path, &info);
    if(!should_exist) {
        return result == FR_NO_FILE;
    }
    if(result != FR_OK) {
        return false;
    }

    return ((info.fattrib & AM_DIR) != 0U) == should_be_directory;
}

static void desktop_set_files_status(const char * text, lv_color_t color)
{
    if(files_status_label == NULL) return;

    lv_label_set_text(files_status_label, text);
    lv_obj_set_style_text_color(files_status_label, color, LV_PART_MAIN);
}

static bool desktop_build_child_path(const char * name,
                                     char * path,
                                     size_t path_size)
{
    size_t current_length;
    size_t name_length;
    bool needs_separator;
    size_t required_length;

    if(name == NULL || path == NULL || path_size == 0U) return false;

    current_length = strlen(files_current_path);
    name_length = strlen(name);
    needs_separator = current_length > 0U &&
                      files_current_path[current_length - 1U] != '/';
    required_length = current_length + (needs_separator ? 1U : 0U) +
                      name_length + 1U;
    if(required_length > path_size) return false;

    memcpy(path, files_current_path, current_length);
    if(needs_separator) {
        path[current_length++] = '/';
    }
    memcpy(path + current_length, name, name_length + 1U);
    return true;
}

static void desktop_update_path_label(void)
{
    if(files_path_label != NULL) {
        lv_label_set_text(files_path_label, files_current_path);
    }
}

static bool desktop_has_extension(const char * path, const char * extension)
{
    size_t path_length;
    size_t extension_length;
    size_t index;

    if(path == NULL || extension == NULL) return false;
    path_length = strlen(path);
    extension_length = strlen(extension);
    if(extension_length > path_length) return false;

    path += path_length - extension_length;
    for(index = 0U; index < extension_length; index++) {
        char left = path[index];
        char right = extension[index];
        if(left >= 'A' && left <= 'Z') left = (char)(left - 'A' + 'a');
        if(right >= 'A' && right <= 'Z') right = (char)(right - 'A' + 'a');
        if(left != right) return false;
    }
    return true;
}

static bool desktop_build_audio_path(uint16_t index,
                                     char * path,
                                     size_t path_size)
{
    size_t directory_length;
    size_t name_length;
    bool needs_separator;

    if(path == NULL || path_size == 0U || audio_playlist == NULL ||
       index >= audio_playlist_count) {
        return false;
    }
    directory_length = strlen(audio_directory);
    name_length = strlen(audio_playlist[index]);
    needs_separator = directory_length > 0U &&
                      audio_directory[directory_length - 1U] != '/';
    if(directory_length + (needs_separator ? 1U : 0U) + name_length + 1U >
       path_size) {
        return false;
    }

    memcpy(path, audio_directory, directory_length);
    if(needs_separator) path[directory_length++] = '/';
    memcpy(path + directory_length, audio_playlist[index], name_length + 1U);
    return true;
}

static void desktop_audio_update_selection(void)
{
    if(audio_playlist_count == 0U) return;
    if(audio_track_label != NULL) {
        lv_label_set_text(audio_track_label,
                          audio_playlist[audio_playlist_index]);
    }
    if(audio_index_label != NULL) {
        lv_label_set_text_fmt(audio_index_label,
                              "%u / %u",
                              (unsigned int)(audio_playlist_index + 1U),
                              (unsigned int)audio_playlist_count);
    }
}

static void desktop_audio_set_state(AudioPlayerState state, uint8_t error)
{
    const char * text;
    lv_color_t color = lv_color_hex(0x294A42);

    switch(state) {
        case AUDIO_PLAYER_LOADING:
            text = "Loading...";
            break;
        case AUDIO_PLAYER_PLAYING:
            text = "Playing";
            color = lv_palette_main(LV_PALETTE_GREEN);
            break;
        case AUDIO_PLAYER_PAUSED:
            text = "Paused";
            color = lv_color_hex(0x8A6500);
            break;
        case AUDIO_PLAYER_STOPPED:
            text = "Stopped";
            break;
        case AUDIO_PLAYER_FINISHED:
            text = "Finished";
            break;
        case AUDIO_PLAYER_ERROR:
            if(error == AUDIO_PLAYER_ERROR_BUFFER) text = "Audio buffer underrun";
            else if(error == AUDIO_PLAYER_ERROR_DATA) text = "Incomplete audio data";
            else if(error == AUDIO_PLAYER_ERROR_FILE) text = "Audio file invalid";
            else if(error == AUDIO_PLAYER_ERROR_DMA) text = "Audio DMA timeout";
            else text = error == (uint8_t)FR_INVALID_OBJECT
                   ? "Unsupported WAV"
                   : desktop_fatfs_error_text((FRESULT)error);
            color = lv_palette_main(LV_PALETTE_RED);
            break;
        default:
            text = "Ready";
            break;
    }

    if(audio_state_label != NULL) {
        lv_label_set_text(audio_state_label, text);
        lv_obj_set_style_text_color(audio_state_label, color, LV_PART_MAIN);
    }
    if(audio_play_label != NULL) {
        lv_label_set_text(audio_play_label,
                          state == AUDIO_PLAYER_PLAYING ||
                          state == AUDIO_PLAYER_LOADING
                          ? LV_SYMBOL_PAUSE
                          : LV_SYMBOL_PLAY);
    }
}

static void desktop_audio_status_timer_cb(lv_timer_t * timer)
{
    AudioPlayerEvent event;

    (void)timer;
    while(AudioPlayer_PollEvent(&event)) {
        desktop_audio_set_state(event.state, event.error);
    }
}

static void desktop_audio_show_error(const char * text)
{
    if(audio_state_label == NULL) return;
    lv_label_set_text(audio_state_label, text);
    lv_obj_set_style_text_color(audio_state_label,
                                lv_palette_main(LV_PALETTE_RED),
                                LV_PART_MAIN);
}

static void desktop_audio_play_event_cb(lv_event_t * e)
{
    AudioPlayerState state;
    char path[DESKTOP_FILES_PATH_MAX];
    bool command_sent;

    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    state = AudioPlayer_GetState();
    if(state == AUDIO_PLAYER_PLAYING || state == AUDIO_PLAYER_LOADING) {
        command_sent = AudioPlayer_Pause();
    }
    else if(state == AUDIO_PLAYER_PAUSED) {
        command_sent = AudioPlayer_Resume();
    }
    else if(desktop_build_audio_path(audio_playlist_index,
                                     path,
                                     sizeof(path))) {
        command_sent = AudioPlayer_Play(path);
    }
    else {
        desktop_audio_show_error("No valid song selected");
        return;
    }
    if(!command_sent) {
        desktop_audio_show_error("Unable to send audio command");
    }
}

static void desktop_audio_stop_event_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        (void)AudioPlayer_Stop();
    }
}

static void desktop_audio_change_track_event_cb(lv_event_t * e)
{
    int32_t direction;
    AudioPlayerState state;
    bool was_active;
    char path[DESKTOP_FILES_PATH_MAX];

    if(lv_event_get_code(e) != LV_EVENT_CLICKED || audio_playlist_count == 0U) {
        return;
    }
    direction = (int32_t)(intptr_t)lv_event_get_user_data(e);
    state = AudioPlayer_GetState();
    was_active = state == AUDIO_PLAYER_LOADING ||
                 state == AUDIO_PLAYER_PLAYING ||
                 state == AUDIO_PLAYER_PAUSED;

    if(direction < 0) {
        audio_playlist_index = audio_playlist_index == 0U
                               ? audio_playlist_count - 1U
                               : audio_playlist_index - 1U;
    }
    else {
        audio_playlist_index++;
        if(audio_playlist_index >= audio_playlist_count) {
            audio_playlist_index = 0U;
        }
    }
    desktop_audio_update_selection();

    if(was_active && desktop_build_audio_path(audio_playlist_index,
                                              path,
                                              sizeof(path))) {
        (void)AudioPlayer_Play(path);
    }
    else {
        desktop_audio_set_state(AUDIO_PLAYER_IDLE, 0U);
    }
}

static lv_obj_t * desktop_audio_create_button(lv_obj_t * parent,
                                               int32_t x,
                                               const char * symbol,
                                               lv_event_cb_t callback,
                                               void * user_data)
{
    lv_obj_t * button = lv_button_create(parent);
    lv_obj_t * label = lv_label_create(button);

    lv_obj_set_pos(button, x, 352);
    lv_obj_set_size(button, 54, 54);
    lv_obj_set_style_radius(button, 4, LV_PART_MAIN);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    lv_label_set_text(label, symbol);
    lv_obj_center(label);
    return label;
}

static void desktop_audio_page_delete_event_cb(lv_event_t * e)
{
    AudioPlayerState state;

    if(lv_event_get_code(e) != LV_EVENT_DELETE) return;
    state = AudioPlayer_GetState();
    if(state == AUDIO_PLAYER_LOADING || state == AUDIO_PLAYER_PLAYING ||
       state == AUDIO_PLAYER_PAUSED) {
        (void)AudioPlayer_Stop();
    }
    if(audio_status_timer != NULL) {
        lv_timer_delete(audio_status_timer);
        audio_status_timer = NULL;
    }
    audio_track_label = NULL;
    audio_index_label = NULL;
    audio_state_label = NULL;
    audio_play_label = NULL;
    audio_playlist_count = 0U;
    audio_playlist_index = 0U;
    audio_directory[0] = '\0';
    if(audio_playlist != NULL) {
        myfree(SRAMEX, audio_playlist);
        audio_playlist = NULL;
    }
}

static void desktop_open_audio_file(const char * name, const char * path)
{
    DIR directory;
    FILINFO info;
    FRESULT result;
    lv_obj_t * page;
    lv_obj_t * icon;
    bool selected_found = false;

    if(!AudioPlayer_IsReady()) {
        SystemLog_Write(SYSTEM_LOG_OPEN, path, SYSTEM_LOG_ERROR_UNAVAILABLE);
        desktop_set_files_status("Audio task is unavailable",
                                 lv_palette_main(LV_PALETTE_RED));
        return;
    }
    if(name == NULL || strlen(name) > DESKTOP_AUDIO_NAME_MAX) {
        SystemLog_Write(SYSTEM_LOG_OPEN, path, FR_INVALID_NAME);
        desktop_set_files_status("Audio file name is too long",
                                 lv_palette_main(LV_PALETTE_RED));
        return;
    }

    audio_playlist_count = 0U;
    audio_playlist_index = 0U;
    if(audio_playlist != NULL) {
        myfree(SRAMEX, audio_playlist);
        audio_playlist = NULL;
    }
    audio_playlist = mymalloc(SRAMEX,
                              DESKTOP_AUDIO_PLAYLIST_MAX *
                              sizeof(*audio_playlist));
    if(audio_playlist == NULL) {
        SystemLog_Write(SYSTEM_LOG_OPEN, path, SYSTEM_LOG_ERROR_MEMORY);
        desktop_set_files_status("Not enough memory",
                                 lv_palette_main(LV_PALETTE_RED));
        return;
    }
    strcpy(audio_directory, files_current_path);
    result = f_opendir(&directory, audio_directory);
    if(result != FR_OK) {
        SystemLog_Write(SYSTEM_LOG_OPEN, path, result);
        desktop_set_files_status(desktop_fatfs_error_text(result),
                                 lv_palette_main(LV_PALETTE_RED));
        myfree(SRAMEX, audio_playlist);
        audio_playlist = NULL;
        return;
    }

    while(audio_playlist_count < DESKTOP_AUDIO_PLAYLIST_MAX) {
        size_t name_length;
        result = f_readdir(&directory, &info);
        if(result != FR_OK || info.fname[0] == '\0') break;
        if((info.fattrib & AM_DIR) != 0U ||
           !desktop_has_extension(info.fname, ".wav")) {
            continue;
        }
        name_length = strlen(info.fname);
        if(name_length > DESKTOP_AUDIO_NAME_MAX) continue;
        strcpy(audio_playlist[audio_playlist_count], info.fname);
        if(strcmp(info.fname, name) == 0) {
            audio_playlist_index = audio_playlist_count;
            selected_found = true;
        }
        audio_playlist_count++;
    }
    (void)f_closedir(&directory);
    if(result != FR_OK) {
        SystemLog_Write(SYSTEM_LOG_OPEN, path, result);
        desktop_set_files_status(desktop_fatfs_error_text(result),
                                 lv_palette_main(LV_PALETTE_RED));
        myfree(SRAMEX, audio_playlist);
        audio_playlist = NULL;
        return;
    }
    if(audio_playlist_count == 0U) {
        SystemLog_Write(SYSTEM_LOG_OPEN, path, FR_NO_FILE);
        desktop_set_files_status("No WAV files in this folder",
                                 lv_palette_main(LV_PALETTE_RED));
        myfree(SRAMEX, audio_playlist);
        audio_playlist = NULL;
        return;
    }
    if(!selected_found) {
        SystemLog_Write(SYSTEM_LOG_OPEN, path, SYSTEM_LOG_ERROR_PLAYLIST);
        desktop_set_files_status("Only the first 32 WAV files are available",
                                 lv_palette_main(LV_PALETTE_RED));
        myfree(SRAMEX, audio_playlist);
        audio_playlist = NULL;
        return;
    }

    page = desktop_open_page("Audio Player", NULL, NULL);
    if(page == NULL) {
        SystemLog_Write(SYSTEM_LOG_OPEN, path, SYSTEM_LOG_ERROR_UI);
        return;
    }
    SystemLog_Write(SYSTEM_LOG_OPEN, path, FR_OK);
    lv_obj_add_event_cb(page,
                        desktop_audio_page_delete_event_cb,
                        LV_EVENT_DELETE,
                        NULL);

    icon = lv_label_create(page);
    lv_label_set_text(icon, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(icon, lv_color_hex(0x2878C7), LV_PART_MAIN);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 92);

    audio_track_label = lv_label_create(page);
    lv_obj_set_width(audio_track_label, 280);
    lv_label_set_long_mode(audio_track_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(audio_track_label,
                                LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(audio_track_label,
                                lv_color_hex(0x173B32),
                                LV_PART_MAIN);
    lv_obj_align(audio_track_label, LV_ALIGN_TOP_MID, 0, 172);

    audio_index_label = lv_label_create(page);
    lv_obj_set_width(audio_index_label, 120);
    lv_obj_set_style_text_align(audio_index_label,
                                LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_obj_align(audio_index_label, LV_ALIGN_TOP_MID, 0, 208);

    audio_state_label = lv_label_create(page);
    lv_obj_set_width(audio_state_label, 240);
    lv_obj_set_style_text_align(audio_state_label,
                                LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_obj_align(audio_state_label, LV_ALIGN_TOP_MID, 0, 250);

    (void)desktop_audio_create_button(page,
                                      30,
                                      LV_SYMBOL_PREV,
                                      desktop_audio_change_track_event_cb,
                                      (void *)(intptr_t)-1);
    audio_play_label = desktop_audio_create_button(page,
                                                    98,
                                                    LV_SYMBOL_PLAY,
                                                    desktop_audio_play_event_cb,
                                                    NULL);
    (void)desktop_audio_create_button(page,
                                      166,
                                      LV_SYMBOL_STOP,
                                      desktop_audio_stop_event_cb,
                                      NULL);
    (void)desktop_audio_create_button(page,
                                      234,
                                      LV_SYMBOL_NEXT,
                                      desktop_audio_change_track_event_cb,
                                      (void *)(intptr_t)1);

    desktop_audio_update_selection();
    desktop_audio_set_state(AUDIO_PLAYER_IDLE, 0U);
    audio_status_timer = lv_timer_create(desktop_audio_status_timer_cb,
                                         DESKTOP_AUDIO_STATUS_REFRESH_MS,
                                         NULL);
}

static void desktop_set_text_editor_status(const char * text, lv_color_t color)
{
    if(text_editor_status == NULL) return;
    lv_label_set_text(text_editor_status, text);
    lv_obj_set_style_text_color(text_editor_status, color, LV_PART_MAIN);
}

static void desktop_text_editor_changed_event_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || text_editor_loading) {
        return;
    }

    text_editor_modified = true;
    desktop_set_text_editor_status("Unsaved changes", lv_color_hex(0x8A6500));
}

static void desktop_text_editor_save_event_cb(lv_event_t * e)
{
    const char * text;
    size_t length;
    FIL file;
    FRESULT result;
    UINT bytes_written = 0U;

    if(lv_event_get_code(e) != LV_EVENT_CLICKED ||
       text_editor == NULL || text_editor_path[0] == '\0') {
        return;
    }

    if(!text_editor_modified) {
        desktop_set_text_editor_status("No changes", lv_color_hex(0x294A42));
        return;
    }

    text = lv_textarea_get_text(text_editor);
    length = strlen(text);
    desktop_set_text_editor_status("Saving...", lv_color_hex(0x294A42));
    lv_refr_now(NULL);

    result = f_open(&file, text_editor_path, FA_CREATE_ALWAYS | FA_WRITE);
    if(result == FR_OK) {
        result = f_write(&file, text, (UINT)length, &bytes_written);
        if(result == FR_OK && bytes_written != (UINT)length) result = FR_DISK_ERR;
        if(result == FR_OK) result = f_sync(&file);
        {
            FRESULT close_result = f_close(&file);
            if(result == FR_OK) result = close_result;
        }
    }

    SystemLog_Write(SYSTEM_LOG_SAVE, text_editor_path, result);
    if(result == FR_OK) {
        text_editor_modified = false;
        desktop_set_text_editor_status("Saved",
                                       lv_palette_main(LV_PALETTE_GREEN));
    }
    else {
        desktop_set_text_editor_status(desktop_fatfs_error_text(result),
                                       lv_palette_main(LV_PALETTE_RED));
    }
}

static void desktop_open_text_file(const char * path, const char * name)
{
    FIL file;
    FRESULT result;
    UINT bytes_read = 0U;
    uint32_t file_size;
    char * buffer;
    lv_obj_t * page;
    lv_obj_t * save_button;
    lv_obj_t * save_label;

    result = f_open(&file, path, FA_READ);
    if(result != FR_OK) {
        SystemLog_Write(SYSTEM_LOG_OPEN, path, result);
        desktop_set_files_status(desktop_fatfs_error_text(result),
                                 lv_palette_main(LV_PALETTE_RED));
        return;
    }

    file_size = (uint32_t)f_size(&file);
    if(file_size > DESKTOP_TEXT_FILE_MAX) {
        (void)f_close(&file);
        SystemLog_Write(SYSTEM_LOG_OPEN, path, SYSTEM_LOG_ERROR_TOO_LARGE);
        desktop_set_files_status("File is larger than 8 KB",
                                 lv_palette_main(LV_PALETTE_RED));
        return;
    }

    buffer = (char *)mymalloc(SRAMEX, file_size + 1U);
    if(buffer == NULL) {
        (void)f_close(&file);
        SystemLog_Write(SYSTEM_LOG_OPEN, path, SYSTEM_LOG_ERROR_MEMORY);
        desktop_set_files_status("Not enough memory",
                                 lv_palette_main(LV_PALETTE_RED));
        return;
    }

    result = f_read(&file, buffer, (UINT)file_size, &bytes_read);
    {
        FRESULT close_result = f_close(&file);
        if(result == FR_OK) result = close_result;
    }
    if(result != FR_OK || bytes_read != (UINT)file_size) {
        SystemLog_Write(SYSTEM_LOG_OPEN, path,
                        result == FR_OK ? FR_DISK_ERR : result);
        myfree(SRAMEX, buffer);
        desktop_set_files_status(result == FR_OK
                                 ? "File read was incomplete"
                                 : desktop_fatfs_error_text(result),
                                 lv_palette_main(LV_PALETTE_RED));
        return;
    }
    buffer[file_size] = '\0';

    page = desktop_open_page(name, LV_SYMBOL_FILE, NULL);
    if(page == NULL) {
        SystemLog_Write(SYSTEM_LOG_OPEN, path, SYSTEM_LOG_ERROR_UI);
        myfree(SRAMEX, buffer);
        return;
    }

    strcpy(text_editor_path, path);
    text_editor_modified = false;

    save_button = lv_button_create(page);
    lv_obj_set_size(save_button, 86, 38);
    lv_obj_align(save_button, LV_ALIGN_TOP_RIGHT, -8, 50);
    lv_obj_set_style_radius(save_button, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(save_button, lv_color_hex(0x2878C7), LV_PART_MAIN);
    lv_obj_add_event_cb(save_button,
                        desktop_text_editor_save_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);

    save_label = lv_label_create(save_button);
    lv_label_set_text(save_label, LV_SYMBOL_SAVE " Save");
    lv_obj_center(save_label);

    text_editor_status = lv_label_create(page);
    lv_obj_set_width(text_editor_status, 208);
    lv_label_set_long_mode(text_editor_status, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(text_editor_status,
                                lv_color_hex(0x294A42),
                                LV_PART_MAIN);
    lv_obj_align(text_editor_status, LV_ALIGN_TOP_LEFT, 8, 61);
    lv_label_set_text(text_editor_status, "Ready");

    text_editor = lv_textarea_create(page);
    lv_obj_set_size(text_editor, 304, 374);
    lv_obj_align(text_editor, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_textarea_set_one_line(text_editor, false);
    lv_textarea_set_max_length(text_editor, DESKTOP_TEXT_FILE_MAX);
    lv_obj_add_event_cb(text_editor,
                        desktop_text_editor_changed_event_cb,
                        LV_EVENT_VALUE_CHANGED,
                        NULL);
    text_editor_loading = true;
    lv_textarea_set_text(text_editor, buffer);
    text_editor_loading = false;
    myfree(SRAMEX, buffer);

    folder_input_group = lv_group_create();
    lv_group_add_obj(folder_input_group, text_editor);
    lv_group_add_obj(folder_input_group, save_button);
    if(indev_keypad != NULL) {
        lv_indev_set_group(indev_keypad, folder_input_group);
    }
    lv_group_focus_obj(text_editor);
    KeyboardInput_Clear();
    SystemLog_Write(SYSTEM_LOG_OPEN, path, FR_OK);
}

static void desktop_file_select_event_cb(lv_event_t * e)
{
    lv_obj_t * row;

    if(lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED) return;

    row = (lv_obj_t *)lv_event_get_user_data(e);
    if(row == NULL) return;

    lv_obj_set_state(row, LV_STATE_CHECKED,
                     !lv_obj_has_state(row, LV_STATE_CHECKED));
}

static void desktop_file_open_event_cb(lv_event_t * e)
{
    lv_obj_t * name_label;
    const char * name;
    char path[DESKTOP_FILES_PATH_MAX];
    char display_name[DESKTOP_FILES_PATH_MAX];

    if(lv_event_get_code(e) != LV_EVENT_DOUBLE_CLICKED) return;

    name_label = (lv_obj_t *)lv_event_get_user_data(e);
    if(name_label == NULL) return;
    name = lv_label_get_text(name_label);
    if(!desktop_build_child_path(name, path, sizeof(path))) {
        desktop_set_files_status("Path is too long",
                                 lv_palette_main(LV_PALETTE_RED));
        return;
    }
    strcpy(display_name, name);
    SystemLog_Write(SYSTEM_LOG_OPEN_REQUEST, path, 0U);

    if(desktop_has_extension(path, ".wav")) {
        desktop_open_audio_file(display_name, path);
    }
    else if(desktop_has_extension(path, ".bmp")) {
        lv_obj_t * page = desktop_open_page(display_name, LV_SYMBOL_IMAGE, NULL);
        if(page != NULL && !DrawingApp_CreateFromFile(page, path)) {
            lv_obj_t * label = lv_label_create(page);
            lv_label_set_text(label, "Unable to open drawing");
            lv_obj_center(label);
        }
        else if(page == NULL) {
            SystemLog_Write(SYSTEM_LOG_OPEN, path, SYSTEM_LOG_ERROR_UI);
        }
    }
    else {
        desktop_open_text_file(path, display_name);
    }
}

static void desktop_folder_open_event_cb(lv_event_t * e)
{
    lv_obj_t * name_label;
    const char * name;
    char next_path[DESKTOP_FILES_PATH_MAX];
    char previous_path[DESKTOP_FILES_PATH_MAX];
    FRESULT result;

    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    name_label = (lv_obj_t *)lv_event_get_user_data(e);
    if(name_label == NULL) return;

    name = lv_label_get_text(name_label);
    if(!desktop_build_child_path(name, next_path, sizeof(next_path))) {
        desktop_set_files_status("Path is too long",
                                 lv_palette_main(LV_PALETTE_RED));
        return;
    }

    strcpy(previous_path, files_current_path);
    strcpy(files_current_path, next_path);
    desktop_set_files_status("", lv_color_hex(0x294A42));
    result = desktop_files_refresh();
    if(result != FR_OK) {
        strcpy(files_current_path, previous_path);
        (void)desktop_files_refresh();
        desktop_set_files_status(desktop_fatfs_error_text(result),
                                 lv_palette_main(LV_PALETTE_RED));
    }
}

static void desktop_request_delete(lv_event_t * e, bool is_directory)
{
    lv_obj_t * name_label;
    const char * name;

    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    name_label = (lv_obj_t *)lv_event_get_user_data(e);
    if(name_label == NULL) return;

    name = lv_label_get_text(name_label);
    desktop_open_delete_dialog(name, is_directory);
}

static void desktop_delete_folder_event_cb(lv_event_t * e)
{
    desktop_request_delete(e, true);
}

static void desktop_delete_file_event_cb(lv_event_t * e)
{
    desktop_request_delete(e, false);
}

static void desktop_add_file_row(const FILINFO * info)
{
    bool is_directory;
    lv_obj_t * row;
    lv_obj_t * open_area;
    lv_obj_t * icon_label;
    lv_obj_t * name_label;
    lv_obj_t * delete_button;
    lv_obj_t * delete_label;

    is_directory = (info->fattrib & AM_DIR) != 0U;

    row = lv_obj_create(files_list);
    lv_obj_set_size(row, 280, 48);
    lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    open_area = lv_button_create(row);
    lv_obj_set_size(open_area, 232, 44);
    lv_obj_align(open_area, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(open_area, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(open_area, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(open_area, 0, LV_PART_MAIN);

    icon_label = lv_label_create(open_area);
    lv_label_set_text(icon_label,
                      is_directory ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE);
    lv_obj_set_style_text_color(icon_label, lv_color_hex(0x294A42), LV_PART_MAIN);
    lv_obj_align(icon_label, LV_ALIGN_LEFT_MID, 4, 0);

    name_label = lv_label_create(open_area);
    lv_label_set_text(name_label, info->fname);
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(name_label, 180);
    lv_obj_set_style_text_color(name_label, lv_color_hex(0x294A42), LV_PART_MAIN);
    lv_obj_align(name_label, LV_ALIGN_LEFT_MID, 34, 0);

    if(is_directory) {
        lv_obj_add_event_cb(open_area,
                            desktop_folder_open_event_cb,
                            LV_EVENT_CLICKED,
                            name_label);
    }
    else {
        lv_obj_set_style_bg_color(row, lv_color_hex(0xB7CFC3),
                                  LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER,
                                LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_add_event_cb(open_area,
                            desktop_file_select_event_cb,
                            LV_EVENT_SHORT_CLICKED,
                            row);
        lv_obj_add_event_cb(open_area,
                            desktop_file_open_event_cb,
                            LV_EVENT_DOUBLE_CLICKED,
                            name_label);
    }

    delete_button = lv_button_create(row);
    lv_obj_set_size(delete_button, 42, 42);
    lv_obj_align(delete_button, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(delete_button, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(delete_button,
                              lv_palette_main(LV_PALETTE_RED),
                              LV_PART_MAIN);
    lv_obj_add_event_cb(delete_button,
                        is_directory
                            ? desktop_delete_folder_event_cb
                            : desktop_delete_file_event_cb,
                        LV_EVENT_CLICKED,
                        name_label);

    delete_label = lv_label_create(delete_button);
    lv_label_set_text(delete_label, LV_SYMBOL_TRASH);
    lv_obj_center(delete_label);
}

static FRESULT desktop_files_refresh(void)
{
    DIR directory;
    FILINFO info;
    FRESULT result;
    uint32_t item_count = 0U;

    if(files_list == NULL) return FR_INVALID_OBJECT;

    desktop_update_path_label();
    lv_obj_clean(files_list);
    result = f_opendir(&directory, files_current_path);
    if(result != FR_OK) {
        (void)lv_list_add_text(files_list, desktop_fatfs_error_text(result));
        desktop_set_files_status(desktop_fatfs_error_text(result),
                                 lv_palette_main(LV_PALETTE_RED));
        return result;
    }

    while(1) {
        result = f_readdir(&directory, &info);
        if(result != FR_OK || info.fname[0] == '\0') break;

        if(strcmp(info.fname, ".") == 0 || strcmp(info.fname, "..") == 0) {
            continue;
        }

        desktop_add_file_row(&info);
        item_count++;
    }

    (void)f_closedir(&directory);

    if(result != FR_OK) {
        desktop_set_files_status(desktop_fatfs_error_text(result),
                                 lv_palette_main(LV_PALETTE_RED));
        return result;
    }

    if(item_count == 0U) {
        (void)lv_list_add_text(files_list, "No files or folders");
    }

    return FR_OK;
}

static const char * desktop_validate_entry_name(const char * name)
{
    const char * invalid_chars = "/\\:*?\"<>|";
    size_t length;
    size_t index;

    if(name == NULL || name[0] == '\0') return "Enter a name";

    length = strlen(name);
    if(length > DESKTOP_ENTRY_NAME_MAX) return "Name is too long";
    if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return "This name is reserved";
    }
    if(name[0] == ' ' || name[length - 1U] == ' ' ||
       name[length - 1U] == '.') {
        return "No leading/trailing space or dot";
    }

    for(index = 0U; index < length; index++) {
        unsigned char character = (unsigned char)name[index];

        if(character < 0x20U || strchr(invalid_chars, name[index]) != NULL) {
            return "Name contains an invalid character";
        }
    }

    return NULL;
}

static void desktop_close_folder_dialog(void)
{
    if(folder_dialog != NULL) {
        lv_obj_delete(folder_dialog);
    }
}

static void desktop_create_entry(void)
{
    const char * name;
    const char * validation_error;
    char path[DESKTOP_FILES_PATH_MAX];
    FILINFO info;
    FIL new_file;
    FRESULT result;

    if(folder_name_input == NULL) return;

    name = lv_textarea_get_text(folder_name_input);
    validation_error = desktop_validate_entry_name(name);
    if(validation_error != NULL) {
        lv_label_set_text(folder_dialog_status, validation_error);
        return;
    }
    if(!desktop_build_child_path(name, path, sizeof(path))) {
        lv_label_set_text(folder_dialog_status, "Path is too long");
        return;
    }

    result = f_stat(path, &info);
    if(result == FR_OK || result == FR_EXIST) {
        SystemLog_Write(create_folder_mode ? SYSTEM_LOG_CREATE_FOLDER
                                          : SYSTEM_LOG_CREATE_FILE, path, FR_EXIST);
        lv_label_set_text(folder_dialog_status, "A file or folder with this name exists");
        return;
    }
    if(result != FR_NO_FILE) {
        SystemLog_Write(create_folder_mode ? SYSTEM_LOG_CREATE_FOLDER
                                          : SYSTEM_LOG_CREATE_FILE, path, result);
        lv_label_set_text(folder_dialog_status, desktop_fatfs_error_text(result));
        return;
    }

    if(create_folder_mode) {
        result = f_mkdir(path);
    }
    else {
        result = f_open(&new_file, path, FA_CREATE_NEW | FA_WRITE);
        if(result == FR_OK) {
            result = f_close(&new_file);
        }
    }

    SystemLog_Write(create_folder_mode ? SYSTEM_LOG_CREATE_FOLDER
                                      : SYSTEM_LOG_CREATE_FILE, path, result);
    if(result == FR_EXIST) {
        lv_label_set_text(folder_dialog_status, "A file or folder with this name exists");
        return;
    }
    if(result != FR_OK &&
       desktop_entry_state_matches(path, true, create_folder_mode)) {
        result = FR_OK;
    }
    if(result != FR_OK) {
        lv_label_set_text(folder_dialog_status, desktop_fatfs_error_text(result));
        return;
    }

    desktop_close_folder_dialog();
    if(desktop_files_refresh() == FR_OK) {
        desktop_set_files_status(create_folder_mode
                                     ? "Folder created"
                                     : "File created",
                                 lv_palette_main(LV_PALETTE_GREEN));
    }
}

static void desktop_entry_create_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_CLICKED || code == LV_EVENT_READY) {
        desktop_create_entry();
    }
}

static void desktop_entry_cancel_event_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        desktop_close_folder_dialog();
    }
}

static void desktop_entry_input_changed_event_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED &&
       folder_dialog_status != NULL) {
        lv_label_set_text(folder_dialog_status, "");
    }
}

static void desktop_open_create_dialog(bool is_folder)
{
    lv_obj_t * panel;
    lv_obj_t * title_label;
    lv_obj_t * create_button;
    lv_obj_t * create_label;
    lv_obj_t * cancel_button;
    lv_obj_t * cancel_label;

    if(active_page == NULL || folder_dialog != NULL || delete_dialog != NULL) {
        return;
    }

    create_folder_mode = is_folder;
    folder_dialog = lv_obj_create(active_page);
    lv_obj_set_size(folder_dialog, LV_PCT(100), LV_PCT(100));
    lv_obj_center(folder_dialog);
    lv_obj_set_style_radius(folder_dialog, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(folder_dialog, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(folder_dialog, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(folder_dialog, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(folder_dialog, LV_OPA_70, LV_PART_MAIN);
    lv_obj_remove_flag(folder_dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(folder_dialog, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(folder_dialog,
                        desktop_folder_dialog_delete_event_cb,
                        LV_EVENT_DELETE,
                        NULL);

    panel = lv_obj_create(folder_dialog);
    lv_obj_set_size(panel, 280, 220);
    lv_obj_center(panel);
    lv_obj_set_style_radius(panel, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 12, LV_PART_MAIN);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    title_label = lv_label_create(panel);
    lv_label_set_text(title_label,
                      is_folder ? "Create folder" : "Create empty file");
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    folder_name_input = lv_textarea_create(panel);
    lv_textarea_set_one_line(folder_name_input, true);
    lv_textarea_set_max_length(folder_name_input, DESKTOP_ENTRY_NAME_MAX);
    lv_textarea_set_placeholder_text(folder_name_input,
                                     is_folder ? "Folder name" : "File name.ext");
    lv_obj_set_size(folder_name_input, 252, 42);
    lv_obj_align(folder_name_input, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_add_event_cb(folder_name_input,
                        desktop_entry_input_changed_event_cb,
                        LV_EVENT_VALUE_CHANGED,
                        NULL);
    lv_obj_add_event_cb(folder_name_input,
                        desktop_entry_create_event_cb,
                        LV_EVENT_READY,
                        NULL);

    folder_dialog_status = lv_label_create(panel);
    lv_label_set_text(folder_dialog_status, "");
    lv_obj_set_width(folder_dialog_status, 252);
    lv_obj_set_style_text_align(folder_dialog_status,
                                LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(folder_dialog_status,
                                lv_palette_main(LV_PALETTE_RED),
                                LV_PART_MAIN);
    lv_obj_align(folder_dialog_status, LV_ALIGN_TOP_MID, 0, 85);

    create_button = lv_button_create(panel);
    lv_obj_set_size(create_button, 112, 42);
    lv_obj_align(create_button, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_event_cb(create_button,
                        desktop_entry_create_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);

    create_label = lv_label_create(create_button);
    lv_label_set_text(create_label, LV_SYMBOL_OK " Create");
    lv_obj_center(create_label);

    cancel_button = lv_button_create(panel);
    lv_obj_set_size(cancel_button, 112, 42);
    lv_obj_align(cancel_button, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(cancel_button,
                        desktop_entry_cancel_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);

    cancel_label = lv_label_create(cancel_button);
    lv_label_set_text(cancel_label, LV_SYMBOL_CLOSE " Cancel");
    lv_obj_center(cancel_label);

    folder_input_group = lv_group_create();
    lv_group_add_obj(folder_input_group, folder_name_input);
    lv_group_add_obj(folder_input_group, create_button);
    lv_group_add_obj(folder_input_group, cancel_button);
    if(indev_keypad != NULL) {
        lv_indev_set_group(indev_keypad, folder_input_group);
    }
    lv_group_focus_obj(folder_name_input);
    KeyboardInput_Clear();
}

static void desktop_new_folder_event_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        desktop_open_create_dialog(true);
    }
}

static void desktop_new_file_event_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        desktop_open_create_dialog(false);
    }
}

static void desktop_back_event_cb(lv_event_t * e)
{
    char * separator;

    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if(strcmp(files_current_path, DESKTOP_FILES_ROOT) == 0) return;

    separator = strrchr(files_current_path, '/');
    if(separator == NULL || separator <= files_current_path + 2) {
        strcpy(files_current_path, DESKTOP_FILES_ROOT);
    }
    else {
        *separator = '\0';
    }

    desktop_set_files_status("", lv_color_hex(0x294A42));
    (void)desktop_files_refresh();
}

static void desktop_close_delete_dialog(void)
{
    if(delete_dialog != NULL) {
        lv_obj_delete(delete_dialog);
    }
}

static void desktop_delete_confirm_event_cb(lv_event_t * e)
{
    FRESULT result;

    if(lv_event_get_code(e) != LV_EVENT_CLICKED || delete_dialog == NULL) return;

    result = f_unlink(delete_target_path);
    if(result != FR_OK &&
       desktop_entry_state_matches(delete_target_path,
                                   false,
                                   delete_target_is_directory)) {
        result = FR_OK;
    }
    if(result == FR_OK) {
        bool deleted_directory = delete_target_is_directory;

        desktop_close_delete_dialog();
        if(desktop_files_refresh() == FR_OK) {
            desktop_set_files_status(deleted_directory
                                         ? "Folder deleted"
                                         : "File deleted",
                                     lv_palette_main(LV_PALETTE_GREEN));
        }
        return;
    }

    if(delete_target_is_directory && result == FR_DENIED) {
        lv_label_set_text(delete_dialog_status, "Folder must be empty before deletion");
    }
    else {
        lv_label_set_text(delete_dialog_status, desktop_fatfs_error_text(result));
    }
}

static void desktop_delete_cancel_event_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        desktop_close_delete_dialog();
    }
}

static void desktop_open_delete_dialog(const char * name, bool is_directory)
{
    lv_obj_t * panel;
    lv_obj_t * prompt_label;
    lv_obj_t * delete_button;
    lv_obj_t * delete_label;
    lv_obj_t * cancel_button;
    lv_obj_t * cancel_label;

    if(active_page == NULL || folder_dialog != NULL || delete_dialog != NULL) {
        return;
    }
    if(!desktop_build_child_path(name,
                                 delete_target_path,
                                 sizeof(delete_target_path))) {
        desktop_set_files_status("Path is too long",
                                 lv_palette_main(LV_PALETTE_RED));
        return;
    }

    delete_target_is_directory = is_directory;
    delete_dialog = lv_obj_create(active_page);
    lv_obj_set_size(delete_dialog, LV_PCT(100), LV_PCT(100));
    lv_obj_center(delete_dialog);
    lv_obj_set_style_radius(delete_dialog, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(delete_dialog, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(delete_dialog, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(delete_dialog, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(delete_dialog, LV_OPA_70, LV_PART_MAIN);
    lv_obj_remove_flag(delete_dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(delete_dialog, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(delete_dialog,
                        desktop_delete_dialog_delete_event_cb,
                        LV_EVENT_DELETE,
                        NULL);

    panel = lv_obj_create(delete_dialog);
    lv_obj_set_size(panel, 280, 220);
    lv_obj_center(panel);
    lv_obj_set_style_radius(panel, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 12, LV_PART_MAIN);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    prompt_label = lv_label_create(panel);
    lv_obj_set_width(prompt_label, 252);
    lv_label_set_long_mode(prompt_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(prompt_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text_fmt(prompt_label,
                          is_directory ? "Delete folder?\n%s" : "Delete file?\n%s",
                          name);
    lv_obj_align(prompt_label, LV_ALIGN_TOP_MID, 0, 8);

    delete_dialog_status = lv_label_create(panel);
    lv_label_set_text(delete_dialog_status, "");
    lv_obj_set_width(delete_dialog_status, 252);
    lv_obj_set_style_text_align(delete_dialog_status,
                                LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(delete_dialog_status,
                                lv_palette_main(LV_PALETTE_RED),
                                LV_PART_MAIN);
    lv_obj_align(delete_dialog_status, LV_ALIGN_TOP_MID, 0, 92);

    delete_button = lv_button_create(panel);
    lv_obj_set_size(delete_button, 112, 42);
    lv_obj_align(delete_button, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(delete_button,
                              lv_palette_main(LV_PALETTE_RED),
                              LV_PART_MAIN);
    lv_obj_add_event_cb(delete_button,
                        desktop_delete_confirm_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);

    delete_label = lv_label_create(delete_button);
    lv_label_set_text(delete_label, LV_SYMBOL_TRASH " Delete");
    lv_obj_center(delete_label);

    cancel_button = lv_button_create(panel);
    lv_obj_set_size(cancel_button, 112, 42);
    lv_obj_align(cancel_button, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(cancel_button,
                        desktop_delete_cancel_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);

    cancel_label = lv_label_create(cancel_button);
    lv_label_set_text(cancel_label, LV_SYMBOL_CLOSE " Cancel");
    lv_obj_center(cancel_label);

    folder_input_group = lv_group_create();
    lv_group_add_obj(folder_input_group, delete_button);
    lv_group_add_obj(folder_input_group, cancel_button);
    if(indev_keypad != NULL) {
        lv_indev_set_group(indev_keypad, folder_input_group);
    }
    lv_group_focus_obj(cancel_button);
    KeyboardInput_Clear();
}

static void desktop_create_files_page(lv_obj_t * page)
{
    lv_obj_t * back_button;
    lv_obj_t * back_label;
    lv_obj_t * new_folder_button;
    lv_obj_t * new_folder_label;
    lv_obj_t * new_file_button;
    lv_obj_t * new_file_label;

    strcpy(files_current_path, DESKTOP_FILES_ROOT);

    back_button = lv_button_create(page);
    lv_obj_set_size(back_button, 38, 38);
    lv_obj_align(back_button, LV_ALIGN_TOP_LEFT, 8, 48);
    lv_obj_set_style_radius(back_button, 4, LV_PART_MAIN);
    lv_obj_add_event_cb(back_button,
                        desktop_back_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);

    back_label = lv_label_create(back_button);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_center(back_label);

    new_folder_button = lv_button_create(page);
    lv_obj_set_size(new_folder_button, 116, 38);
    lv_obj_align(new_folder_button, LV_ALIGN_TOP_LEFT, 52, 48);
    lv_obj_set_style_radius(new_folder_button, 4, LV_PART_MAIN);
    lv_obj_add_event_cb(new_folder_button,
                        desktop_new_folder_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);

    new_folder_label = lv_label_create(new_folder_button);
    lv_label_set_text(new_folder_label, LV_SYMBOL_PLUS " Folder");
    lv_obj_center(new_folder_label);

    new_file_button = lv_button_create(page);
    lv_obj_set_size(new_file_button, 104, 38);
    lv_obj_align(new_file_button, LV_ALIGN_TOP_RIGHT, -8, 48);
    lv_obj_set_style_radius(new_file_button, 4, LV_PART_MAIN);
    lv_obj_add_event_cb(new_file_button,
                        desktop_new_file_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);

    new_file_label = lv_label_create(new_file_button);
    lv_label_set_text(new_file_label, LV_SYMBOL_PLUS " File");
    lv_obj_center(new_file_label);

    files_path_label = lv_label_create(page);
    lv_label_set_text(files_path_label, files_current_path);
    lv_label_set_long_mode(files_path_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_width(files_path_label, 300);
    lv_obj_set_style_text_color(files_path_label,
                                lv_color_hex(0x294A42),
                                LV_PART_MAIN);
    lv_obj_align(files_path_label, LV_ALIGN_TOP_MID, 0, 94);

    files_status_label = lv_label_create(page);
    lv_label_set_text(files_status_label, "");
    lv_obj_set_width(files_status_label, 300);
    lv_obj_set_style_text_align(files_status_label,
                                LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_obj_align(files_status_label, LV_ALIGN_TOP_MID, 0, 116);

    files_list = lv_list_create(page);
    lv_obj_set_size(files_list, 300, 330);
    lv_obj_align(files_list, LV_ALIGN_BOTTOM_MID, 0, -10);

    (void)desktop_files_refresh();
}

static void desktop_files_event_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        lv_obj_t * page = desktop_open_page("Files", NULL, NULL);

        if(page != NULL) {
            desktop_create_files_page(page);
        }
    }
}

static void desktop_drawing_event_cb(lv_event_t * e)
{
    lv_obj_t * page;

    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    page = desktop_open_page("Paint", NULL, NULL);
    if(page != NULL) {
        (void)DrawingApp_Create(page);
    }
}

static void desktop_settings_event_cb(lv_event_t * e)
{
    lv_obj_t * page;
    lv_obj_t * label;
    lv_obj_t * value_label;
    lv_obj_t * apply_button;
    lv_obj_t * cancel_button;
    const SystemSettingsValues * values;

    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    page = desktop_open_page("Settings", NULL, NULL);
    if(page == NULL) return;

    values = SystemSettings_Get();

    label = lv_label_create(page);
    lv_label_set_text(label, "Mouse sensitivity");
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 24, 72);

    value_label = lv_label_create(page);
    lv_label_set_text_fmt(value_label, "%u", values->mouse_sensitivity);
    lv_obj_align(value_label, LV_ALIGN_TOP_RIGHT, -24, 72);

    settings_mouse_slider = lv_slider_create(page);
    lv_obj_set_size(settings_mouse_slider, 300, 16);
    lv_obj_align(settings_mouse_slider, LV_ALIGN_TOP_MID, 0, 108);
    lv_slider_set_range(settings_mouse_slider,
                        SYSTEM_SETTINGS_MOUSE_MIN,
                        SYSTEM_SETTINGS_MOUSE_MAX);
    lv_slider_set_value(settings_mouse_slider,
                        values->mouse_sensitivity,
                        LV_ANIM_OFF);
    lv_obj_add_event_cb(settings_mouse_slider,
                        desktop_settings_slider_event_cb,
                        LV_EVENT_VALUE_CHANGED,
                        value_label);

    label = lv_label_create(page);
    lv_label_set_text(label, "Screen brightness");
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 24, 166);

    value_label = lv_label_create(page);
    lv_label_set_text_fmt(value_label, "%u%%", values->brightness);
    lv_obj_align(value_label, LV_ALIGN_TOP_RIGHT, -24, 166);

    settings_brightness_slider = lv_slider_create(page);
    lv_obj_set_size(settings_brightness_slider, 300, 16);
    lv_obj_align(settings_brightness_slider, LV_ALIGN_TOP_MID, 0, 202);
    lv_slider_set_range(settings_brightness_slider,
                        SYSTEM_SETTINGS_BRIGHTNESS_MIN,
                        SYSTEM_SETTINGS_BRIGHTNESS_MAX);
    lv_slider_set_value(settings_brightness_slider,
                        values->brightness,
                        LV_ANIM_OFF);
    lv_obj_add_event_cb(settings_brightness_slider,
                        desktop_settings_percent_slider_event_cb,
                        LV_EVENT_VALUE_CHANGED,
                        value_label);

    label = lv_label_create(page);
    lv_label_set_text(label, "Volume");
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 24, 260);

    value_label = lv_label_create(page);
    lv_label_set_text_fmt(value_label, "%u%%", values->volume);
    lv_obj_align(value_label, LV_ALIGN_TOP_RIGHT, -24, 260);

    settings_volume_slider = lv_slider_create(page);
    lv_obj_set_size(settings_volume_slider, 300, 16);
    lv_obj_align(settings_volume_slider, LV_ALIGN_TOP_MID, 0, 296);
    lv_slider_set_range(settings_volume_slider,
                        SYSTEM_SETTINGS_VOLUME_MIN,
                        SYSTEM_SETTINGS_VOLUME_MAX);
    lv_slider_set_value(settings_volume_slider,
                        values->volume,
                        LV_ANIM_OFF);
    lv_obj_add_event_cb(settings_volume_slider,
                        desktop_settings_percent_slider_event_cb,
                        LV_EVENT_VALUE_CHANGED,
                        value_label);

    settings_status_label = lv_label_create(page);
    lv_label_set_text(settings_status_label, "");
    lv_obj_set_width(settings_status_label, 300);
    lv_obj_set_style_text_align(settings_status_label,
                                LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_obj_align(settings_status_label, LV_ALIGN_BOTTOM_MID, 0, -92);

    apply_button = lv_button_create(page);
    lv_obj_set_size(apply_button, 132, 44);
    lv_obj_align(apply_button, LV_ALIGN_BOTTOM_LEFT, 24, -28);
    lv_obj_add_event_cb(apply_button,
                        desktop_settings_apply_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);
    label = lv_label_create(apply_button);
    lv_label_set_text(label, LV_SYMBOL_OK " Apply");
    lv_obj_center(label);

    cancel_button = lv_button_create(page);
    lv_obj_set_size(cancel_button, 132, 44);
    lv_obj_align(cancel_button, LV_ALIGN_BOTTOM_RIGHT, -24, -28);
    lv_obj_add_event_cb(cancel_button,
                        desktop_settings_cancel_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);
    label = lv_label_create(cancel_button);
    lv_label_set_text(label, LV_SYMBOL_CLOSE " Cancel");
    lv_obj_center(label);
}

static void desktop_settings_slider_event_cb(lv_event_t * e)
{
    lv_obj_t * value_label = (lv_obj_t *)lv_event_get_user_data(e);

    if(value_label != NULL) {
        lv_label_set_text_fmt(value_label,
                              "%d",
                              (int)lv_slider_get_value(lv_event_get_target_obj(e)));
    }
    if(settings_status_label != NULL) {
        lv_label_set_text(settings_status_label, "");
    }
}

static void desktop_settings_percent_slider_event_cb(lv_event_t * e)
{
    lv_obj_t * value_label = (lv_obj_t *)lv_event_get_user_data(e);

    if(value_label != NULL) {
        lv_label_set_text_fmt(value_label,
                              "%d%%",
                              (int)lv_slider_get_value(lv_event_get_target_obj(e)));
    }
    if(settings_status_label != NULL) {
        lv_label_set_text(settings_status_label, "");
    }
}

static void desktop_settings_apply_event_cb(lv_event_t * e)
{
    SystemSettingsValues values;

    if(lv_event_get_code(e) != LV_EVENT_CLICKED ||
       settings_mouse_slider == NULL ||
       settings_brightness_slider == NULL ||
       settings_volume_slider == NULL) {
        return;
    }

    values.mouse_sensitivity = (uint8_t)lv_slider_get_value(settings_mouse_slider);
    values.brightness = (uint8_t)lv_slider_get_value(settings_brightness_slider);
    values.volume = (uint8_t)lv_slider_get_value(settings_volume_slider);

    if(SystemSettings_SaveAndApply(&values)) {
        desktop_status_refresh();
        lv_label_set_text(settings_status_label, "Settings saved");
        lv_obj_set_style_text_color(settings_status_label,
                                    lv_palette_main(LV_PALETTE_GREEN),
                                    LV_PART_MAIN);
    }
    else {
        lv_label_set_text(settings_status_label, "Unable to save settings");
        lv_obj_set_style_text_color(settings_status_label,
                                    lv_palette_main(LV_PALETTE_RED),
                                    LV_PART_MAIN);
    }
}

static void desktop_settings_cancel_event_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED && active_page != NULL) {
        lv_obj_delete(active_page);
    }
}

static void desktop_logs_refresh(void)
{
    uint16_t count;
    uint16_t i;
    SystemLogEntry entry;

    if(logs_view == NULL || logs_view->list == NULL || logs_view->status == NULL) return;
    /* Capture the version before reading so a concurrent new entry triggers
     * another refresh. At most eight entries are rendered per page. */
    logs_view->version = SystemLog_GetVersion();
    count = SystemLog_GetCount();
    if(logs_view->offset >= count) logs_view->offset = 0U;
    lv_obj_clean(logs_view->list);
    lv_obj_scroll_to_y(logs_view->list, 0, LV_ANIM_OFF);
    if(!SystemLog_IsReady()) {
        lv_label_set_text(logs_view->status, "Log storage unavailable");
        return;
    }
    if(count == 0U) {
        lv_label_set_text(logs_view->status, "No logs (RAM only)");
        return;
    }
    lv_label_set_text_fmt(logs_view->status, "%u-%u / %u (newest first)",
                          (unsigned int)(logs_view->offset + 1U),
                          (unsigned int)(logs_view->offset + 8U < count
                                         ? logs_view->offset + 8U : count),
                          (unsigned int)count);
    for(i = logs_view->offset; i < count && i < logs_view->offset + 8U; i++) {
        lv_obj_t * label;
        if(!SystemLog_Read(i, &entry)) break;
        label = lv_label_create(logs_view->list);
        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_color(label,
                                    entry.result == 0U ? lv_color_hex(0x173B32)
                                                       : lv_color_hex(0xB02020),
                                    LV_PART_MAIN);
        lv_label_set_text_fmt(label, "%02lu:%02lu:%02lu  %s\n%s\n%s [0x%03X]",
                              (unsigned long)(entry.seconds / 3600U),
                              (unsigned long)((entry.seconds / 60U) % 60U),
                              (unsigned long)(entry.seconds % 60U),
                              SystemLog_ActionText(entry.action), entry.path,
                              entry.result == 0U &&
                              (entry.action == SYSTEM_LOG_OPEN_REQUEST ||
                               entry.action == SYSTEM_LOG_PLAY_REQUEST)
                                ? "Requested" : SystemLog_ResultText(entry.result),
                              (unsigned int)entry.result);
    }
}

static void desktop_logs_timer_cb(lv_timer_t * timer)
{
    (void)timer;
    if(logs_view != NULL && logs_view->version != SystemLog_GetVersion()) desktop_logs_refresh();
}

static void desktop_logs_button_event_cb(lv_event_t * e)
{
    intptr_t action;
    if(lv_event_get_code(e) != LV_EVENT_CLICKED || logs_view == NULL) return;
    action = (intptr_t)lv_event_get_user_data(e);
    if(action == 0) {
        SystemLog_Clear();
        logs_view->offset = 0U;
    }
    else if(action < 0) {
        if(logs_view->offset >= 8U) logs_view->offset -= 8U;
    }
    else if(logs_view->offset + 8U < SystemLog_GetCount()) {
        logs_view->offset += 8U;
    }
    desktop_logs_refresh();
}

static void desktop_logs_event_cb(lv_event_t * e)
{
    lv_obj_t * page;
    uint16_t i;
    static const char * const names[] = {"Newer", "Older", "Clear"};
    static const intptr_t actions[] = {-1, 1, 0};

    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    page = desktop_open_page("File Logs", NULL, NULL);
    if(page == NULL) return;
    logs_view = (DesktopLogsView *)mymalloc(SRAMEX, sizeof(DesktopLogsView));
    if(logs_view == NULL) {
        lv_obj_t * label = lv_label_create(page);
        lv_label_set_text(label, "Not enough memory for log viewer");
        lv_obj_center(label);
        return;
    }
    memset(logs_view, 0, sizeof(*logs_view));
    for(i = 0U; i < 3U; i++) {
        lv_obj_t * button = lv_button_create(page);
        lv_obj_t * label;
        lv_obj_set_pos(button, 8 + i * 104, 50);
        lv_obj_set_size(button, 96, 34);
        lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(button, desktop_logs_button_event_cb,
                            LV_EVENT_CLICKED, (void *)actions[i]);
        label = lv_label_create(button);
        lv_label_set_text(label, names[i]);
        lv_obj_center(label);
    }
    logs_view->status = lv_label_create(page);
    lv_obj_set_pos(logs_view->status, 8, 91);
    logs_view->list = lv_obj_create(page);
    lv_obj_set_pos(logs_view->list, 4, 116);
    lv_obj_set_size(logs_view->list, 312, 360);
    lv_obj_set_flex_flow(logs_view->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(logs_view->list, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(logs_view->list, 14, LV_PART_MAIN);
    logs_view->offset = 0U;
    desktop_logs_refresh();
    logs_view->timer = lv_timer_create(desktop_logs_timer_cb, 500U, NULL);
}

lv_obj_t * DesktopUI_AddShortcut(const char * symbol,
                                 const char * label,
                                 int32_t x,
                                 int32_t y,
                                 lv_event_cb_t event_cb,
                                 void * user_data)
{
    lv_obj_t * button;
    lv_obj_t * button_label;

    if(desktop_root == NULL || symbol == NULL || label == NULL) return NULL;

    button = lv_button_create(desktop_root);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, DESKTOP_SHORTCUT_WIDTH, DESKTOP_SHORTCUT_HEIGHT);
    lv_obj_set_style_radius(button, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x173B32), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(0xD6E8DF), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);

    button_label = lv_label_create(button);
    lv_label_set_text_fmt(button_label, "%s\n%s", symbol, label);
    lv_obj_set_style_text_align(button_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(button_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(button_label);

    if(event_cb != NULL) {
        lv_obj_add_event_cb(button, event_cb, LV_EVENT_CLICKED, user_data);
    }

    return button;
}

void DesktopUI_Create(void)
{
    lv_obj_t * background;

    if(desktop_root != NULL) return;

    desktop_root = lv_obj_create(lv_screen_active());
    lv_obj_set_size(desktop_root, LV_PCT(100), LV_PCT(100));
    lv_obj_center(desktop_root);
    lv_obj_set_style_radius(desktop_root, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(desktop_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(desktop_root, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(desktop_root, lv_color_black(), LV_PART_MAIN);
    lv_obj_remove_flag(desktop_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(desktop_root,
                        desktop_root_delete_event_cb,
                        LV_EVENT_DELETE,
                        NULL);

    background = lv_image_create(desktop_root);
    lv_image_set_src(background, &desktop);
    lv_obj_center(background);
    lv_obj_remove_flag(background, LV_OBJ_FLAG_CLICKABLE);

    desktop_create_status_bar();

    (void)DesktopUI_AddShortcut(LV_SYMBOL_DIRECTORY,
                                "Files",
                                14,
                                58,
                                desktop_files_event_cb,
                                NULL);
    (void)DesktopUI_AddShortcut(LV_SYMBOL_SETTINGS,
                                "Settings",
                                100,
                                58,
                                desktop_settings_event_cb,
                                NULL);
    (void)DesktopUI_AddShortcut(LV_SYMBOL_EDIT,
                                "Paint",
                                186,
                                58,
                                desktop_drawing_event_cb,
                                NULL);
    (void)DesktopUI_AddShortcut(LV_SYMBOL_LIST,
                                "Logs",
                                14,
                                144,
                                desktop_logs_event_cb,
                                NULL);

    /* Keep the desktop below the password layer and the input cursor. */
    lv_obj_move_background(desktop_root);
}

void DesktopUI_Destroy(void)
{
    if(desktop_root != NULL) {
        lv_obj_delete(desktop_root);
    }
}

bool DesktopUI_IsCreated(void)
{
    return desktop_root != NULL;
}

lv_obj_t * DesktopUI_GetRoot(void)
{
    return desktop_root;
}
