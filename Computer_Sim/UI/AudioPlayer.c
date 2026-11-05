#include "AudioPlayer.h"
#include "SystemLog.h"

#include "ff.h"
#include "i2s.h"
#include "malloc.h"
#include "wm8978.h"
#include "audioplay.h"

#include <string.h>

#define AUDIO_COMMAND_QUEUE_LENGTH 6U
#define AUDIO_DMA_BUFFER_SIZE WAV_I2S_TX_DMA_BUFSIZE
#define AUDIO_DMA_WAIT_TICKS pdMS_TO_TICKS(1U)
#define AUDIO_DMA_TIMEOUT_MS 1000U

typedef enum {
    AUDIO_COMMAND_PLAY,
    AUDIO_COMMAND_PAUSE,
    AUDIO_COMMAND_RESUME,
    AUDIO_COMMAND_STOP
} AudioCommandType;

typedef struct {
    AudioCommandType type;
    char path[AUDIO_PLAYER_PATH_MAX];
} AudioCommand;

typedef enum {
    AUDIO_RUN_FINISHED,
    AUDIO_RUN_STOPPED,
    AUDIO_RUN_REPLACED,
    AUDIO_RUN_ERROR
} AudioRunResult;

typedef struct {
    uint16_t format;
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint32_t data_offset;
    uint32_t data_size;
} AudioWaveInfo;

/* Retained after playback for Keil Watch. Stage: 1 open, 2 parse, 3/4
 * prefill, 5 DMA init, 6 playing, 7 refill, 8 codec/I2S init,
 * 9 sample-rate setup. Finish: 1 no initial PCM,
 * 2 final DMA buffer, 3 invalid EOF while input data remains,
 * 4 missed DMA completion, 5 no DMA progress, 6 invalid file object,
 * 7 short PCM read. */
typedef struct {
    char path[96];
    AudioWaveInfo wave;
    uint32_t file_size;
    uint32_t stage;
    uint32_t error;
    uint32_t initial_bytes[2];
    uint32_t data_remaining;
    uint32_t dma_interrupts;
    uint32_t finish_reason;
    uint32_t elapsed_ms;
    uint32_t dma_events;
    uint32_t last_completed;
    uint32_t buffer_bytes[2];
    uint32_t last_refill_bytes;
    uint32_t read_requested;
    uint32_t read_actual;
    uint32_t read_result;
    uint32_t file_position;
    uint32_t file_size_at_read;
    uint32_t stack_free_words;
} AudioPlaybackDebug;

volatile AudioPlaybackDebug g_audio_debug;

static QueueHandle_t audio_command_queue;
static QueueHandle_t audio_event_queue;
static StaticQueue_t audio_command_queue_control;
static StaticQueue_t audio_event_queue_control;
static uint8_t audio_event_queue_storage[sizeof(AudioPlayerEvent)];
static uint8_t * audio_command_queue_storage;
static TaskHandle_t audio_task_handle;
static volatile AudioPlayerState audio_state = AUDIO_PLAYER_IDLE;
static bool audio_initialized;

static void audio_report_state(AudioPlayerState state, uint8_t error)
{
    AudioPlayerEvent event;

    audio_state = state;
    event.state = state;
    event.error = error;
    if(audio_event_queue != NULL) {
        (void)xQueueOverwrite(audio_event_queue, &event);
    }
}

static bool audio_send_command(AudioCommandType type, const char * path)
{
    AudioCommand command;

    if(audio_command_queue == NULL) return false;
    memset(&command, 0, sizeof(command));
    command.type = type;
    if(path != NULL) {
        size_t length = strlen(path);
        if(length == 0U || length >= sizeof(command.path)) return false;
        memcpy(command.path, path, length + 1U);
    }
    return xQueueSend(audio_command_queue, &command, 0U) == pdPASS;
}

/* Use the ALIENTEK wavctrl/audiodev and wav_buffill playback core. This
 * adapter only records results and maps them to the LVGL status queue. */
