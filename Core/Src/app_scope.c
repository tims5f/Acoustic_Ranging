#include "app_scope.h"

#include "app_clock.h"
#include "stm32f7xx_hal.h"

#define SCOPE_SECOND_NS       1000000000ULL
#define SCOPE_LEAD_NS          250000000ULL
#define SCOPE_DONE_MARGIN_NS     5000000ULL
#define SCOPE_TIMER_CLOCK_HZ  108000000U
#define SCOPE_PULSE_DELAY_TICKS       108U
#define SCOPE_PULSE_END_TICKS       10908U

static uint8_t scope_armed;
static uint64_t scope_target_global_ns;

static void scope_disarm(void)
{
  /* A future target deasserts the Ethernet PTP trigger before TIM2 is reset. */
  ETH->PTPTTHR = UINT32_MAX;
  ETH->PTPTTLR = UINT32_MAX;
  TIM2->CR1 &= ~TIM_CR1_CEN;
  TIM2->EGR = TIM_EGR_UG;
  TIM2->CNT = 0U;
  TIM2->SR = 0U;
  scope_armed = 0U;
}

void AppScope_Init(void)
{
  GPIO_InitTypeDef pin = {0};

  if (HAL_RCC_GetPCLK1Freq() * 2U != SCOPE_TIMER_CLOCK_HZ)
  {
    return;
  }

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_TIM2_CLK_ENABLE();
  TIM2->CR1 = 0U;
  TIM2->CR2 = 0U;
  TIM2->SMCR = TIM_SMCR_TS_0 | TIM_SMCR_SMS_1 | TIM_SMCR_SMS_2;
  TIM2->DIER = 0U;
  TIM2->CCER = 0U;
  TIM2->PSC = 0U;
  TIM2->ARR = SCOPE_PULSE_END_TICKS - 1U;
  TIM2->CCR1 = SCOPE_PULSE_DELAY_TICKS;
  /* PWM mode 2: PA15 rises 1 us after trigger, falls 100 us later. */
  TIM2->CCMR1 = TIM_CCMR1_OC1M_0 | TIM_CCMR1_OC1M_1 |
                TIM_CCMR1_OC1M_2;
  TIM2->OR = (TIM2->OR & ~TIM2_OR_ITR1_RMP) | TIM2_OR_ITR1_RMP_0;
  TIM2->CR1 = TIM_CR1_OPM;
  scope_disarm();
  TIM2->CCER = TIM_CCER_CC1E;

  pin.Pin = GPIO_PIN_15;
  pin.Mode = GPIO_MODE_AF_PP;
  pin.Pull = GPIO_NOPULL;
  pin.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  pin.Alternate = GPIO_AF1_TIM2;
  HAL_GPIO_Init(GPIOA, &pin);
}

void AppScope_Process(void)
{
  uint64_t now_ns;
  uint64_t raw_target_ns;
  uint64_t target_ns;

  if (AppClock_GetState() != APP_CLOCK_LOCKED)
  {
    if (scope_armed != 0U)
    {
      scope_disarm();
    }
    return;
  }

  now_ns = AppClock_NowNs();
  if ((scope_armed != 0U) &&
      (now_ns < scope_target_global_ns + SCOPE_DONE_MARGIN_NS))
  {
    return;
  }
  target_ns = ((now_ns + SCOPE_LEAD_NS + SCOPE_SECOND_NS - 1ULL) /
               SCOPE_SECOND_NS) * SCOPE_SECOND_NS;
  if (AppClock_RawDeadlineForGlobal(target_ns, &raw_target_ns) == 0U)
  {
    scope_disarm();
    return;
  }

  scope_disarm();
  ETH->PTPTTHR = (uint32_t)(raw_target_ns / SCOPE_SECOND_NS);
  ETH->PTPTTLR = (uint32_t)(raw_target_ns % SCOPE_SECOND_NS);
  scope_target_global_ns = target_ns;
  scope_armed = 1U;
}
