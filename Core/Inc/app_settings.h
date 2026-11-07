#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H
#include <stdint.h>
#define APP_AUDIO_MODE_CLAP 0U
#define APP_AUDIO_MODE_PHONE 1U
#define APP_WAVEFORM_LIVE 0U
#define APP_WAVEFORM_EVENT 1U
#define APP_WAVEFORM_PERIOD_NS 50000000ULL
#define APP_WAVEFORM_DISPLAY_DELAY_NS 20000000ULL
uint8_t AppSettings_GetAudioMode(void);
void AppSettings_SetAudioMode(uint8_t mode);
uint8_t AppSettings_GetWaveformMode(void);
void AppSettings_SetWaveformMode(uint8_t mode);
uint8_t AppSettings_IsPending(void);
#endif
