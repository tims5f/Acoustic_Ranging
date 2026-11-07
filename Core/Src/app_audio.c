#include "app_audio.h"
#include "app_clock.h"
#include "app_audio_filter.h"
#include "app_settings.h"

#include "stm32746g_discovery_audio.h"

#include <string.h>

#define AUDIO_DMA_WORDS            1024U
#define AUDIO_HALF_WORDS            (AUDIO_DMA_WORDS / 2U)
#define AUDIO_FRAMES_PER_HALF       (AUDIO_HALF_WORDS / 2U)
#define AUDIO_DMA_BUFFER_ADDRESS    0x20047000U
#define AUDIO_RESTART_DELAY_MS      1000U
#define AUDIO_TRIGGER_HOLDOFF_FRAMES (APP_AUDIO_SAMPLE_RATE_HZ * 4U / 5U)
#define AUDIO_TRIGGER_UI_PRE_FRAMES 40U
#define AUDIO_LIVE_RING_SAMPLES 4096U

extern SAI_HandleTypeDef haudio_in_sai;

/* DMA2 cannot access DTCM at 0x20000000. This buffer is in SRAM1. */
#pragma location=AUDIO_DMA_BUFFER_ADDRESS
__root __no_init static uint16_t audio_dma_words[AUDIO_DMA_WORDS];

static volatile AppAudioState audio_state;
static volatile uint16_t interval_left_peak;
static volatile uint16_t interval_right_peak;
static volatile uint32_t frames_captured;
static volatile uint32_t error_count;
static volatile uint8_t interval_clipped;
static volatile int16_t trigger_waveform[APP_AUDIO_WAVEFORM_SAMPLES];
static volatile int16_t live_ring[AUDIO_LIVE_RING_SAMPLES];
static volatile uint16_t live_head;
static volatile uint16_t live_count;
static volatile uint64_t live_end_ns;
static int16_t trigger_capture[APP_AUDIO_TRIGGER_CAPTURE_SAMPLES];
static int16_t pretrigger_left[APP_AUDIO_TRIGGER_PRE_SAMPLES];
static int16_t pretrigger_right[APP_AUDIO_TRIGGER_PRE_SAMPLES];
static volatile uint16_t trigger_threshold_permille;
static volatile uint16_t trigger_threshold_raw;
static volatile uint16_t trigger_peak;
static volatile uint16_t capture_count;
static volatile uint16_t pretrigger_head;
static volatile uint32_t trigger_event_id;
static volatile uint32_t trigger_accepted_count;
static volatile uint32_t holdoff_until_frame;
static volatile uint8_t trigger_channel;
static volatile uint8_t trigger_armed;
static volatile uint8_t trigger_waveform_valid;
static volatile uint8_t trigger_capture_valid;
static volatile uint8_t trigger_event_pending;
static volatile uint8_t trigger_capture_reading;
static volatile AppAudioTriggerEvent pending_event;
static uint32_t capture_sample_index;
static uint64_t capture_arrival_ns;
static volatile uint32_t last_restart_ms;
static volatile uint32_t last_block_ms;
static uint8_t codec_ready;
static uint32_t envelope_left_sum, envelope_right_sum;
static AppAudioFilter audio_filters[2];
static volatile uint16_t audio_noise_level, audio_adaptive_gate;
static uint32_t audio_warmup_until;
static uint8_t audio_capture_mode;
static volatile uint32_t audio_dma_delay_words, audio_dma_max_delay_words, audio_dma_late_blocks;

static uint8_t audio_block_end(uint32_t boundary_remaining, uint64_t *end_ns)
{
  uint32_t before, after, elapsed, retry;
  uint64_t now;
  for(retry=0U; retry<3U; retry++) {
    before=__HAL_DMA_GET_COUNTER(haudio_in_sai.hdmarx);
    now=AppClock_NowNs();
    after=__HAL_DMA_GET_COUNTER(haudio_in_sai.hdmarx);
    if(before==after) break;
  }
  elapsed=(boundary_remaining+AUDIO_DMA_WORDS-after)%AUDIO_DMA_WORDS;
  audio_dma_delay_words=elapsed;
  if(elapsed>audio_dma_max_delay_words) audio_dma_max_delay_words=elapsed;
  if(elapsed>=AUDIO_HALF_WORDS) {
    audio_dma_late_blocks++; error_count++;
    capture_count=0U; trigger_capture_valid=0U; trigger_event_pending=0U;
    return 0U;
  }
  /* DMA counts stereo halfwords, two per sample frame. Backdate the
     callback to the buffer boundary instead of including ISR latency. */
  *end_ns=now-(uint64_t)elapsed*1000000000ULL/(APP_AUDIO_SAMPLE_RATE_HZ*2ULL);
  return 1U;
}
static uint16_t sample_magnitude(int16_t sample)
{
  int32_t value = sample;
  return (uint16_t)((value < 0) ? -value : value);
}

