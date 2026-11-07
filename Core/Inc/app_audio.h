#ifndef APP_AUDIO_H
#define APP_AUDIO_H

#include <stdint.h>

#define APP_AUDIO_SAMPLE_RATE_HZ 48000U
#define APP_AUDIO_WAVEFORM_SAMPLES 160U
#define APP_AUDIO_TRIGGER_PRE_SAMPLES 1024U
#define APP_AUDIO_TRIGGER_CAPTURE_SAMPLES 3072U
#define APP_AUDIO_TRIGGER_MIN_PERMILLE 1U
#define APP_AUDIO_TRIGGER_MAX_PERMILLE 200U
#define APP_AUDIO_TRIGGER_DEFAULT_PERMILLE 5U

typedef enum
{
  APP_AUDIO_STARTING = 0,
  APP_AUDIO_RUNNING,
  APP_AUDIO_ERROR
} AppAudioState;

typedef struct
{
  AppAudioState state;
  uint16_t left_peak;
  uint16_t right_peak;
  uint32_t frames_captured;
  uint32_t error_count;
  uint8_t clipped;
  uint8_t trigger_valid;
  uint8_t trigger_channel;
  uint16_t trigger_threshold_permille;
  uint16_t trigger_peak;
  uint32_t trigger_event_id;
  uint32_t trigger_accepted_count;
} AppAudioSnapshot;

typedef struct
{
  uint32_t event_id;
  uint32_t sample_index;
  uint64_t arrival_ns;
  uint64_t sample_time_ns;
  uint8_t onset_valid;
  uint16_t peak;
  uint16_t threshold_permille;
  uint8_t channel;
} AppAudioTriggerEvent;

void AppAudio_ConfigMPU(void);
void AppAudio_Init(void);
void AppAudio_Process(void);
void AppAudio_SetTriggerThreshold(uint16_t threshold_permille);
uint8_t AppAudio_PopTriggerEvent(AppAudioTriggerEvent *event);
uint8_t AppAudio_CopyTriggerCapture(uint32_t event_id, int16_t *pcm,
                                    uint16_t sample_count);
void AppAudio_CopySnapshot(AppAudioSnapshot *snapshot,
                           int16_t *waveform, uint16_t waveform_count);
/* Common-clock start time; fixed left microphone channel, no edge alignment. */
uint8_t AppAudio_CopyLiveWaveform(int16_t *waveform, uint16_t count,
                                 uint64_t start_ns);

#endif
