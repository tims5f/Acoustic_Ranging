#ifndef APP_TEMPERATURE_H
#define APP_TEMPERATURE_H
#include <stdint.h>

/* Temperature in 0.1 C, sound speed in mm/s. One sensor serves both boards. */
void AppTemperature_Init(void);
void AppTemperature_Process(void);
void AppTemperature_SetLocalSample(int16_t temperature, uint8_t valid);
void AppTemperature_SetManual(int16_t temperature);
void AppTemperature_SetAutomatic(uint8_t automatic);
int16_t AppTemperature_GetTemperature(void);
int16_t AppTemperature_GetManual(void);
uint32_t AppTemperature_GetSoundSpeed(void);
uint8_t AppTemperature_IsAutomatic(void);
uint8_t AppTemperature_IsReady(void);
uint8_t AppTemperature_GetSource(void); /* 0 manual, 1 master sensor, 2 slave sensor */
#endif
