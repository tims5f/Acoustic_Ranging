#ifndef APP_FONT_ZH_H
#define APP_FONT_ZH_H

#include <stdint.h>

void AppFontZh_DrawText(uint16_t x, uint16_t y, const uint16_t *text,
                        uint16_t count, uint32_t color, uint32_t background);

#endif
