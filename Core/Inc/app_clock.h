#ifndef APP_CLOCK_H
#define APP_CLOCK_H

#include <stdint.h>

typedef enum
{
  APP_CLOCK_SEARCHING = 0,
  APP_CLOCK_ACQUIRING,
  APP_CLOCK_LOCKED,
  APP_CLOCK_LOST
} AppClockState;

void AppClock_Init(void);
void AppClock_Process(void);
AppClockState AppClock_GetState(void);
int32_t AppClock_GetErrorNs(void);
int32_t AppClock_GetDriftPpb(void);
uint32_t AppClock_GetRoundTripNs(void);
uint64_t AppClock_NowNs(void);
uint8_t AppClock_RawDeadlineForGlobal(uint64_t global_target_ns,
                                      uint64_t *raw_target_ns);

#endif
