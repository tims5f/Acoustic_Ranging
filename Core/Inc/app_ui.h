#ifndef APP_UI_H
#define APP_UI_H

#include <stdint.h>

typedef enum
{
  APP_UI_ROLE_UNKNOWN = 0,
  APP_UI_ROLE_MASTER,
  APP_UI_ROLE_SLAVE
} AppUiRole;

typedef enum
{
  APP_UI_SYNC_SEARCHING = 0,
  APP_UI_SYNC_ACQUIRING,
  APP_UI_SYNC_LOCKED,
  APP_UI_SYNC_LOST
} AppUiSyncState;

typedef enum
{
  APP_UI_DIRECTION_NONE = 0,
  APP_UI_DIRECTION_A,
  APP_UI_DIRECTION_B
} AppUiDirection;

typedef enum
{
  APP_UI_QUALITY_POOR = 0,
  APP_UI_QUALITY_FAIR,
  APP_UI_QUALITY_GOOD,
  APP_UI_QUALITY_EXCELLENT
} AppUiQuality;

typedef struct
{
  AppUiRole role;
  AppUiSyncState sync_state;
  AppUiDirection direction;
  AppUiQuality quality;
  uint8_t network_link_up;
  uint8_t network_has_address;
  uint8_t local_ip[4];
  uint8_t peer_online;
  uint8_t measurement_valid;
  uint8_t signal_clipped;
  uint8_t microphone_state;
  uint8_t trigger_valid;
  uint8_t trigger_channel;
  uint8_t touch_available;
  uint16_t microphone_left_peak;
  uint16_t microphone_right_peak;
  uint16_t trigger_threshold_permille;
  uint16_t trigger_peak;
  uint32_t trigger_event_id;
  uint32_t trigger_accepted_count;
  uint32_t microphone_frames;
  uint32_t microphone_errors;
  uint32_t event_id;
  uint32_t frame_id;
  uint32_t distance_mm;
  int32_t clock_offset_ns;
  int32_t clock_drift_ppb;
  uint8_t temperature_automatic;
  uint8_t temperature_ready;
  uint8_t temperature_source;
  int16_t temperature_deci_c;
  uint32_t sound_speed_mm_s;
  uint16_t correlation_per_mille;
  uint8_t measurement_reject_reason;
  uint16_t snr_deci_db;
  uint16_t result_latency_ms;
  uint32_t network_rx_count;
  uint32_t network_tx_count;
  uint32_t network_drop_count;
  const int16_t *waveform;
  uint16_t waveform_count;
  uint64_t arrival_ns;
  uint32_t arrival_event_id;
  uint8_t arrival_valid;
  uint32_t arrival_difference_change_ns;
  uint8_t arrival_difference_change_valid;
  uint8_t audio_mode;
  uint8_t waveform_mode;
  uint8_t waveform_valid;
  uint8_t waveform_fullscreen;
  /* Display range only: 0 = automatic; otherwise +/- PCM sample units. */
  uint16_t waveform_amplitude_span;
} AppUiModel;

typedef enum
{
  APP_UI_INIT_OK = 0,
  APP_UI_INIT_LTDC_ERROR = 2,
  APP_UI_INIT_SDRAM_ERROR = 3,
  APP_UI_INIT_FRAMEBUFFER_ERROR = 4
} AppUiInitStatus;

AppUiInitStatus AppUI_Init(void);
void AppUI_ConfigMPU(void);
void AppUI_Render(const AppUiModel *model);
#define APP_UI_TOUCH_REDRAW 1U
#define APP_UI_TOUCH_TRIGGER 2U
#define APP_UI_TOUCH_AUDIO_MODE 4U
#define APP_UI_TOUCH_WAVE_MODE 8U
uint8_t AppUI_HandleWaveformTouch(AppUiModel *model, uint16_t x,
                                 uint16_t y, uint8_t fresh_press);
uint8_t AppUI_AdjustTemperatureByTouch(uint16_t x, uint16_t y);
uint8_t AppUI_AdjustTriggerByTouch(AppUiModel *model, uint16_t x, uint16_t y);
void AppUI_CreateDemo(AppUiModel *model, int16_t *waveform, uint16_t count);
void AppUI_StepDemo(AppUiModel *model, int16_t *waveform, uint16_t count);

#endif
