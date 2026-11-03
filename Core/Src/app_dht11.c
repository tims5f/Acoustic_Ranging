#include "app_dht11.h"
#include "app_temperature.h"
#include "stm32f7xx_hal.h"

/* Arduino A0 = PA0 = TIM5_CH1. CH2 indirectly captures the falling edge
 * of the same input. Hardware timestamps avoid delaying the audio DMA ISR.
 * The bus is only driven LOW (open drain); external module pull-up releases it.
 * No ADC or internal pull-up is enabled, including with a 5V powered module.
 */
static volatile uint8_t dht_state; /* 0 idle, 1 start low, 2 capture, 3 done, 4 error */
static volatile uint8_t dht_bytes[5], dht_bits, dht_response, dht_rise_valid;
static volatile uint32_t dht_rise_us;
static uint32_t dht_last_attempt_ms;
static volatile uint32_t dht_attempts, dht_successes, dht_checksum_errors, dht_timeout_errors;
static volatile int16_t dht_temperature_deci_c;
static volatile uint16_t dht_humidity_deci_percent;

uint8_t AppDht11_Decode(const uint8_t b[5], int16_t *temperature, uint16_t *humidity)
{
  int16_t t;
  uint16_t h;
  if(b==0 || temperature==0 || humidity==0 ||
     (uint8_t)(b[0]+b[1]+b[2]+b[3])!=b[4] || b[1]>9 || (b[3]&0x7FU)>9) return 0;
  h=(uint16_t)(b[0]*10U+b[1]);
  t=(int16_t)(b[2]*10U+(b[3]&0x7FU));
  if(b[3]&0x80U) t=(int16_t)-t;
  if(h>1000U || t < -200 || t>600) return 0;
  *temperature=t; *humidity=h;
  return 1;
}

static void finish(uint8_t state)
{
  TIM5->DIER=0U;
  TIM5->CCER=0U;
  GPIOA->MODER &= ~3UL; /* input, external pull-up only */
  dht_state=state;
}

void AppDht11_Init(void)
{
  GPIO_InitTypeDef pin={0};
  uint32_t timer_hz=HAL_RCC_GetPCLK1Freq();
  if((RCC->CFGR & RCC_CFGR_PPRE1)!=0U) timer_hz*=2U;
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_TIM5_CLK_ENABLE();
  pin.Pin=GPIO_PIN_0; pin.Mode=GPIO_MODE_INPUT; pin.Pull=GPIO_NOPULL;
  pin.Speed=GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA,&pin);
  GPIOA->OTYPER |= GPIO_PIN_0;
  GPIOA->AFR[0]=(GPIOA->AFR[0]&~15UL)|GPIO_AF2_TIM5;
  TIM5->CR1=0U; TIM5->DIER=0U; TIM5->CCER=0U;
  TIM5->PSC=timer_hz/1000000U-1U; TIM5->ARR=0xFFFFFFFFUL;
  TIM5->CCMR1=TIM_CCMR1_CC1S_0|TIM_CCMR1_CC2S_1;
  TIM5->CCMR2=0U; TIM5->EGR=TIM_EGR_UG; TIM5->SR=0U;
  TIM5->CR1=TIM_CR1_CEN;
  HAL_NVIC_SetPriority(TIM5_IRQn,3U,0U);
  HAL_NVIC_EnableIRQ(TIM5_IRQn);
  dht_last_attempt_ms=HAL_GetTick(); /* sensor power-up settling */
}

void AppDht11_TimerIRQ(void)
{
  uint32_t status=TIM5->SR;
  status &= TIM5->DIER;
  if(status&TIM_SR_CC3IF) {
    TIM5->SR=~TIM_SR_CC3IF;
    if(dht_state==1U) {
      TIM5->CCER=TIM_CCER_CC1E|TIM_CCER_CC2E|TIM_CCER_CC2P;
      TIM5->SR=0U;
      TIM5->CCR4=TIM5->CNT+7000U;
      dht_state=2U;
      TIM5->DIER=TIM_DIER_CC1IE|TIM_DIER_CC2IE|TIM_DIER_CC4IE;
      GPIOA->MODER=(GPIOA->MODER&~3UL)|2UL; /* release bus to TIM5 input */
    }
    return;
  }
  if(status&TIM_SR_CC1IF) {
    dht_rise_us=TIM5->CCR1;
    dht_rise_valid=1U;
    TIM5->SR=~TIM_SR_CC1IF;
  }
  if(status&TIM_SR_CC2IF) {
    uint32_t falling=TIM5->CCR2;
    TIM5->SR=~TIM_SR_CC2IF;
    if(dht_state==2U && dht_rise_valid) {
      uint32_t width=falling-dht_rise_us;
      dht_rise_valid=0U;
      if(!dht_response) {
        if(width>=60U && width<=110U) dht_response=1U;
      } else if(width>=15U && width<=100U) {
        uint8_t i=(uint8_t)(dht_bits/8U);
        dht_bytes[i]=(uint8_t)((dht_bytes[i]<<1)|((width>=50U)?1U:0U));
        dht_bits++;
        if(dht_bits==40U) finish(3U);
      } else finish(4U);
    }
  }
  if((status&TIM_SR_CC4IF) && dht_state==2U) {
    TIM5->SR=~TIM_SR_CC4IF;
    finish(4U);
  }
}

void AppDht11_Process(void)
{
  uint32_t now=HAL_GetTick();
  if(dht_state==3U) {
    uint8_t b[5],i; int16_t t; uint16_t h;
    for(i=0;i<5U;i++) b[i]=dht_bytes[i];
    if(AppDht11_Decode(b,&t,&h)) {
      dht_successes++; dht_temperature_deci_c=t; dht_humidity_deci_percent=h;
      AppTemperature_SetLocalSample(t,1U);
    } else dht_checksum_errors++;
    dht_state=0U;
  } else if(dht_state==4U) { dht_timeout_errors++; dht_state=0U; }
  if(dht_state==0U && now-dht_last_attempt_ms>=2100U) {
    uint8_t i;
    dht_last_attempt_ms=now; dht_attempts++;
    dht_bits=0U; dht_response=0U; dht_rise_valid=0U;
    for(i=0;i<5U;i++) dht_bytes[i]=0U;
    TIM5->DIER=0U; TIM5->CCER=0U; TIM5->SR=0U;
    GPIOA->BSRR=(uint32_t)GPIO_PIN_0<<16;
    GPIOA->MODER=(GPIOA->MODER&~3UL)|1UL;
    dht_state=1U;
    TIM5->CCR3=TIM5->CNT+20000U;
    TIM5->DIER=TIM_DIER_CC3IE;
  }
}
