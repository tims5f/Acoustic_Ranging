/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_network.h"
#include "app_clock.h"
#include "app_scope.h"
#include "app_audio.h"
#include "app_measure.h"
#include "app_temperature.h"
#include "app_settings.h"
#include "app_dht11.h"
#include "app_touch.h"
#include "app_ui.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static AppUiModel ui_model;
static int16_t ui_waveform[160];
static AppAudioSnapshot audio_snapshot;
static AppAudioTriggerEvent audio_trigger_event;
static AppMeasureResult measure_result;
static AppUiInitStatus ui_init_status;
static uint32_t ui_update_tick;
static uint64_t ui_live_target_ns;
static uint64_t ui_live_last_slot = UINT64_MAX;
static uint32_t touch_last_seen_tick;
static uint32_t touch_last_adjust_tick;
static uint32_t last_measurement_tick;
static uint32_t last_measurement_speed;
static uint32_t last_measurement_distance;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void App_FatalBlink(uint8_t count)
{
  uint8_t i;

  while (1)
  {
    for (i = 0U; i < count; i++)
    {
      HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
      HAL_Delay(180U);
      HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
      HAL_Delay(180U);
    }
    HAL_Delay(1200U);
  }
}

static void App_UpdateNetworkUi(void)
{
  AppNetworkRole role;
  AppClockState clock_status;

  ui_model.network_link_up = AppNetwork_IsLinkUp();
  ui_model.network_has_address = AppNetwork_HasAddress();
  AppNetwork_GetIPv4(ui_model.local_ip);
  ui_model.peer_online = AppNetwork_IsPeerOnline();
  ui_model.network_rx_count = AppNetwork_GetRxCount();
  ui_model.network_tx_count = AppNetwork_GetTxCount();
  ui_model.network_drop_count = AppNetwork_GetDropCount();

  role = AppNetwork_GetRole();
  if (role == APP_NETWORK_ROLE_MASTER)
  {
    ui_model.role = APP_UI_ROLE_MASTER;
  }
  else if (role == APP_NETWORK_ROLE_SLAVE)
  {
    ui_model.role = APP_UI_ROLE_SLAVE;
  }
  else
  {
    ui_model.role = APP_UI_ROLE_UNKNOWN;
  }

  clock_status = AppClock_GetState();
  if (clock_status == APP_CLOCK_LOCKED)
  {
    ui_model.sync_state = APP_UI_SYNC_LOCKED;
  }
  else if (clock_status == APP_CLOCK_ACQUIRING)
  {
    ui_model.sync_state = APP_UI_SYNC_ACQUIRING;
  }
  else if (clock_status == APP_CLOCK_LOST)
  {
    ui_model.sync_state = APP_UI_SYNC_LOST;
  }
  else
  {
    ui_model.sync_state = APP_UI_SYNC_SEARCHING;
  }
  ui_model.clock_offset_ns = AppClock_GetErrorNs();
  ui_model.clock_drift_ppb = AppClock_GetDriftPpb();
}

static void App_UpdateTemperatureUi(void)
{
  ui_model.temperature_automatic = AppTemperature_IsAutomatic();
  ui_model.temperature_ready = AppTemperature_IsReady();
  ui_model.temperature_source = AppTemperature_GetSource();
  ui_model.temperature_deci_c = AppTemperature_GetTemperature();
  ui_model.sound_speed_mm_s = AppTemperature_GetSoundSpeed();
  if (ui_model.measurement_valid && last_measurement_speed && ui_model.temperature_ready)
    ui_model.distance_mm = (uint32_t)(((uint64_t)last_measurement_distance *
        ui_model.sound_speed_mm_s + last_measurement_speed / 2U) / last_measurement_speed);
}

