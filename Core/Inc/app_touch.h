#ifndef APP_TOUCH_H
#define APP_TOUCH_H

#include <stdint.h>

void AppTouch_Init(void);
uint8_t AppTouch_Poll(uint16_t *x, uint16_t *y);
uint8_t AppTouch_IsReady(void);

#endif
