#include "app_touch.h"

#include "stm32746g_discovery_ts.h"

#define TOUCH_POLL_INTERVAL_MS 25U
#define TOUCH_RETRY_INTERVAL_MS 2000U
#define TOUCH_DISPLAY_WIDTH    480U
#define TOUCH_DISPLAY_HEIGHT   272U

static uint8_t touch_ready;
static uint32_t last_poll_ms;
static uint32_t last_retry_ms;

void AppTouch_Init(void)
{
  last_retry_ms = HAL_GetTick();
  last_poll_ms = last_retry_ms;
  touch_ready = (BSP_TS_Init(TOUCH_DISPLAY_WIDTH,
                             TOUCH_DISPLAY_HEIGHT) == TS_OK) ? 1U : 0U;
}

uint8_t AppTouch_Poll(uint16_t *x, uint16_t *y)
{
  TS_StateTypeDef state = {0};
  uint32_t now_ms = HAL_GetTick();

  if ((x == NULL) || (y == NULL))
  {
    return 0U;
  }
  if (touch_ready == 0U)
  {
    if ((now_ms - last_retry_ms) >= TOUCH_RETRY_INTERVAL_MS)
    {
      last_retry_ms = now_ms;
      touch_ready = (BSP_TS_Init(TOUCH_DISPLAY_WIDTH,
                                 TOUCH_DISPLAY_HEIGHT) == TS_OK) ? 1U : 0U;
    }
    return 0U;
  }
  if ((now_ms - last_poll_ms) < TOUCH_POLL_INTERVAL_MS)
  {
    return 0U;
  }
  last_poll_ms = now_ms;
  if (BSP_TS_GetState(&state) != TS_OK)
  {
    touch_ready = 0U;
    last_retry_ms = now_ms;
    return 0U;
  }
  if ((state.touchDetected == 0U) ||
      (state.touchX[0] >= TOUCH_DISPLAY_WIDTH) ||
      (state.touchY[0] >= TOUCH_DISPLAY_HEIGHT))
  {
    return 0U;
  }
  *x = state.touchX[0];
  *y = state.touchY[0];
  return 1U;
}

uint8_t AppTouch_IsReady(void)
{
  return touch_ready;
}
