#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H
#include <stdint.h>
#define APP_AUDIO_MODE_CLAP 0U
#define APP_AUDIO_MODE_PHONE 1U
uint8_t AppSettings_GetAudioMode(void);
void AppSettings_SetAudioMode(uint8_t mode);
uint8_t AppSettings_IsPending(void);
#endif
