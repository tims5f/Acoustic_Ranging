#ifndef APP_DHT11_H
#define APP_DHT11_H
#include <stdint.h>
void AppDht11_Init(void);
void AppDht11_Process(void);
void AppDht11_TimerIRQ(void);
uint8_t AppDht11_Decode(const uint8_t bytes[5], int16_t *temperature, uint16_t *humidity);
#endif
