#ifndef APP_MEASURE_H
#define APP_MEASURE_H

#include <stdint.h>
#include "app_audio.h"

typedef struct
{
  uint32_t event_id;
  uint32_t distance_mm;
  uint32_t sound_speed_mm_s;
  uint16_t latency_ms;
  uint8_t source_at_master;
  uint8_t clipped;
  uint16_t peak;
  uint16_t correlation_permille;
} AppMeasureResult;

void AppMeasure_Init(void);
void AppMeasure_Process(void);
void AppMeasure_OnAudioEvent(const AppAudioTriggerEvent *event);
uint8_t AppMeasure_PopResult(AppMeasureResult *result);
void AppMeasure_GetDiagnostics(uint16_t *correlation, uint8_t *reject_reason);

#endif