static uint8_t audio_find_onset(uint16_t *onset, uint32_t *fraction_ns)
{
  uint32_t noise = 0U, sum = 0U, maximum = 0U, baseline, gate;
  uint16_t i;
  for (i=0U; i<128U; i++) noise += sample_magnitude(trigger_capture[i]);
  baseline = noise * 24U / 128U;
  for (i=0U; i<APP_AUDIO_TRIGGER_CAPTURE_SAMPLES; i++)
  {
    sum += sample_magnitude(trigger_capture[i]);
    if (i>=24U) sum -= sample_magnitude(trigger_capture[i-24U]);
    if (i>=151U && sum>maximum) maximum=sum;
  }
  /* A quiet preamble and a relative envelope edge disambiguate periodic
     bursts. The estimate selects a cycle; correlation supplies final timing. */
  if (maximum < baseline*3U || maximum < 24U*32U) return 0U;
  gate=baseline+(maximum-baseline)/2U;
  sum=0U;
  for (i=0U; i<APP_AUDIO_TRIGGER_CAPTURE_SAMPLES; i++)
  {
    uint32_t previous=sum;
    sum += sample_magnitude(trigger_capture[i]);
    if (i>=24U) sum -= sample_magnitude(trigger_capture[i-24U]);
    if (i>=151U && sum>=gate && previous<gate)
    {
      *onset=(uint16_t)(i-24U);
      *fraction_ns=((gate-previous)*(1000000000UL/APP_AUDIO_SAMPLE_RATE_HZ))/(sum-previous);
      return 1U;
    }
  }
  return 0U;
}

