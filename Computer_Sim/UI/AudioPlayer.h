#ifndef __AUDIO_PLAYER_H
#define __AUDIO_PLAYER_H

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include <stdbool.h>
#include <stdint.h>

#define AUDIO_PLAYER_PATH_MAX 256U
#define AUDIO_PLAYER_ERROR_BUFFER 0x80U
#define AUDIO_PLAYER_ERROR_DATA   0x81U
#define AUDIO_PLAYER_ERROR_DMA    0x82U
#define AUDIO_PLAYER_ERROR_FILE   0x83U

typedef enum {
    AUDIO_PLAYER_IDLE,
    AUDIO_PLAYER_LOADING,
    AUDIO_PLAYER_PLAYING,
    AUDIO_PLAYER_PAUSED,
    AUDIO_PLAYER_STOPPED,
    AUDIO_PLAYER_FINISHED,
    AUDIO_PLAYER_ERROR
} AudioPlayerState;

typedef struct {
    AudioPlayerState state;
    uint8_t error;
} AudioPlayerEvent;

bool AudioPlayer_Init(void);
/* Run once from task4; blocks on commands and never returns. */
void AudioPlayer_Task(void * argument);
bool AudioPlayer_IsReady(void);
bool AudioPlayer_Play(const char * path);
bool AudioPlayer_Pause(void);
bool AudioPlayer_Resume(void);
bool AudioPlayer_Stop(void);
AudioPlayerState AudioPlayer_GetState(void);
bool AudioPlayer_PollEvent(AudioPlayerEvent * event);

#endif