static uint32_t audio_fill_buffer(uint8_t * buffer, uint8_t * error)
{
    uint32_t produced;
    uint32_t position;

    g_audio_debug.file_position = (uint32_t)f_tell(audiodev.file);
    g_audio_debug.file_size_at_read = (uint32_t)f_size(audiodev.file);
    produced = wav_buffill(buffer, AUDIO_DMA_BUFFER_SIZE, (u8)wavctrl.bps);
    g_audio_debug.read_requested = wav_read_info.requested;
    g_audio_debug.read_actual = wav_read_info.actual;
    g_audio_debug.read_result = wav_read_info.result;
    position = (uint32_t)f_tell(audiodev.file);
    if(position >= wavctrl.datastart &&
       position - wavctrl.datastart <= wavctrl.datasize) {
        g_audio_debug.data_remaining = wavctrl.datasize - (position - wavctrl.datastart);
    }
    if(wav_read_info.result != FR_OK) {
        *error = wav_read_info.result == FR_INVALID_OBJECT
                   ? AUDIO_PLAYER_ERROR_FILE : (uint8_t)wav_read_info.result;
        g_audio_debug.finish_reason = 6U;
    }
    else if(wav_read_info.actual != wav_read_info.requested) {
        *error = AUDIO_PLAYER_ERROR_DATA;
        g_audio_debug.finish_reason = 7U;
    }
    return produced;
}