static void audio_process_half(uint16_t start_word, uint64_t block_end_ns)
{
  uint16_t i;
  uint16_t left_peak = 0U;
  uint16_t right_peak = 0U;
  uint16_t threshold = trigger_threshold_raw;
  uint16_t envelope_gate = threshold / 2U;
  uint16_t release_level;
  uint32_t noise_gate;
  audio_noise_level = AppAudioFilter_Noise(&audio_filters[0]);
  if(AppAudioFilter_Noise(&audio_filters[1])>audio_noise_level)
    audio_noise_level=AppAudioFilter_Noise(&audio_filters[1]);
  noise_gate=(uint32_t)audio_noise_level*(AppSettings_GetAudioMode()==APP_AUDIO_MODE_PHONE?4U:6U)+16U;
  if(noise_gate>30000U) noise_gate=30000U;
  if(envelope_gate<noise_gate) envelope_gate=(uint16_t)noise_gate;
  if(envelope_gate<48U) envelope_gate=48U;
  audio_adaptive_gate=envelope_gate;
  release_level = (uint16_t)((uint32_t)envelope_gate * 3U / 4U);
  uint32_t first_frame = frames_captured;
  if (audio_state == APP_AUDIO_ERROR)
  {
    return;
  }

  for (i = 0U; i < AUDIO_FRAMES_PER_HALF; i++)
  {
    int16_t left_sample = AppAudioFilter_Process(&audio_filters[0], (int16_t)audio_dma_words[start_word + 2U * i]);
    int16_t right_sample = AppAudioFilter_Process(&audio_filters[1], (int16_t)audio_dma_words[start_word + 2U * i + 1U]);
    uint16_t left = sample_magnitude(left_sample);
    uint16_t right = sample_magnitude(right_sample);
    uint16_t magnitude = (left > right) ? left : right;
    uint32_t frame = first_frame + i;
    uint16_t old_index = (uint16_t)((pretrigger_head - 24U) &
                                  (APP_AUDIO_TRIGGER_PRE_SAMPLES - 1U));
    uint16_t envelope;
    envelope_left_sum += left;
    envelope_right_sum += right;
    envelope_left_sum -= sample_magnitude(pretrigger_left[old_index]);
    envelope_right_sum -= sample_magnitude(pretrigger_right[old_index]);
    envelope = (uint16_t)(((envelope_left_sum > envelope_right_sum) ?
                          envelope_left_sum : envelope_right_sum) / 24U);
    if (left > left_peak)
    {
      left_peak = left;
    }
    if (right > right_peak)
    {
      right_peak = right;
    }

    if (capture_count != 0U)
    {
      int16_t sample = (trigger_channel == 0U) ? left_sample : right_sample;
      trigger_capture[capture_count++] = sample;
      if (sample_magnitude(sample) > trigger_peak)
      {
        trigger_peak = sample_magnitude(sample);
      }
      if (capture_count == APP_AUDIO_TRIGGER_CAPTURE_SAMPLES)
      {
        uint16_t j;
        uint16_t onset;
        uint32_t onset_fraction_ns;

        for (j = 0U; j < APP_AUDIO_WAVEFORM_SAMPLES; j++)
        {
          trigger_waveform[j] =
              trigger_capture[APP_AUDIO_TRIGGER_PRE_SAMPLES -
                              AUDIO_TRIGGER_UI_PRE_FRAMES + j];
        }
        capture_count = 0U;
        trigger_waveform_valid = 1U;
        trigger_capture_valid = 1U;
        trigger_event_id++;
        pending_event.event_id = trigger_event_id;
        pending_event.sample_index = capture_sample_index;
        pending_event.sample_time_ns = capture_arrival_ns;
        pending_event.onset_valid = 0U;
        if (audio_find_onset(&onset, &onset_fraction_ns) != 0U)
        {
          pending_event.onset_valid = 1U;
          int32_t offset = (int32_t)onset -
                           (int32_t)APP_AUDIO_TRIGGER_PRE_SAMPLES;
          int64_t refined_ns = (int64_t)capture_arrival_ns +
              ((int64_t)offset * 1000000000LL) /
              APP_AUDIO_SAMPLE_RATE_HZ + onset_fraction_ns;
          pending_event.arrival_ns = (refined_ns > 0) ?
                                      (uint64_t)refined_ns : 0U;
        }
        else
        {
          pending_event.arrival_ns = 0U;
        }
        pending_event.peak = trigger_peak;
        pending_event.threshold_permille = trigger_threshold_permille;
        pending_event.channel = trigger_channel;
        trigger_event_pending = (pending_event.arrival_ns != 0U) ? 1U : 0U;
        if (trigger_event_pending != 0U) trigger_accepted_count++;
        {
        uint32_t minimum_peak=(uint32_t)trigger_threshold_raw*4U;
        uint16_t completed_peak=trigger_peak;
        if(!trigger_event_pending || (uint32_t)completed_peak < minimum_peak) {
          /* A rejected weak capture must not hide a subsequent real clap
             for the full 0.8 s strong-event refractory interval. */
          holdoff_until_frame=frame+APP_AUDIO_SAMPLE_RATE_HZ/100U;
        }
        }
      }
    }
    else if ((int32_t)(frame - holdoff_until_frame) >= 0 &&
             (int32_t)(frame - audio_warmup_until) >= 0)
    {
      if (envelope <= release_level)
      {
        trigger_armed = 1U;
      }
      if ((trigger_armed != 0U) && (envelope >= envelope_gate) &&
          (trigger_capture_reading == 0U))
      {
        uint16_t j;

        trigger_armed = 0U;
        trigger_channel = (envelope_right_sum > envelope_left_sum) ? 1U : 0U;
        trigger_peak = magnitude;
        capture_sample_index = frame;
        /* DMA callback is at the end of this 256-frame block. Both boards
           use the same sample position convention; fixed codec latency cancels. */
        capture_arrival_ns = block_end_ns -
            ((uint64_t)(AUDIO_FRAMES_PER_HALF - i) * 1000000000ULL /
             APP_AUDIO_SAMPLE_RATE_HZ);
        trigger_waveform_valid = 0U;
        trigger_capture_valid = 0U;
        holdoff_until_frame = frame + AUDIO_TRIGGER_HOLDOFF_FRAMES;
        for (j = 0U; j < APP_AUDIO_TRIGGER_PRE_SAMPLES; j++)
        {
          uint16_t index = (uint16_t)((pretrigger_head + j) &
                                     (APP_AUDIO_TRIGGER_PRE_SAMPLES - 1U));
          trigger_capture[j] = (trigger_channel == 0U) ?
                               pretrigger_left[index] : pretrigger_right[index];
        }
        trigger_capture[APP_AUDIO_TRIGGER_PRE_SAMPLES] =
            (trigger_channel == 0U) ? left_sample : right_sample;
        capture_count = APP_AUDIO_TRIGGER_PRE_SAMPLES + 1U;
      }
    }

    pretrigger_left[pretrigger_head] = left_sample;
    live_ring[live_head] = left_sample;
    live_head = (uint16_t)((live_head + 1U) & (AUDIO_LIVE_RING_SAMPLES - 1U));
    if (live_count < AUDIO_LIVE_RING_SAMPLES) live_count++;
    pretrigger_right[pretrigger_head] = right_sample;
    pretrigger_head = (uint16_t)((pretrigger_head + 1U) &
                                 (APP_AUDIO_TRIGGER_PRE_SAMPLES - 1U));
  }

  if (left_peak > interval_left_peak)
  {
    interval_left_peak = left_peak;
  }
  if (right_peak > interval_right_peak)
  {
    interval_right_peak = right_peak;
  }
  if ((left_peak >= 32000U) || (right_peak >= 32000U))
  {
    interval_clipped = 1U;
  }
  frames_captured += AUDIO_FRAMES_PER_HALF;
  live_end_ns = block_end_ns;
  last_block_ms = HAL_GetTick();
  audio_state = APP_AUDIO_RUNNING;
}

