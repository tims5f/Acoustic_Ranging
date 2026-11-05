#ifndef APP_AUDIO_FILTER_H
#define APP_AUDIO_FILTER_H
#include <stdint.h>
typedef struct {
  float hp1, hp2, lp1, lp2, noise;
} AppAudioFilter;
void AppAudioFilter_Reset(AppAudioFilter *f);
int16_t AppAudioFilter_Process(AppAudioFilter *f, int16_t sample);
uint16_t AppAudioFilter_Noise(const AppAudioFilter *f);
uint8_t AppAudioFilter_IsImpulse(const int16_t *pcm, uint16_t count, uint16_t pre, uint16_t peak);
uint8_t AppAudioFilter_PhoneQuality(const int16_t *pcm, uint16_t count, uint16_t pre);
#endif