static AudioRunResult audio_run_file(const char * path,
                                     char * replacement_path,
                                     uint32_t replacement_size,
                                     uint8_t * error)
{
    uint8_t * buffers[2];
    uint32_t valid_bytes[2] = {0U, 0U};
    uint32_t started_at = HAL_GetTick();
    uint32_t last_dma_at;
    uint32_t processed_interrupts = 0U;
    uint8_t next_completed = 0U;
    bool file_open = false;
    bool dma_initialized = false;
    bool paused = false;
    bool release_buffers = true;
    FRESULT result;
    AudioRunResult run_result = AUDIO_RUN_ERROR;

    memset((void *)&g_audio_debug, 0, sizeof(g_audio_debug));
    strncpy((char *)g_audio_debug.path, path, sizeof(g_audio_debug.path) - 1U);
    *error = 0U;
    g_audio_debug.stage = 1U;
    /* A previous DMA stop failure retains its buffers until reset. */
    if(audiodev.file != NULL || audiodev.i2sbuf1 != NULL ||
       audiodev.i2sbuf2 != NULL || audiodev.tbuf != NULL) {
        *error = AUDIO_PLAYER_ERROR_DMA;
        g_audio_debug.error = *error;
        return AUDIO_RUN_ERROR;
    }
    audiodev.file = (FIL *)mymalloc(SRAMIN, sizeof(FIL));
    audiodev.i2sbuf1 = (u8 *)mymalloc(SRAMIN, AUDIO_DMA_BUFFER_SIZE);
    audiodev.i2sbuf2 = (u8 *)mymalloc(SRAMIN, AUDIO_DMA_BUFFER_SIZE);
    audiodev.tbuf = (u8 *)mymalloc(SRAMIN, AUDIO_DMA_BUFFER_SIZE);
    if(audiodev.file == NULL || audiodev.i2sbuf1 == NULL ||
       audiodev.i2sbuf2 == NULL || audiodev.tbuf == NULL) {
        *error = FR_NOT_ENOUGH_CORE;
        goto cleanup;
    }
    buffers[0] = audiodev.i2sbuf1;
    buffers[1] = audiodev.i2sbuf2;
    g_audio_debug.stage = 2U;
    result = (FRESULT)wav_decode_init((u8 *)path, &wavctrl);
    if(result != FR_OK) {
        *error = (uint8_t)result;
        goto cleanup;
    }
    g_audio_debug.wave.format = wavctrl.audioformat;
    g_audio_debug.wave.channels = wavctrl.nchannels;
    g_audio_debug.wave.sample_rate = wavctrl.samplerate;
    g_audio_debug.wave.bits_per_sample = wavctrl.bps;
    g_audio_debug.wave.data_offset = wavctrl.datastart;
    g_audio_debug.wave.data_size = wavctrl.datasize;
    result = f_open(audiodev.file, path, FA_READ);
    if(result != FR_OK) {
        *error = (uint8_t)result;
        goto cleanup;
    }
    file_open = true;
    g_audio_debug.file_size = (uint32_t)f_size(audiodev.file);

    g_audio_debug.stage = 8U;
    WM8978_ADDA_Cfg(1U, 0U);
    WM8978_Input_Cfg(0U, 0U, 0U);
    WM8978_Output_Cfg(1U, 0U);
    WM8978_I2S_Cfg(2U, wavctrl.bps == 16U ? 0U : 2U);
    I2S2_Init(I2S_STANDARD_PHILIPS, I2S_MODE_MASTER_TX, I2S_CPOL_LOW,
               wavctrl.bps == 16U ? I2S_DATAFORMAT_16B_EXTENDED : I2S_DATAFORMAT_24B);
    g_audio_debug.stage = 9U;
    if(I2S2_SampleRate_Set(wavctrl.samplerate) != 0U) {
        *error = FR_INVALID_PARAMETER;
        goto cleanup;
    }
    result = f_lseek(audiodev.file, wavctrl.datastart);
    if(result != FR_OK) {
        *error = (uint8_t)result;
        goto cleanup;
    }
    g_audio_debug.stage = 3U;
    valid_bytes[0] = audio_fill_buffer(buffers[0], error);
    g_audio_debug.initial_bytes[0] = valid_bytes[0];
    if(*error != 0U) goto cleanup;
    g_audio_debug.stage = 4U;
    valid_bytes[1] = audio_fill_buffer(buffers[1], error);
    g_audio_debug.initial_bytes[1] = valid_bytes[1];
    if(*error != 0U) goto cleanup;
    if(valid_bytes[0] == 0U) {
        *error = AUDIO_PLAYER_ERROR_DATA;
        g_audio_debug.finish_reason = 1U;
        goto cleanup;
    }

    g_audio_debug.stage = 5U;
    i2s_tx_callback = wav_i2s_dma_tx_callback;
    if(I2S2_TX_DMA_Init(buffers[0], buffers[1], AUDIO_DMA_BUFFER_SIZE / 2U) != 0U) {
        *error = AUDIO_PLAYER_ERROR_DMA;
        goto cleanup;
    }
    dma_initialized = true;
    wavtransferend = 0U;
    wavwitchbuf = 0U;
    wav_dma_completions = 0U;
    g_audio_debug.buffer_bytes[0] = valid_bytes[0];
    g_audio_debug.buffer_bytes[1] = valid_bytes[1];
    last_dma_at = HAL_GetTick();
    audio_start();
    g_audio_debug.stage = 6U;
    audio_report_state(AUDIO_PLAYER_PLAYING, 0U);
    SystemLog_Write(SYSTEM_LOG_PLAY_STARTED, path, 0U);

    for(;;) {
        AudioCommand command;
        uint8_t completed = 0U;
        uint32_t interrupt_count = 0U;
        bool transfer_complete = false;
        bool have_command;

        g_audio_debug.stack_free_words = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
        /* Queue wait replaces the example's while(wavtransferend == 0) spin.
         * Idle/paused waits block indefinitely; active playback checks each ms. */
        have_command = xQueueReceive(audio_command_queue, &command,
                           paused ? portMAX_DELAY : AUDIO_DMA_WAIT_TICKS) == pdPASS;
        if(have_command) {
            if(command.type == AUDIO_COMMAND_STOP) {
                run_result = AUDIO_RUN_STOPPED;
                break;
            }
            if(command.type == AUDIO_COMMAND_PLAY) {
                size_t length = strlen(command.path);
                if(length > 0U && length < replacement_size) {
                    memcpy(replacement_path, command.path, length + 1U);
                    run_result = AUDIO_RUN_REPLACED;
                }
                else *error = FR_INVALID_NAME;
                break;
            }
            if(command.type == AUDIO_COMMAND_PAUSE && !paused) {
                I2S_Play_Stop();
                paused = true;
                audio_report_state(AUDIO_PLAYER_PAUSED, 0U);
            }
            else if(command.type == AUDIO_COMMAND_RESUME && paused) {
                paused = false;
                last_dma_at = HAL_GetTick();
                audio_start();
                audio_report_state(AUDIO_PLAYER_PLAYING, 0U);
            }
        }
        if(paused) continue;

        /* IRQ priority 11 is masked by this short critical section. Take the
         * original flag/index together, then release before any SD access. */
        taskENTER_CRITICAL();
        if(wavtransferend != 0U) {
            transfer_complete = true;
            completed = wavwitchbuf;
            interrupt_count = wav_dma_completions;
            wavtransferend = 0U;
        }
        taskEXIT_CRITICAL();
        if(transfer_complete) {
            g_audio_debug.dma_interrupts = interrupt_count;
            g_audio_debug.dma_events = 1UL << completed;
            if(interrupt_count - processed_interrupts != 1U || completed != next_completed) {
                g_audio_debug.finish_reason = 4U;
                *error = AUDIO_PLAYER_ERROR_BUFFER;
                break;
            }
            processed_interrupts = interrupt_count;
            next_completed ^= 1U;
            last_dma_at = HAL_GetTick();
            g_audio_debug.last_completed = completed;
            /* Track both buffers so the last partial buffer is actually heard
             * before finishing. Natural EOF never selects the next song. */
            if(valid_bytes[completed] < AUDIO_DMA_BUFFER_SIZE) {
                g_audio_debug.finish_reason = 2U;
                run_result = AUDIO_RUN_FINISHED;
                break;
            }
            g_audio_debug.stage = 7U;
            valid_bytes[completed] = audio_fill_buffer(buffers[completed], error);
            g_audio_debug.last_refill_bytes = valid_bytes[completed];
            g_audio_debug.buffer_bytes[completed] = valid_bytes[completed];
            if(*error != 0U) break;
        }
        else if(HAL_GetTick() - last_dma_at >= AUDIO_DMA_TIMEOUT_MS) {
            g_audio_debug.finish_reason = 5U;
            *error = AUDIO_PLAYER_ERROR_DMA;
            break;
        }
    }
cleanup:
    if(dma_initialized) {
        if(I2S2_TX_DMA_DeInit() != 0U) {
            *error = AUDIO_PLAYER_ERROR_DMA;
            run_result = AUDIO_RUN_ERROR;
            release_buffers = false;
        }
    }
    audiodev.status = 0U;
    if(file_open) {
        FRESULT close_result = f_close(audiodev.file);
        if(close_result != FR_OK && *error == 0U) {
            *error = (uint8_t)close_result;
            run_result = AUDIO_RUN_ERROR;
        }
    }
    g_audio_debug.stack_free_words = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    g_audio_debug.error = *error;
    g_audio_debug.elapsed_ms = HAL_GetTick() - started_at;
    if(release_buffers) {
        myfree(SRAMIN, audiodev.file);
        myfree(SRAMIN, audiodev.i2sbuf1);
        myfree(SRAMIN, audiodev.i2sbuf2);
        myfree(SRAMIN, audiodev.tbuf);
        memset(&audiodev, 0, sizeof(audiodev));
    }
    return run_result;
}