static uint8_t audio_start_dma(void)
{
  AppAudioFilter_Reset(&audio_filters[0]);
  AppAudioFilter_Reset(&audio_filters[1]);
  audio_warmup_until=frames_captured+APP_AUDIO_SAMPLE_RATE_HZ;
  envelope_left_sum = 0U;
  envelope_right_sum = 0U;
  capture_count = 0U;
  trigger_armed = 0U;
  holdoff_until_frame = frames_captured;
  pretrigger_head = 0U;
  (void)memset(pretrigger_left, 0, sizeof(pretrigger_left));
  live_head = 0U;
  live_count = 0U;
  live_end_ns = 0U;
  (void)memset(pretrigger_right, 0, sizeof(pretrigger_right));
  if (HAL_SAI_Receive_DMA(&haudio_in_sai, (uint8_t *)audio_dma_words,
                          AUDIO_DMA_WORDS) != HAL_OK)
  {
    audio_state = APP_AUDIO_ERROR;
    error_count++;
    last_restart_ms = HAL_GetTick();
    return 0U;
  }
  audio_state = APP_AUDIO_STARTING;
  last_block_ms = HAL_GetTick();
  return 1U;
}

void AppAudio_ConfigMPU(void)
{
  MPU_Region_InitTypeDef region = {0};

  HAL_MPU_Disable();
  region.Enable = MPU_REGION_ENABLE;
  region.Number = MPU_REGION_NUMBER3;
  region.BaseAddress = AUDIO_DMA_BUFFER_ADDRESS;
  region.Size = MPU_REGION_SIZE_4KB;
  region.SubRegionDisable = 0U;
  region.TypeExtField = MPU_TEX_LEVEL0;
  region.AccessPermission = MPU_REGION_FULL_ACCESS;
  region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  region.IsShareable = MPU_ACCESS_SHAREABLE;
  region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  region.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
  HAL_MPU_ConfigRegion(&region);
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

void AppAudio_Init(void)
{
  audio_state = APP_AUDIO_STARTING;
  interval_left_peak = 0U;
  interval_right_peak = 0U;
  frames_captured = 0U;
  error_count = 0U;
  interval_clipped = 0U;
  trigger_threshold_permille = APP_AUDIO_TRIGGER_DEFAULT_PERMILLE;
  trigger_threshold_raw = (uint16_t)((32768UL *
      APP_AUDIO_TRIGGER_DEFAULT_PERMILLE) / 1000UL);
  trigger_peak = 0U;
  capture_count = 0U;
  pretrigger_head = 0U;
  trigger_event_id = 0U;
  trigger_accepted_count = 0U;
  holdoff_until_frame = 0U;
  trigger_channel = 0U;
  trigger_armed = 0U;
  trigger_waveform_valid = 0U;
  trigger_capture_valid = 0U;
  trigger_event_pending = 0U;
  trigger_capture_reading = 0U;
  codec_ready = 0U;
  last_block_ms = HAL_GetTick();
  (void)memset((void *)trigger_waveform, 0, sizeof(trigger_waveform));
  (void)memset(pretrigger_left, 0, sizeof(pretrigger_left));
  (void)memset(pretrigger_right, 0, sizeof(pretrigger_right));

  if (BSP_AUDIO_IN_Init(APP_AUDIO_SAMPLE_RATE_HZ, 16U, 2U) != AUDIO_OK)
  {
    audio_state = APP_AUDIO_ERROR;
    error_count++;
    last_restart_ms = HAL_GetTick();
    return;
  }
  codec_ready = 1U;
  (void)BSP_AUDIO_IN_SetVolume(68U);
  /* Timestamping runs in the DMA callback. The BSP default is priority 15,
     which lets unrelated interrupts delay the timestamp unpredictably. */
  HAL_NVIC_SetPriority(DMA2_Stream7_IRQn, 4U, 0U);
  (void)audio_start_dma();
}

void AppAudio_SetTriggerThreshold(uint16_t threshold_permille)
{
  uint32_t interrupt_mask;

  if (threshold_permille < APP_AUDIO_TRIGGER_MIN_PERMILLE)
  {
    threshold_permille = APP_AUDIO_TRIGGER_MIN_PERMILLE;
  }
  else if (threshold_permille > APP_AUDIO_TRIGGER_MAX_PERMILLE)
  {
    threshold_permille = APP_AUDIO_TRIGGER_MAX_PERMILLE;
  }
  interrupt_mask = __get_PRIMASK();
  __disable_irq();
  trigger_threshold_permille = threshold_permille;
  trigger_threshold_raw = (uint16_t)((32768UL * threshold_permille) / 1000UL);
  capture_count = 0U;
  trigger_armed = 0U;
  holdoff_until_frame = frames_captured;
  trigger_waveform_valid = 0U;
  trigger_capture_valid = 0U;
  trigger_event_pending = 0U;
  (void)memset((void *)trigger_waveform, 0, sizeof(trigger_waveform));
  __set_PRIMASK(interrupt_mask);
}

uint8_t AppAudio_PopTriggerEvent(AppAudioTriggerEvent *event)
{
  uint32_t interrupt_mask;

  if (event == NULL)
  {
    return 0U;
  }
  interrupt_mask = __get_PRIMASK();
  __disable_irq();
  if (trigger_event_pending == 0U)
  {
    __set_PRIMASK(interrupt_mask);
    return 0U;
  }
  event->event_id = pending_event.event_id;
  event->sample_index = pending_event.sample_index;
  event->arrival_ns = pending_event.arrival_ns;
  event->sample_time_ns = pending_event.sample_time_ns;
  event->onset_valid = pending_event.onset_valid;
  event->peak = pending_event.peak;
  event->threshold_permille = pending_event.threshold_permille;
  event->channel = pending_event.channel;
  trigger_event_pending = 0U;
  __set_PRIMASK(interrupt_mask);
  return 1U;
}

uint8_t AppAudio_CopyTriggerCapture(uint32_t event_id, int16_t *pcm,
                                    uint16_t sample_count)
{
  uint32_t interrupt_mask;

  if ((pcm == NULL) ||
      (sample_count != APP_AUDIO_TRIGGER_CAPTURE_SAMPLES))
  {
    return 0U;
  }
  interrupt_mask = __get_PRIMASK();
  __disable_irq();
  if ((trigger_capture_valid == 0U) || (trigger_event_id != event_id))
  {
    __set_PRIMASK(interrupt_mask);
    return 0U;
  }
  trigger_capture_reading = 1U;
  __set_PRIMASK(interrupt_mask);
  (void)memcpy(pcm, trigger_capture, sizeof(trigger_capture));
  interrupt_mask = __get_PRIMASK();
  __disable_irq();
  trigger_capture_reading = 0U;
  __set_PRIMASK(interrupt_mask);
  return 1U;
}

void AppAudio_Process(void)
{
  if(audio_capture_mode!=AppSettings_GetAudioMode()) {
    audio_capture_mode=AppSettings_GetAudioMode();
    AppAudio_SetTriggerThreshold(trigger_threshold_permille);
  }
  uint32_t now_ms = HAL_GetTick();

  if ((audio_state != APP_AUDIO_ERROR) &&
      ((now_ms - last_block_ms) >= AUDIO_RESTART_DELAY_MS))
  {
    audio_state = APP_AUDIO_ERROR;
    error_count++;
    last_restart_ms = now_ms;
  }
  if ((audio_state != APP_AUDIO_ERROR) ||
      ((now_ms - last_restart_ms) < AUDIO_RESTART_DELAY_MS))
  {
    return;
  }
  last_restart_ms = now_ms;
  if (codec_ready == 0U)
  {
    if (BSP_AUDIO_IN_Init(APP_AUDIO_SAMPLE_RATE_HZ, 16U, 2U) != AUDIO_OK)
    {
      error_count++;
      return;
    }
    codec_ready = 1U;
    (void)BSP_AUDIO_IN_SetVolume(68U);
  }
  else
  {
    (void)HAL_SAI_DMAStop(&haudio_in_sai);
  }
  (void)audio_start_dma();
}

void AppAudio_CopySnapshot(AppAudioSnapshot *snapshot,
                           int16_t *waveform, uint16_t waveform_count)
{
  uint32_t interrupt_mask;
  uint16_t i;

  if ((snapshot == NULL) || (waveform == NULL) ||
      (waveform_count != APP_AUDIO_WAVEFORM_SAMPLES))
  {
    return;
  }
  interrupt_mask = __get_PRIMASK();
  __disable_irq();
  snapshot->state = audio_state;
  snapshot->left_peak = interval_left_peak;
  snapshot->right_peak = interval_right_peak;
  snapshot->frames_captured = frames_captured;
  snapshot->error_count = error_count;
  snapshot->clipped = interval_clipped;
  snapshot->trigger_valid = trigger_waveform_valid;
  snapshot->trigger_channel = trigger_channel;
  snapshot->trigger_threshold_permille = trigger_threshold_permille;
  snapshot->trigger_peak = trigger_peak;
  snapshot->trigger_event_id = trigger_event_id;
  snapshot->trigger_accepted_count = trigger_accepted_count;
  for (i = 0U; i < APP_AUDIO_WAVEFORM_SAMPLES; i++)
  {
    waveform[i] = (trigger_waveform_valid != 0U) ?
                  trigger_waveform[i] : 0;
  }
  interval_left_peak = 0U;
  interval_right_peak = 0U;
  interval_clipped = 0U;
  __set_PRIMASK(interrupt_mask);
}

uint8_t AppAudio_CopyLiveWaveform(int16_t *waveform, uint16_t count,
                                 uint64_t start_ns)
{
  uint32_t mask, offset;
  uint64_t age_ns;
  uint16_t i, first;
  if (waveform == NULL || count != APP_AUDIO_WAVEFORM_SAMPLES) return 0U;
  mask = __get_PRIMASK();
  __disable_irq();
  if (audio_state != APP_AUDIO_RUNNING || live_end_ns <= start_ns)
  {
    __set_PRIMASK(mask);
    return 0U;
  }
  age_ns = live_end_ns - start_ns;
  if (age_ns > 100000000ULL)
  {
    __set_PRIMASK(mask);
    return 0U;
  }
  offset = (uint32_t)((age_ns * APP_AUDIO_SAMPLE_RATE_HZ + 500000000ULL) /
                       1000000000ULL);
  if (offset < count || offset > live_count)
  {
    __set_PRIMASK(mask);
    return 0U;
  }
  first = (uint16_t)((live_head + AUDIO_LIVE_RING_SAMPLES - offset) &
                     (AUDIO_LIVE_RING_SAMPLES - 1U));
  for (i = 0U; i < count; i++)
    waveform[i] = live_ring[(first + i) & (AUDIO_LIVE_RING_SAMPLES - 1U)];
  __set_PRIMASK(mask);
  return 1U;
}

void BSP_AUDIO_IN_HalfTransfer_CallBack(void)
{
  uint64_t end_ns;
  if(audio_block_end(AUDIO_HALF_WORDS, &end_ns)) audio_process_half(0U, end_ns);
}

void BSP_AUDIO_IN_TransferComplete_CallBack(void)
{
  uint64_t end_ns;
  if(audio_block_end(AUDIO_DMA_WORDS, &end_ns)) audio_process_half(AUDIO_HALF_WORDS, end_ns);
}

void BSP_AUDIO_IN_Error_CallBack(void)
{
  audio_state = APP_AUDIO_ERROR;
  error_count++;
  last_restart_ms = HAL_GetTick();
}