static void App_UpdateAudioUi(void)
{
  AppAudio_CopySnapshot(&audio_snapshot, ui_waveform,
                        (uint16_t)(sizeof(ui_waveform) /
                                   sizeof(ui_waveform[0])));
  ui_model.waveform_mode = AppSettings_GetWaveformMode();
  ui_model.waveform_valid = audio_snapshot.trigger_valid;
  if (ui_model.waveform_mode == APP_WAVEFORM_LIVE)
  {
    ui_model.waveform_valid = AppAudio_CopyLiveWaveform(ui_waveform,
        APP_AUDIO_WAVEFORM_SAMPLES, ui_live_target_ns);
  }
  if(ui_model.audio_mode!=AppSettings_GetAudioMode()) {
    ui_model.measurement_valid=0U; ui_model.arrival_difference_change_valid=0U;
    ui_model.direction=APP_UI_DIRECTION_NONE;
  }
  ui_model.audio_mode=AppSettings_GetAudioMode();
  ui_model.microphone_state = (uint8_t)audio_snapshot.state;
  ui_model.microphone_left_peak = audio_snapshot.left_peak;
  ui_model.microphone_right_peak = audio_snapshot.right_peak;
  ui_model.microphone_frames = audio_snapshot.frames_captured;
  ui_model.microphone_errors = audio_snapshot.error_count;
  ui_model.trigger_valid = audio_snapshot.trigger_valid;
  ui_model.trigger_channel = audio_snapshot.trigger_channel;
  ui_model.trigger_threshold_permille =
      audio_snapshot.trigger_threshold_permille;
  ui_model.trigger_peak = audio_snapshot.trigger_peak;
  ui_model.trigger_event_id = audio_snapshot.trigger_event_id;
  ui_model.trigger_accepted_count = audio_snapshot.trigger_accepted_count;
  ui_model.touch_available = AppTouch_IsReady();
  ui_model.signal_clipped = (uint8_t)(ui_model.signal_clipped |
                                     audio_snapshot.clipped);
  ui_model.frame_id = audio_snapshot.frames_captured / 256U;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  AppUI_ConfigMPU();
  AppAudio_ConfigMPU();
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  /* USER CODE BEGIN 2 */
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
  ui_init_status = AppUI_Init();
  if (ui_init_status != APP_UI_INIT_OK)
  {
    App_FatalBlink((uint8_t)ui_init_status);
  }
  AppUI_CreateDemo(&ui_model, ui_waveform,
                   (uint16_t)(sizeof(ui_waveform) / sizeof(ui_waveform[0])));
  ui_model.measurement_valid = 0U;
  ui_model.direction = APP_UI_DIRECTION_NONE;
  ui_model.quality = APP_UI_QUALITY_POOR;
  ui_model.correlation_per_mille = 0U;
  ui_model.snr_deci_db = 0U;
  ui_model.event_id = 0U;
  AppNetwork_Init();
  AppClock_Init();
  AppScope_Init();
  AppAudio_Init();
  AppMeasure_Init();
  AppTemperature_Init();
  AppDht11_Init();
  AppTouch_Init();
  App_UpdateNetworkUi();
  App_UpdateAudioUi();
  App_UpdateTemperatureUi();
  AppUI_Render(&ui_model);
  ui_update_tick = HAL_GetTick();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    AppNetwork_Process();
    AppClock_Process();
    AppScope_Process();
    AppDht11_Process();
    AppTemperature_Process();
    AppAudio_Process();
    AppMeasure_Process();

    {
      uint8_t redraw = 0U;
      uint16_t touch_x;
      uint16_t touch_y;
      uint32_t now_ms = HAL_GetTick();

      if (AppTouch_Poll(&touch_x, &touch_y) != 0U)
      {
        if (((now_ms - touch_last_seen_tick) >= 75U) ||
            ((now_ms - touch_last_adjust_tick) >= 250U))
        {
          uint8_t action = AppUI_HandleWaveformTouch(&ui_model, touch_x,
              touch_y, (uint8_t)((now_ms - touch_last_seen_tick) >= 75U));
          if (action != 0U)
          {
            if ((action & (APP_UI_TOUCH_TRIGGER | APP_UI_TOUCH_AUDIO_MODE)) != 0U)
              AppAudio_SetTriggerThreshold(ui_model.trigger_threshold_permille);
            if ((action & APP_UI_TOUCH_WAVE_MODE) != 0U)
              ui_live_last_slot = UINT64_MAX;
            touch_last_adjust_tick = now_ms;
            redraw = 1U;
          }
 /*         
          // 新按钮：判断手指是否点在按钮里面
else if (touch_x >= 225U && touch_x < 285U &&
         touch_y >= 160U && touch_y < 178U &&
         (now_ms - touch_last_seen_tick) >= 75U)
{
    // 点击后，把温度设为25℃，并进入手动模式
    AppTemperature_SetManual(250);

    touch_last_adjust_tick = now_ms;
    redraw = 1U;
}*/
          else if (!ui_model.waveform_fullscreen &&
                   AppUI_AdjustTemperatureByTouch(touch_x, touch_y))
          {
            touch_last_adjust_tick = now_ms;
            redraw = 1U;
          }
          else if (!ui_model.waveform_fullscreen && AppUI_AdjustTriggerByTouch(&ui_model,
                                         touch_x, touch_y) != 0U)
          {
            AppAudio_SetTriggerThreshold(
                ui_model.trigger_threshold_permille);
            touch_last_adjust_tick = now_ms;
            redraw = 1U;
          }
        }
        touch_last_seen_tick = now_ms;
      }

      if (AppAudio_PopTriggerEvent(&audio_trigger_event) != 0U)
      {
        ui_model.arrival_ns = audio_trigger_event.arrival_ns;
        ui_model.arrival_event_id = audio_trigger_event.event_id;
        ui_model.arrival_valid = audio_trigger_event.onset_valid;
        AppMeasure_OnAudioEvent(&audio_trigger_event);
        redraw = 1U;
      }

      if (AppMeasure_PopResult(&measure_result) != 0U)
      {
        ui_model.measurement_valid = 1U;
        ui_model.distance_mm = measure_result.distance_mm;
        ui_model.arrival_difference_change_ns = measure_result.arrival_difference_change_ns;
        ui_model.arrival_difference_change_valid = measure_result.arrival_difference_change_valid;
        last_measurement_distance = measure_result.distance_mm;
        last_measurement_speed = measure_result.sound_speed_mm_s;
        ui_model.direction = measure_result.source_at_master ?
            APP_UI_DIRECTION_A : APP_UI_DIRECTION_B;
        ui_model.event_id = measure_result.event_id;
        last_measurement_tick = now_ms;
        ui_model.result_latency_ms = measure_result.latency_ms;
        ui_model.correlation_per_mille = measure_result.correlation_permille;
        ui_model.signal_clipped = measure_result.clipped;
        ui_model.quality = measure_result.clipped ?
            APP_UI_QUALITY_POOR : APP_UI_QUALITY_FAIR;
        redraw = 1U;
      }

      if ((now_ms - ui_update_tick) >= 500U)
      {
        ui_update_tick = now_ms;
        App_UpdateNetworkUi();
        App_UpdateTemperatureUi();
        AppMeasure_GetDiagnostics(&ui_model.correlation_per_mille,
                                  &ui_model.measurement_reject_reason);
        if (!ui_model.temperature_ready || (ui_model.peer_online == 0U) ||
            (ui_model.sync_state != APP_UI_SYNC_LOCKED) ||
            ((last_measurement_tick != 0U) &&
             ((now_ms - last_measurement_tick) > 5000U)))
        {
          ui_model.measurement_valid = 0U;
          ui_model.direction = APP_UI_DIRECTION_NONE;
        }
        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
        redraw = 1U;
      }

      if (AppSettings_GetWaveformMode() == APP_WAVEFORM_LIVE)
      {
        /* Select a common 50 ms grid, allowing DMA 20 ms to collect it. */
        uint64_t wave_now = AppClock_NowNs();
        uint64_t slot = wave_now / APP_WAVEFORM_PERIOD_NS;
        if ((wave_now % APP_WAVEFORM_PERIOD_NS) >= APP_WAVEFORM_DISPLAY_DELAY_NS &&
            slot != ui_live_last_slot)
        {
          ui_live_target_ns = slot * APP_WAVEFORM_PERIOD_NS;
          ui_live_last_slot = slot;
          redraw = 1U;
        }
        else redraw = 0U;
      }
      else ui_live_last_slot = UINT64_MAX;

      if (redraw != 0U)
      {
        App_UpdateTemperatureUi();
        App_UpdateAudioUi();
        AppUI_Render(&ui_model);
      }
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 432;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Activate the Over-Drive mode
  */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_7) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