void AudioPlayer_Task(void * argument)
{
    AudioCommand command;
    char current_path[AUDIO_PLAYER_PATH_MAX];
    char replacement_path[AUDIO_PLAYER_PATH_MAX];

    (void)argument;
    if(!audio_initialized) {
        vTaskDelete(NULL);
        return;
    }
    audio_task_handle = xTaskGetCurrentTaskHandle();
    for(;;) {
        if(xQueueReceive(audio_command_queue, &command, portMAX_DELAY) != pdPASS) {
            continue;
        }
        if(command.type != AUDIO_COMMAND_PLAY) continue;

        strcpy(current_path, command.path);
        for(;;) {
            AudioRunResult result;
            uint8_t error;

            audio_report_state(AUDIO_PLAYER_LOADING, 0U);
            replacement_path[0] = '\0';
            result = audio_run_file(current_path,
                                    replacement_path,
                                    sizeof(replacement_path),
                                    &error);
            if(result == AUDIO_RUN_REPLACED) {
                SystemLog_Write(SYSTEM_LOG_PLAY_REPLACED, current_path, 0U);
                strcpy(current_path, replacement_path);
                continue;
            }
            if(result == AUDIO_RUN_FINISHED) {
                SystemLog_Write(SYSTEM_LOG_PLAY_FINISHED, current_path, 0U);
                audio_report_state(AUDIO_PLAYER_FINISHED, 0U);
            }
            else if(result == AUDIO_RUN_STOPPED) {
                SystemLog_Write(SYSTEM_LOG_PLAY_STOPPED, current_path, 0U);
                audio_report_state(AUDIO_PLAYER_STOPPED, 0U);
            }
            else {
                SystemLog_Write(SYSTEM_LOG_PLAY_ERROR, current_path, error);
                audio_report_state(AUDIO_PLAYER_ERROR, error);
            }
            break;
        }
    }
}

bool AudioPlayer_Init(void)
{
    if(audio_initialized) return true;

    audio_command_queue_storage = (uint8_t *)mymalloc(
        SRAMEX,
        AUDIO_COMMAND_QUEUE_LENGTH * sizeof(AudioCommand));
    if(audio_command_queue_storage == NULL) return false;

    audio_command_queue = xQueueCreateStatic(AUDIO_COMMAND_QUEUE_LENGTH,
                                              sizeof(AudioCommand),
                                              audio_command_queue_storage,
                                              &audio_command_queue_control);
    audio_event_queue = xQueueCreateStatic(1U,
                                            sizeof(AudioPlayerEvent),
                                            audio_event_queue_storage,
                                            &audio_event_queue_control);
    if(audio_command_queue == NULL || audio_event_queue == NULL) return false;

    audio_initialized = true;
    audio_report_state(AUDIO_PLAYER_IDLE, 0U);
    return true;
}

bool AudioPlayer_Play(const char * path)
{
    bool sent;
    /* Record before sending: the higher priority audio task may run as soon
     * as xQueueSend wakes it, before this UI task resumes. */
    SystemLog_Write(SYSTEM_LOG_PLAY_REQUEST, path, 0U);
    sent = audio_send_command(AUDIO_COMMAND_PLAY, path);
    if(!sent) SystemLog_Write(SYSTEM_LOG_PLAY_REQUEST, path, SYSTEM_LOG_ERROR_QUEUE);
    return sent;
}

bool AudioPlayer_IsReady(void)
{
    return audio_initialized && audio_task_handle != NULL;
}

bool AudioPlayer_Pause(void)
{
    return audio_send_command(AUDIO_COMMAND_PAUSE, NULL);
}

bool AudioPlayer_Resume(void)
{
    return audio_send_command(AUDIO_COMMAND_RESUME, NULL);
}

bool AudioPlayer_Stop(void)
{
    return audio_send_command(AUDIO_COMMAND_STOP, NULL);
}

AudioPlayerState AudioPlayer_GetState(void)
{
    return audio_state;
}

bool AudioPlayer_PollEvent(AudioPlayerEvent * event)
{
    return event != NULL && audio_event_queue != NULL &&
           xQueueReceive(audio_event_queue, event, 0U) == pdPASS;
}
