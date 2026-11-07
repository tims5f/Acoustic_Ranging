#include "app_ui.h"
#include "app_audio.h"
#include "app_settings.h"
#include "app_temperature.h"
#include "app_font_zh.h"

#include <stdio.h>
#include <string.h>

#include "stm32746g_discovery_lcd.h"
#include "stm32746g_discovery_sdram.h"

#define UI_WIDTH                  480U
#define UI_HEIGHT                 272U
#define UI_FRAMEBUFFER_0_ADDRESS  LCD_FB_START_ADDRESS
#define UI_FRAMEBUFFER_1_ADDRESS  (LCD_FB_START_ADDRESS + 0x00080000U)
#define UI_FRAMEBUFFER_PIXELS     (UI_WIDTH * UI_HEIGHT)
#define UI_SDRAM_TEST_ADDRESS     (SDRAM_DEVICE_ADDR + SDRAM_DEVICE_SIZE - 16U)
#define UI_BOOT_FRAME_COUNT       48U

#define UI_COLOR_BG               0xFF07131FU
#define UI_COLOR_SURFACE          0xFF0C2030U
#define UI_COLOR_HEADER           0xFF04101AU
#define UI_COLOR_TEXT             0xFFE4F7FFU
#define UI_COLOR_MUTED            0xFF82A7B7U
#define UI_COLOR_LINE             0xFF1B4258U
#define UI_COLOR_GREEN            0xFF00E6A7U
#define UI_COLOR_GREEN_SOFT       0xFF103D42U
#define UI_COLOR_AMBER            0xFFFFC857U
#define UI_COLOR_AMBER_SOFT       0xFF453719U
#define UI_COLOR_RED              0xFFFF527AU
#define UI_COLOR_RED_SOFT         0xFF451D2CU
#define UI_COLOR_BLUE             0xFF25B9FFU
#define UI_COLOR_BLUE_SOFT        0xFF103A56U
#define UI_COLOR_WAVE             0xFF00E5FFU
#define UI_COLOR_GRID             0xFF15364AU

static const int16_t demo_wave_shape[32] =
{
     0,  18,  35,  49,  59,  64,  62,  55,
    43,  27,   9,  -9, -27, -43, -55, -62,
   -64, -59, -49, -35, -18,   0,  15,  27,
    34,  36,  31,  20,   7,  -5, -11,  -8
};

/* 16x16 Chinese labels used by the readable on-board UI. */
static const uint16_t zh_title[]       = {0x58F0U, 0x6CE2U, 0x6D4BU, 0x8DDDU};
static const uint16_t zh_distance[]    = {0x8DDDU, 0x79BBU};
static const uint16_t zh_environment[] = {0x73AFU, 0x5883U};
static const uint16_t zh_quality[]     = {0x8D28U, 0x91CFU};
static const uint16_t zh_wait_sound[]  = {0x7B49U, 0x5F85U, 0x58F0U, 0x97F3U};
static const uint16_t zh_source_prefix[] = {0x58F0U, 0x6E90U, 0x5728U};
static const uint16_t zh_side[]        = {0x4FA7U};
static const uint16_t zh_peer_online[] = {0x5BF9U, 0x7AEFU, 0x5728U, 0x7EBFU};
static const uint16_t zh_sync_locked[] = {0x540CU, 0x6B65U, 0x9501U, 0x5B9AU};
static const uint16_t zh_sync_acquire[]= {0x540CU, 0x6B65U, 0x4E2DU};
static const uint16_t zh_sync_lost[]   = {0x540CU, 0x6B65U, 0x4E22U, 0x5931U};
static const uint16_t zh_sync_search[] = {0x7B49U, 0x5F85U, 0x540CU, 0x6B65U};
static const uint16_t zh_captured[]    = {0x5DF2U, 0x91C7U, 0x96C6U};
static const uint16_t zh_armed[]       = {0x5F85U, 0x673AU};
static const uint16_t zh_mic_error[]   = {0x9EA6U, 0x514BU, 0x98CEU, 0x9519U, 0x8BEFU};
static const uint16_t zh_threshold[]   = {0x9608U, 0x503CU};
static const uint16_t zh_wait_trigger[]= {0x7B49U, 0x5F85U, 0x89E6U, 0x53D1U};
static const uint16_t zh_link_down[]   = {0x94FEU, 0x65ADU};
static const uint16_t zh_autoip[]      = {0x81EAU, 0x52A8U, 0x914DU, 0x7F6EU};
static const uint16_t zh_discover[]    = {0x641CU, 0x5BFBU, 0x5BF9U, 0x7AEFU};
static const uint16_t zh_touch_off[]   = {0x89E6U, 0x6478U, 0x4E0DU, 0x53EFU, 0x7528U};
static const uint16_t zh_clipped[]     = {0x4FE1U, 0x53F7U, 0x524AU, 0x9876U};
static const uint16_t zh_peer_ready[]  = {0x5BF9U, 0x7AEFU, 0x5C31U, 0x7EEAU};

extern LTDC_HandleTypeDef hLtdcHandler;

static uint32_t ui_front_buffer = UI_FRAMEBUFFER_0_ADDRESS;
static uint32_t ui_back_buffer = UI_FRAMEBUFFER_1_ADDRESS;

static void ui_fill_rect(uint16_t x, uint16_t y, uint16_t width,
                         uint16_t height, uint32_t color);

static uint8_t ui_sdram_self_test(void)
{
  volatile uint32_t *test = (volatile uint32_t *)UI_SDRAM_TEST_ADDRESS;
  uint32_t saved[4];
  static const uint32_t patterns[4] =
  {
    0x55AA55AAU, 0xAA55AA55U, 0x01234567U, 0x89ABCDEFU
  };
  uint32_t i;
  uint8_t passed = 1U;

  for (i = 0U; i < 4U; i++)
  {
    saved[i] = test[i];
    test[i] = patterns[i];
  }
  __DSB();

  for (i = 0U; i < 4U; i++)
  {
    if (test[i] != patterns[i])
    {
      passed = 0U;
    }
    test[i] = saved[i];
  }
  __DSB();
  return passed;
}

static void ui_fill_buffer(uint32_t address, uint32_t color)
{
  volatile uint32_t *framebuffer = (volatile uint32_t *)address;
  uint32_t i;

  for (i = 0U; i < UI_FRAMEBUFFER_PIXELS; i++)
  {
    framebuffer[i] = color;
  }
  __DSB();
}

static uint8_t ui_framebuffer_self_test(uint32_t address)
{
  volatile uint32_t *framebuffer = (volatile uint32_t *)address;
  uint32_t middle = (UI_HEIGHT / 2U) * UI_WIDTH;
  uint32_t last = UI_FRAMEBUFFER_PIXELS - 1U;

  framebuffer[0] = 0xFFFF3030U;
  framebuffer[middle] = 0xFF20C878U;
  framebuffer[last] = 0xFF2878E0U;
  __DSB();

  if ((framebuffer[0] != 0xFFFF3030U) ||
      (framebuffer[middle] != 0xFF20C878U) ||
      (framebuffer[last] != 0xFF2878E0U))
  {
    return 0U;
  }
  return 1U;
}

static void ui_wait_for_vertical_reload(void)
{
  uint32_t started = HAL_GetTick();

  while ((LTDC->SRCR & LTDC_SRCR_VBR) != 0U)
  {
    if ((HAL_GetTick() - started) > 50U)
    {
      break;
    }
  }
}

static void ui_begin_frame(void)
{
  ui_wait_for_vertical_reload();
  BSP_LCD_SetLayerAddress_NoReload(0U, ui_back_buffer);
}

static void ui_present_frame(void)
{
  uint32_t previous_front;

  __DSB();
  __HAL_LTDC_VERTICAL_BLANKING_RELOAD_CONFIG(&hLtdcHandler);
  ui_wait_for_vertical_reload();

  previous_front = ui_front_buffer;
  ui_front_buffer = ui_back_buffer;
  ui_back_buffer = previous_front;
}

static void ui_draw_boot_frame(uint16_t frame)
{
  static const int16_t sweep_x[16] =
  {
    240, 255, 268, 276, 279, 276, 268, 255,
    240, 225, 212, 204, 201, 204, 212, 225
  };
  static const int16_t sweep_y[16] =
  {
     78,  81,  89, 102, 116, 130, 143, 151,
    154, 151, 143, 130, 116, 102,  89,  81
  };
  uint16_t phase = (uint16_t)(frame % 16U);
  uint16_t pulse = (uint16_t)(34U + ((frame % 12U) * 3U));
  uint16_t scan_x = (uint16_t)((frame * 20U) % UI_WIDTH);
  uint16_t progress = (uint16_t)(((uint32_t)(frame + 1U) * 280U) /
                                 UI_BOOT_FRAME_COUNT);
  uint16_t i;

  BSP_LCD_Clear(UI_COLOR_BG);

  /* Technical grid and moving scan line. */
  BSP_LCD_SetTextColor(UI_COLOR_GRID);
  for (i = 0U; i < UI_WIDTH; i = (uint16_t)(i + 32U))
  {
    BSP_LCD_DrawVLine(i, 0U, UI_HEIGHT);
  }
  for (i = 0U; i < UI_HEIGHT; i = (uint16_t)(i + 18U))
  {
    BSP_LCD_DrawHLine(0U, i, UI_WIDTH);
  }
  BSP_LCD_SetTextColor(UI_COLOR_BLUE);
  BSP_LCD_DrawVLine(scan_x, 0U, UI_HEIGHT);
  BSP_LCD_DrawHLine(0U, 31U, UI_WIDTH);
  BSP_LCD_DrawHLine(0U, 250U, UI_WIDTH);

  /* Radar-like acoustic core. */
  BSP_LCD_SetTextColor(UI_COLOR_LINE);
  BSP_LCD_DrawCircle(240U, 112U, 78U);
  BSP_LCD_DrawCircle(240U, 112U, 60U);
  BSP_LCD_DrawCircle(240U, 112U, 42U);
  BSP_LCD_DrawCircle(240U, 112U, pulse);
  BSP_LCD_DrawHLine(162U, 112U, 156U);
  BSP_LCD_DrawVLine(240U, 34U, 156U);

  BSP_LCD_SetTextColor(UI_COLOR_GREEN);
  BSP_LCD_DrawLine(240U, 112U, (uint16_t)sweep_x[phase],
                   (uint16_t)sweep_y[phase]);
  BSP_LCD_FillCircle(240U, 112U, 6U);

  BSP_LCD_SetTextColor(UI_COLOR_BLUE);
  BSP_LCD_FillCircle(82U, 112U, 4U);
  BSP_LCD_FillCircle(398U, 112U, 4U);
  BSP_LCD_DrawLine(82U, 112U, 162U, 112U);
  BSP_LCD_DrawLine(318U, 112U, 398U, 112U);

  /* Corner brackets frame the active sensing area. */
  BSP_LCD_SetTextColor(UI_COLOR_BLUE);
  BSP_LCD_DrawHLine(150U, 38U, 22U);
  BSP_LCD_DrawVLine(150U, 38U, 18U);
  BSP_LCD_DrawHLine(308U, 38U, 22U);
  BSP_LCD_DrawVLine(330U, 38U, 18U);
  BSP_LCD_DrawHLine(150U, 190U, 22U);
  BSP_LCD_DrawVLine(150U, 172U, 18U);
  BSP_LCD_DrawHLine(308U, 190U, 22U);
  BSP_LCD_DrawVLine(330U, 172U, 18U);

  AppFontZh_DrawText(208U, 146U, zh_title,
                     (uint16_t)(sizeof(zh_title) / sizeof(zh_title[0])),
                     UI_COLOR_BLUE, UI_COLOR_BG);

  BSP_LCD_SetFont(&Font12);
  BSP_LCD_SetTextColor(UI_COLOR_MUTED);
  BSP_LCD_SetBackColor(UI_COLOR_BG);
  BSP_LCD_DisplayStringAt(0U, 169U, (uint8_t *)"BOOT // AUDIO // CLOCK // LINK",
                          CENTER_MODE);

  ui_fill_rect(100U, 218U, 280U, 6U, UI_COLOR_LINE);
  ui_fill_rect(100U, 218U, progress, 6U, UI_COLOR_GREEN);
  BSP_LCD_SetTextColor(UI_COLOR_GREEN);
  BSP_LCD_DrawRect(96U, 214U, 288U, 14U);
  BSP_LCD_SetFont(&Font8);
  BSP_LCD_SetTextColor(UI_COLOR_MUTED);
  BSP_LCD_SetBackColor(UI_COLOR_BG);
  BSP_LCD_DisplayStringAt(0U, 232U, (uint8_t *)"PTP SYNC  /  SAI DMA  /  ETH PHY",
                          CENTER_MODE);
}

static void ui_run_boot_animation(void)
{
  uint16_t frame;

  for (frame = 0U; frame < UI_BOOT_FRAME_COUNT; frame++)
  {
    ui_begin_frame();
    ui_draw_boot_frame(frame);
    ui_present_frame();
    HAL_Delay(42U);
  }
}

void AppUI_ConfigMPU(void)
{
  MPU_Region_InitTypeDef region = {0};

  HAL_MPU_Disable();

  region.Enable = MPU_REGION_ENABLE;
  region.Number = MPU_REGION_NUMBER1;
  region.BaseAddress = SDRAM_DEVICE_ADDR;
  region.Size = MPU_REGION_SIZE_8MB;
  region.SubRegionDisable = 0x00;
  region.TypeExtField = MPU_TEX_LEVEL1;
  region.AccessPermission = MPU_REGION_FULL_ACCESS;
  region.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
  region.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  region.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
  HAL_MPU_ConfigRegion(&region);

  region.Number = MPU_REGION_NUMBER2;
  region.BaseAddress = 0xA0000000U;
  region.Size = MPU_REGION_SIZE_8KB;
  region.TypeExtField = MPU_TEX_LEVEL0;
  region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  region.IsShareable = MPU_ACCESS_SHAREABLE;
  region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  region.IsBufferable = MPU_ACCESS_BUFFERABLE;
  HAL_MPU_ConfigRegion(&region);

  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

static void ui_fill_rect(uint16_t x, uint16_t y, uint16_t width,
                         uint16_t height, uint32_t color)
{
  BSP_LCD_SetTextColor(color);
  BSP_LCD_FillRect(x, y, width, height);
}

static void ui_draw_text(uint16_t x, uint16_t y, const char *text,
                         sFONT *font, uint32_t color, uint32_t background)
{
  BSP_LCD_SetFont(font);
  BSP_LCD_SetTextColor(color);
  BSP_LCD_SetBackColor(background);
  BSP_LCD_DisplayStringAt(x, y, (uint8_t *)text, LEFT_MODE);
}

static void ui_draw_panel(uint16_t x, uint16_t y, uint16_t width,
                          uint16_t height)
{
  ui_fill_rect(x, y, width, height, UI_COLOR_SURFACE);
  BSP_LCD_SetTextColor(UI_COLOR_LINE);
  BSP_LCD_DrawRect(x, y, width, height);
  BSP_LCD_SetTextColor(UI_COLOR_BLUE);
  BSP_LCD_DrawHLine((uint16_t)(x + 2U), (uint16_t)(y + 1U),
                    (uint16_t)(width - 4U));
  BSP_LCD_DrawHLine((uint16_t)(x + 2U), (uint16_t)(y + height - 2U),
                    18U);
  BSP_LCD_DrawHLine((uint16_t)(x + width - 20U),
                    (uint16_t)(y + height - 2U), 18U);
  BSP_LCD_DrawVLine((uint16_t)(x + 1U), (uint16_t)(y + 2U), 14U);
  BSP_LCD_DrawVLine((uint16_t)(x + width - 2U), (uint16_t)(y + 2U), 14U);
  BSP_LCD_DrawVLine((uint16_t)(x + 1U),
                    (uint16_t)(y + height - 16U), 14U);
  BSP_LCD_DrawVLine((uint16_t)(x + width - 2U),
                    (uint16_t)(y + height - 16U), 14U);
}

static const char *ui_role_text(AppUiRole role)
{
  if (role == APP_UI_ROLE_MASTER)
  {
    return "MASTER";
  }
  if (role == APP_UI_ROLE_SLAVE)
  {
    return "SLAVE";
  }
  return "UNASSIGNED";
}

static const char *ui_quality_text(AppUiQuality quality)
{
  switch (quality)
  {
    case APP_UI_QUALITY_EXCELLENT: return "EXCELLENT";
    case APP_UI_QUALITY_GOOD:      return "GOOD";
    case APP_UI_QUALITY_FAIR:      return "FAIR";
    default:                       return "POOR";
  }
}

static uint32_t ui_sync_color(AppUiSyncState state)
{
  if (state == APP_UI_SYNC_LOCKED)
  {
    return UI_COLOR_GREEN;
  }
  if (state == APP_UI_SYNC_LOST)
  {
    return UI_COLOR_RED;
  }
  return UI_COLOR_AMBER;
}

static uint32_t ui_quality_color(const AppUiModel *model)
{
  if ((model->quality == APP_UI_QUALITY_POOR) || (model->signal_clipped != 0U))
  {
    return UI_COLOR_RED;
  }
  if (model->quality == APP_UI_QUALITY_FAIR)
  {
    return UI_COLOR_AMBER;
  }
  return UI_COLOR_GREEN;
}

static void ui_format_distance(char *buffer, size_t size, uint32_t distance_mm)
{
  (void)snprintf(buffer, size, "%lu.%03lu m",
                 (unsigned long)(distance_mm / 1000U),
                 (unsigned long)(distance_mm % 1000U));
}

static void ui_format_signed_tenths(char *buffer, size_t size, int32_t value,
                                    int32_t divisor, const char *unit)
{
  uint32_t magnitude;
  char sign = '+';

  if (value < 0)
  {
    sign = '-';
    magnitude = (uint32_t)(-value);
  }
  else
  {
    magnitude = (uint32_t)value;
  }

  (void)snprintf(buffer, size, "%c%lu.%lu %s", sign,
                 (unsigned long)(magnitude / (uint32_t)divisor),
                 (unsigned long)((magnitude % (uint32_t)divisor) /
                                 ((uint32_t)divisor / 10U)), unit);
}

static void ui_draw_header(const AppUiModel *model)
{
  const char *role = ui_role_text(model->role);
  uint16_t badge_width = (model->role == APP_UI_ROLE_UNKNOWN) ? 104U : 78U;
  char network[32];
  uint32_t network_color;
  uint32_t badge_color;

  ui_fill_rect(0, 0, UI_WIDTH, 32, UI_COLOR_HEADER);
  BSP_LCD_SetTextColor(UI_COLOR_BLUE);
  BSP_LCD_DrawHLine(0, 31, UI_WIDTH);
  AppFontZh_DrawText(14, 8, zh_title,
                     (uint16_t)(sizeof(zh_title) / sizeof(zh_title[0])),
                     UI_COLOR_BLUE, UI_COLOR_HEADER);

  if (model->network_link_up == 0U)
  {
    (void)snprintf(network, sizeof(network), "LINK DOWN");
    network_color = UI_COLOR_RED;
  }
  else if (model->network_has_address == 0U)
  {
    (void)snprintf(network, sizeof(network), "AUTOIP...");
    network_color = UI_COLOR_AMBER;
  }
  else
  {
    (void)snprintf(network, sizeof(network), "LINK UP  %u.%u.%u.%u",
                   model->local_ip[0], model->local_ip[1],
                   model->local_ip[2], model->local_ip[3]);
    network_color = UI_COLOR_GREEN;
  }
  ui_draw_text(158, 10, network, &Font12,
               network_color, UI_COLOR_HEADER);

  badge_color = (model->role == APP_UI_ROLE_UNKNOWN) ?
                UI_COLOR_AMBER : UI_COLOR_GREEN;
  ui_fill_rect((uint16_t)(UI_WIDTH - badge_width - 12U), 7, badge_width, 18,
               badge_color);
  ui_draw_text((uint16_t)(UI_WIDTH - badge_width - 3U), 10, role, &Font12,
               LCD_COLOR_WHITE, badge_color);
}

static void ui_draw_direction(const AppUiModel *model)
{
  uint32_t accent = UI_COLOR_BLUE;
  uint32_t soft = UI_COLOR_BLUE_SOFT;
  const uint16_t *direction = zh_wait_sound;
  uint16_t direction_count =
      (uint16_t)(sizeof(zh_wait_sound) / sizeof(zh_wait_sound[0]));
  uint8_t source_is_local;

  if (model->measurement_valid != 0U)
  {
    if (model->direction == APP_UI_DIRECTION_A)
    {
      direction = zh_source_prefix;
      direction_count = (uint16_t)(sizeof(zh_source_prefix) /
                                   sizeof(zh_source_prefix[0]));
      accent = UI_COLOR_GREEN;
      soft = UI_COLOR_GREEN_SOFT;
    }
    else if (model->direction == APP_UI_DIRECTION_B)
    {
      direction = zh_source_prefix;
      direction_count = (uint16_t)(sizeof(zh_source_prefix) /
                                   sizeof(zh_source_prefix[0]));
      accent = UI_COLOR_BLUE;
      soft = UI_COLOR_BLUE_SOFT;
    }
  }

  ui_fill_rect(21, 105, 264, 29, soft);
  BSP_LCD_SetTextColor(accent);

  /* Board roles do not encode physical left/right placement. */
  ui_draw_text(34, 115, "SRC", &Font8, accent, soft);
  AppFontZh_DrawText(76, 111, direction, direction_count, accent, soft);
  if ((model->measurement_valid != 0U) &&
      ((model->direction == APP_UI_DIRECTION_A) ||
       (model->direction == APP_UI_DIRECTION_B)))
  {
    ui_draw_text(124, 113,
                 (model->direction == APP_UI_DIRECTION_A) ? "A" : "B",
                 &Font12, accent, soft);
    AppFontZh_DrawText(132, 111, zh_side,
                       (uint16_t)(sizeof(zh_side) / sizeof(zh_side[0])),
                       accent, soft);
    source_is_local = (uint8_t)(
        (model->direction == APP_UI_DIRECTION_A && model->role == APP_UI_ROLE_MASTER) ||
        (model->direction == APP_UI_DIRECTION_B && model->role == APP_UI_ROLE_SLAVE));
    ui_draw_text(156, 115, model->direction == APP_UI_DIRECTION_A ? "MASTER" : "SLAVE",
                 &Font8, accent, soft);
    ui_draw_text(212, 115, source_is_local ? "LOCAL" : "PEER",
                 &Font8, accent, soft);
  }
}

static void ui_draw_measurement(const AppUiModel *model)
{
  char text[32];
  uint32_t sync_color = ui_sync_color(model->sync_state);

  ui_draw_panel(10, 40, 287, 130);
  AppFontZh_DrawText(21, 48, zh_distance,
                     (uint16_t)(sizeof(zh_distance) / sizeof(zh_distance[0])),
                     UI_COLOR_BLUE, UI_COLOR_SURFACE);

  if ((model->measurement_valid != 0U) && (model->arrival_difference_change_valid != 0U))
  {
    uint32_t tenths_us=(model->arrival_difference_change_ns+50U)/100U;
    (void)snprintf(text, sizeof(text), "|dDT| %lu.%lu us",
        (unsigned long)(tenths_us/10U), (unsigned long)(tenths_us%10U));
  }
  else
  {
    (void)strcpy(text, "|dDT| --- us");
  }
  ui_draw_text(95, 50, text, &Font12, UI_COLOR_MUTED, UI_COLOR_SURFACE);

  if (model->measurement_valid != 0U)
  {
    ui_format_distance(text, sizeof(text), model->distance_mm);
  }
  else
  {
    (void)strcpy(text, "---.--- m");
  }
  ui_draw_text(21, 68, text, &Font24, UI_COLOR_TEXT, UI_COLOR_SURFACE);

  /* Range lock reticle beside the main value. */
  BSP_LCD_SetTextColor((model->measurement_valid != 0U) ?
                       UI_COLOR_GREEN : UI_COLOR_BLUE);
  BSP_LCD_DrawCircle(244U, 82U, 20U);
  BSP_LCD_DrawCircle(244U, 82U, 11U);
  BSP_LCD_DrawHLine(216U, 82U, 56U);
  BSP_LCD_DrawVLine(244U, 54U, 56U);
  BSP_LCD_FillCircle(244U, 82U, 3U);
  ui_draw_direction(model);

  ui_fill_rect(21, 145, 8, 8,
               (model->peer_online != 0U) ? UI_COLOR_GREEN : UI_COLOR_RED);
  AppFontZh_DrawText(34, 140, zh_peer_online,
                     (uint16_t)(sizeof(zh_peer_online) /
                                sizeof(zh_peer_online[0])),
                     UI_COLOR_MUTED, UI_COLOR_SURFACE);

  ui_fill_rect(117, 145, 8, 8, sync_color);
  if (model->sync_state == APP_UI_SYNC_LOCKED)
  {
    AppFontZh_DrawText(130, 140, zh_sync_locked,
                       (uint16_t)(sizeof(zh_sync_locked) /
                                  sizeof(zh_sync_locked[0])),
                       UI_COLOR_MUTED, UI_COLOR_SURFACE);
  }
  else if (model->sync_state == APP_UI_SYNC_ACQUIRING)
  {
    AppFontZh_DrawText(130, 140, zh_sync_acquire,
                       (uint16_t)(sizeof(zh_sync_acquire) /
                                  sizeof(zh_sync_acquire[0])),
                       UI_COLOR_MUTED, UI_COLOR_SURFACE);
  }
  else if (model->sync_state == APP_UI_SYNC_LOST)
  {
    AppFontZh_DrawText(130, 140, zh_sync_lost,
                       (uint16_t)(sizeof(zh_sync_lost) /
                                  sizeof(zh_sync_lost[0])),
                       UI_COLOR_MUTED, UI_COLOR_SURFACE);
  }
  else
  {
    AppFontZh_DrawText(130, 140, zh_sync_search,
                       (uint16_t)(sizeof(zh_sync_search) /
                                  sizeof(zh_sync_search[0])),
                       UI_COLOR_MUTED, UI_COLOR_SURFACE);
  }

  ui_format_signed_tenths(text, sizeof(text), model->clock_offset_ns, 1000, "us");
  ui_draw_text(205, 140, text, &Font12, UI_COLOR_MUTED, UI_COLOR_SURFACE);

  ui_format_signed_tenths(text, sizeof(text), model->clock_drift_ppb, 1000, "ppm");
  ui_draw_text(205, 153, text, &Font12, UI_COLOR_MUTED, UI_COLOR_SURFACE);
}

static void ui_draw_environment(const AppUiModel *model)
{
  char text[32];
  uint32_t quality_color = ui_quality_color(model);
  uint16_t quality_width = (uint16_t)((model->correlation_per_mille * 137U) / 1000U);

  ui_draw_panel(305, 40, 165, 130);
  AppFontZh_DrawText(316, 48, zh_environment,
                     (uint16_t)(sizeof(zh_environment) /
                                sizeof(zh_environment[0])),
                     UI_COLOR_BLUE, UI_COLOR_SURFACE);

  (void)snprintf(text, sizeof(text), "%s%d.%d C",
                 model->temperature_deci_c < 0 ? "-" : "",
                 (model->temperature_deci_c < 0 ? -model->temperature_deci_c : model->temperature_deci_c) / 10,
                 (model->temperature_deci_c < 0 ? -model->temperature_deci_c : model->temperature_deci_c) % 10);
  if (!model->temperature_ready) (void)snprintf(text, sizeof(text), "--.- C");
  ui_draw_text(316, 69, text, &Font20, UI_COLOR_TEXT, UI_COLOR_SURFACE);

  (void)snprintf(text, sizeof(text), "%lu.%02lu m/s",
                 (unsigned long)(model->sound_speed_mm_s / 1000U),
                 (unsigned long)((model->sound_speed_mm_s % 1000U) / 10U));
  if (!model->temperature_ready) (void)snprintf(text, sizeof(text), "---.-- m/s");
  ui_draw_text(316, 94, text, &Font12, UI_COLOR_MUTED, UI_COLOR_SURFACE);

  ui_fill_rect(362, 44, 27, 22, UI_COLOR_BLUE_SOFT);
  ui_fill_rect(392, 44, 27, 22, UI_COLOR_BLUE_SOFT);
  ui_fill_rect(422, 44, 42, 22, UI_COLOR_BLUE_SOFT);
  ui_draw_text(371, 47, "-", &Font16, UI_COLOR_TEXT, UI_COLOR_BLUE_SOFT);
  ui_draw_text(401, 47, "+", &Font16, UI_COLOR_TEXT, UI_COLOR_BLUE_SOFT);
  ui_draw_text(427, 51, model->temperature_automatic ? "AUTO" : "MAN",
               &Font8, UI_COLOR_TEXT, UI_COLOR_BLUE_SOFT);
  ui_draw_text(316, 107, !model->temperature_ready ? "SENSOR WAIT / SET MAN" :
               !model->temperature_automatic ? "MANUAL +/- 0.5 C" :
               model->temperature_source == 1 ? "SENSOR A" : "SENSOR B",
               &Font8, model->temperature_ready ? UI_COLOR_GREEN : UI_COLOR_AMBER,
               UI_COLOR_SURFACE);

  BSP_LCD_SetTextColor(UI_COLOR_LINE);
  BSP_LCD_DrawHLine(316, 116, 143);

  AppFontZh_DrawText(316, 121, zh_quality,
                     (uint16_t)(sizeof(zh_quality) / sizeof(zh_quality[0])),
                     UI_COLOR_MUTED, UI_COLOR_SURFACE);
  ui_draw_text(405, 122, ui_quality_text(model->quality), &Font8,
               quality_color, UI_COLOR_SURFACE);
  ui_fill_rect(316, 142, 137, 7, UI_COLOR_LINE);
  ui_fill_rect(316, 142, quality_width, 7, quality_color);

  (void)snprintf(text, sizeof(text), "CORR %u.%u%%  %s",
                 model->correlation_per_mille / 10U,
                 model->correlation_per_mille % 10U,
                 model->measurement_valid ? "VALID" :
                 model->measurement_reject_reason == 2U ? "AMBIG" :
                 model->measurement_reject_reason == 1U ? "LOW" :
                 model->measurement_reject_reason == 3U ? "RANGE" :
                 model->measurement_reject_reason == 4U ? "TYPE" : "WAIT");
  ui_draw_text(316, 154, text, &Font8, UI_COLOR_MUTED, UI_COLOR_SURFACE);
}

/* Waveform display controls use local zoom/range; capture modes stay shared. */
static int32_t ui_waveform_span(const AppUiModel *model)
{
  int32_t span = 256;
  uint16_t i;
  if (model->waveform_amplitude_span != 0U)
    return model->waveform_amplitude_span;
  if (model->waveform != NULL && model->waveform_valid != 0U)
    for (i = 0U; i < model->waveform_count; i++)
    {
      int32_t magnitude = model->waveform[i];
      if (magnitude < 0) magnitude = -magnitude;
      if (magnitude > span) span = magnitude;
    }
  return span;
}

static uint8_t ui_waveform_hit(uint16_t x, uint16_t y,
                               uint16_t left, uint16_t top,
                               uint16_t width, uint16_t height)
{
  return (uint8_t)(x >= left && x < left + width &&
                    y >= top && y < top + height);
}

uint8_t AppUI_HandleWaveformTouch(AppUiModel *model, uint16_t x,
                                 uint16_t y, uint8_t fresh_press)
{
  uint8_t full, amplitude_up, amplitude_down, trigger_up, trigger_down;
  uint16_t next;
  int32_t span;
  if (model == NULL || model->touch_available == 0U) return 0U;
  full = model->waveform_fullscreen;
  if (fresh_press && ((!full && ui_waveform_hit(x,y,14,181,50,21)) ||
                     (full && ui_waveform_hit(x,y,392,8,78,30))))
  {
    model->waveform_fullscreen = (uint8_t)!full;
    return APP_UI_TOUCH_REDRAW;
  }
  if (fresh_press && ((!full && ui_waveform_hit(x,y,70,181,70,21)) ||
                     (full && ui_waveform_hit(x,y,130,8,76,30))))
  {
    AppSettings_SetWaveformMode((uint8_t)(AppSettings_GetWaveformMode() ^ 1U));
    return APP_UI_TOUCH_REDRAW | APP_UI_TOUCH_WAVE_MODE;
  }
  if (fresh_press && ((!full && ui_waveform_hit(x,y,145,181,76,21)) ||
                     (full && ui_waveform_hit(x,y,214,8,76,30))))
  {
    AppSettings_SetAudioMode((uint8_t)(AppSettings_GetAudioMode() ^ 1U));
    return APP_UI_TOUCH_REDRAW | APP_UI_TOUCH_AUDIO_MODE;
  }
  if (!full) return 0U;
  if (fresh_press && ui_waveform_hit(x,y,130,226,54,34))
  {
    model->waveform_amplitude_span = 0U;
    return APP_UI_TOUCH_REDRAW;
  }
  amplitude_down = ui_waveform_hit(x,y,10,226,54,34);
  amplitude_up = ui_waveform_hit(x,y,70,226,54,34);
  if (amplitude_down || amplitude_up)
  {
    span = ui_waveform_span(model);
    span = amplitude_up ? span / 2 : span * 2;
    if (span < 64) span = 64;
    if (span > 32768) span = 32768;
    model->waveform_amplitude_span = (uint16_t)span;
    return APP_UI_TOUCH_REDRAW;
  }
  trigger_down = ui_waveform_hit(x,y,194,226,64,34);
  trigger_up = ui_waveform_hit(x,y,264,226,64,34);
  if (trigger_down || trigger_up)
  {
    next = model->trigger_threshold_permille;
    if (trigger_down)
      next = next > 5U ? (uint16_t)(next - 5U) : APP_AUDIO_TRIGGER_MIN_PERMILLE;
    else
      next = next < APP_AUDIO_TRIGGER_MAX_PERMILLE - 5U ?
             (uint16_t)(next + 5U) : APP_AUDIO_TRIGGER_MAX_PERMILLE;
    if (next == model->trigger_threshold_permille) return 0U;
    model->trigger_threshold_permille = next;
    model->trigger_valid = 0U;
    return APP_UI_TOUCH_REDRAW | APP_UI_TOUCH_TRIGGER;
  }
  return 0U;
}

static void ui_waveform_button(uint16_t x, uint16_t y, uint16_t w,
                                uint16_t h, const char *label, uint32_t color)
{
  ui_fill_rect(x,y,w,h,UI_COLOR_BLUE_SOFT);
  ui_draw_text((uint16_t)(x+6U),(uint16_t)(y+(h-12U)/2U),label,
               &Font12,color,UI_COLOR_BLUE_SOFT);
}

static void ui_draw_waveform_fullscreen(const AppUiModel *model)
{
  char text[64];
  uint16_t i, x, previous_x = 12U;
  int16_t previous_y = 132;
  int32_t span = ui_waveform_span(model);
  int32_t threshold, pixels, scaled;
  ui_fill_rect(0,0,480,40,UI_COLOR_HEADER);
  (void)snprintf(text,sizeof(text),"WAVE %s",
                 model->role == APP_UI_ROLE_MASTER ? "A" :
                 model->role == APP_UI_ROLE_SLAVE ? "B" : "?");
  ui_draw_text(12,14,text,&Font16,UI_COLOR_TEXT,UI_COLOR_HEADER);
  ui_waveform_button(130,8,76,30,
      model->waveform_mode == APP_WAVEFORM_LIVE ? "LIVE" : "EVENT",UI_COLOR_GREEN);
  ui_waveform_button(214,8,76,30,
      model->audio_mode == APP_AUDIO_MODE_PHONE ? "PHONE" : "CLAP",UI_COLOR_TEXT);
  ui_waveform_button(392,8,78,30,"BACK",UI_COLOR_AMBER);
  if (model->waveform_amplitude_span == 0U)
    (void)snprintf(text,sizeof(text),"AMP AUTO | +/- %lu.%lu%%FS",
        (unsigned long)((uint32_t)span*1000U/32768U/10U),
        (unsigned long)((uint32_t)span*1000U/32768U%10U));
  else
    (void)snprintf(text,sizeof(text),"AMP MAN  | +/- %lu.%lu%%FS",
        (unsigned long)((uint32_t)span*1000U/32768U/10U),
        (unsigned long)((uint32_t)span*1000U/32768U%10U));
  ui_draw_text(12,44,text,&Font8,UI_COLOR_MUTED,UI_COLOR_BG);
  ui_draw_text(328,44,"3.33ms | 48kHz",&Font8,UI_COLOR_MUTED,UI_COLOR_BG);
  ui_fill_rect(10,55,460,156,UI_COLOR_SURFACE);
  BSP_LCD_SetTextColor(UI_COLOR_GRID);
  for (i=0;i<5;i++) BSP_LCD_DrawHLine(12,(uint16_t)(58U+i*37U),456);
  for (i=0;i<9;i++) BSP_LCD_DrawVLine((uint16_t)(12U+i*57U),58,149);
  BSP_LCD_SetTextColor(UI_COLOR_LINE);
  BSP_LCD_DrawHLine(12,132,456);
  threshold = (int32_t)((32768UL*model->trigger_threshold_permille)/1000UL);
  pixels = threshold*74/span;
  if (pixels>0 && pixels<=74)
  {
    BSP_LCD_SetTextColor(UI_COLOR_AMBER);
    BSP_LCD_DrawHLine(12,(uint16_t)(132-pixels),456);
    BSP_LCD_DrawHLine(12,(uint16_t)(132+pixels),456);
  }
  if (model->waveform_mode == APP_WAVEFORM_EVENT)
  {
    BSP_LCD_SetTextColor(UI_COLOR_BLUE);
    BSP_LCD_DrawVLine(126,58,149);
  }
  if (model->waveform_valid && model->waveform != NULL && model->waveform_count>=2U)
  {
    BSP_LCD_SetTextColor(UI_COLOR_WAVE);
    for (i=0;i<model->waveform_count;i++)
    {
      int16_t y;
      x=(uint16_t)(12U+(uint32_t)i*455U/(model->waveform_count-1U));
      scaled=(int32_t)model->waveform[i]*74/span;
      if (scaled>74) scaled=74;
      if (scaled< -74) scaled= -74;
      y=(int16_t)(132-scaled);
      if (i!=0U) BSP_LCD_DrawLine(previous_x,(uint16_t)previous_y,x,(uint16_t)y);
      previous_x=x; previous_y=y;
    }
  }
  else ui_draw_text(155,126,model->waveform_mode == APP_WAVEFORM_LIVE ?
                    "WAIT AUDIO" : "WAIT TRIGGER",&Font12,
                    UI_COLOR_MUTED,UI_COLOR_SURFACE);
  (void)snprintf(text,sizeof(text),"%s  |  EVT #%lu  |  %s",
      model->microphone_state == 2U ? "MIC ERROR" : "MIC OK",
      (unsigned long)model->trigger_event_id,
      model->sync_state == APP_UI_SYNC_LOCKED ? "SYNC LOCK" : "SYNC WAIT");
  ui_draw_text(12,214,text,&Font8,model->microphone_state == 2U ?
                UI_COLOR_RED : UI_COLOR_MUTED,UI_COLOR_BG);
  ui_waveform_button(10,226,54,34,"AMP-",UI_COLOR_TEXT);
  ui_waveform_button(70,226,54,34,"AMP+",UI_COLOR_TEXT);
  ui_waveform_button(130,226,54,34,"AUTO",UI_COLOR_GREEN);
  ui_waveform_button(194,226,64,34,"TRIG-",UI_COLOR_AMBER);
  ui_waveform_button(264,226,64,34,"TRIG+",UI_COLOR_AMBER);
  (void)snprintf(text,sizeof(text),"TRIG %u.%u%%FS",
      model->trigger_threshold_permille/10U,model->trigger_threshold_permille%10U);
  ui_draw_text(340,237,text,&Font12,UI_COLOR_AMBER,UI_COLOR_BG);
}

static void ui_draw_waveform(const AppUiModel *model)
{
  uint16_t i;
  uint16_t previous_x = 17U;
  int16_t previous_y = 222;
  char text[64];
  const uint16_t *microphone_status = zh_armed;
  uint16_t microphone_status_count =
      (uint16_t)(sizeof(zh_armed) / sizeof(zh_armed[0]));
  uint8_t microphone_error = 0U;
  /* Scale only the captured event; keep low-level samples near the center. */
  int32_t waveform_scale = ui_waveform_span(model);

  if (model->microphone_state == 1U)
  {
    microphone_status = (model->trigger_valid != 0U) ?
                        zh_captured : zh_armed;
    microphone_status_count = (model->trigger_valid != 0U) ?
        (uint16_t)(sizeof(zh_captured) / sizeof(zh_captured[0])) :
        (uint16_t)(sizeof(zh_armed) / sizeof(zh_armed[0]));
  }
  else if (model->microphone_state == 2U)
  {
    microphone_status = zh_mic_error;
    microphone_status_count =
        (uint16_t)(sizeof(zh_mic_error) / sizeof(zh_mic_error[0]));
    microphone_error = 1U;
  }

  ui_draw_panel(10, 179, 460, 70);
  if (microphone_error == 0U)
  {
    ui_waveform_button(14,181,50,21,"FULL",UI_COLOR_TEXT);
    ui_fill_rect(70, 181, 70, 21, UI_COLOR_GREEN_SOFT);
    ui_draw_text(77, 185, model->waveform_mode == APP_WAVEFORM_LIVE ?
                 "LIVE" : "EVENT", &Font12, UI_COLOR_GREEN, UI_COLOR_GREEN_SOFT);
  }
  else
  {
    AppFontZh_DrawText(18, 181, microphone_status, microphone_status_count,
                       UI_COLOR_RED, UI_COLOR_SURFACE);
  }
  ui_fill_rect(145, 181, 76, 21, UI_COLOR_BLUE_SOFT);
  ui_draw_text(152, 185, model->audio_mode==APP_AUDIO_MODE_PHONE ? "PHONE" : "CLAP",
               &Font12, UI_COLOR_TEXT, UI_COLOR_BLUE_SOFT);
  (void)snprintf(text, sizeof(text), "L%u.%u R%u.%u",
                 (unsigned int)(((uint32_t)model->microphone_left_peak *
                                 1000U) / 32768U / 10U),
                 (unsigned int)(((uint32_t)model->microphone_left_peak *
                                 1000U) / 32768U % 10U),
                 (unsigned int)(((uint32_t)model->microphone_right_peak *
                                 1000U) / 32768U / 10U),
                 (unsigned int)(((uint32_t)model->microphone_right_peak *
                                 1000U) / 32768U % 10U));
  ui_draw_text(226, 185, text, &Font8, UI_COLOR_MUTED, UI_COLOR_SURFACE);

  ui_fill_rect(290, 181, 29, 21,
               (model->touch_available != 0U) ? UI_COLOR_BLUE_SOFT : UI_COLOR_LINE);
  ui_draw_text(299, 183, "-", &Font16, UI_COLOR_BLUE,
               (model->touch_available != 0U) ? UI_COLOR_BLUE_SOFT : UI_COLOR_LINE);
  AppFontZh_DrawText(326, 183, zh_threshold,
                     (uint16_t)(sizeof(zh_threshold) /
                                sizeof(zh_threshold[0])),
                     UI_COLOR_AMBER, UI_COLOR_SURFACE);
  (void)snprintf(text, sizeof(text), "%u.%u %%FS",
                 model->trigger_threshold_permille / 10U,
                 model->trigger_threshold_permille % 10U);
  ui_draw_text(362, 185, text, &Font12, UI_COLOR_TEXT, UI_COLOR_SURFACE);
  ui_fill_rect(431, 181, 29, 21,
               (model->touch_available != 0U) ? UI_COLOR_BLUE_SOFT : UI_COLOR_LINE);
  ui_draw_text(440, 183, "+", &Font16, UI_COLOR_BLUE,
               (model->touch_available != 0U) ? UI_COLOR_BLUE_SOFT : UI_COLOR_LINE);

  BSP_LCD_SetTextColor(UI_COLOR_GRID);
  BSP_LCD_DrawHLine(17, 204, 446);
  BSP_LCD_DrawHLine(17, 222, 446);
  BSP_LCD_DrawHLine(17, 240, 446);
  for (i = 17U; i <= 457U; i = (uint16_t)(i + 55U))
  {
    BSP_LCD_DrawVLine(i, 202, 40);
  }

  if ((model->waveform == NULL) || (model->waveform_count < 2U))
  {
    return;
  }
  if (model->waveform_valid == 0U)
  {
    BSP_LCD_SetTextColor(UI_COLOR_BLUE_SOFT);
    BSP_LCD_DrawVLine((uint16_t)(17U +
                         ((model->frame_id * 13U) % 440U)), 204U, 37U);
    if (model->waveform_mode == APP_WAVEFORM_LIVE)
    {
      ui_draw_text(190, 214, "LIVE: WAIT AUDIO", &Font12,
                   UI_COLOR_MUTED, UI_COLOR_SURFACE);
    }
    else AppFontZh_DrawText(208, 214, zh_wait_trigger,
                       (uint16_t)(sizeof(zh_wait_trigger) /
                                  sizeof(zh_wait_trigger[0])),
                       UI_COLOR_MUTED, UI_COLOR_SURFACE);
    return;
  }

  {
    int32_t threshold = (int32_t)((32768UL *
        model->trigger_threshold_permille) / 1000UL);
    int32_t threshold_pixels = (threshold * 17) / waveform_scale;

    if ((threshold_pixels > 0) && (threshold_pixels <= 17))
    {
      BSP_LCD_SetTextColor(UI_COLOR_AMBER);
      BSP_LCD_DrawHLine(17, (uint16_t)(222 - threshold_pixels), 446);
      BSP_LCD_DrawHLine(17, (uint16_t)(222 + threshold_pixels), 446);
    }
  }

  BSP_LCD_SetTextColor(UI_COLOR_BLUE);
  BSP_LCD_DrawVLine(129, 204, 37);

  BSP_LCD_SetTextColor(UI_COLOR_BLUE_SOFT);
  BSP_LCD_DrawVLine((uint16_t)(17U +
                       ((model->frame_id * 13U) % 440U)), 204U, 37U);

  BSP_LCD_SetTextColor(UI_COLOR_BLUE_SOFT);
  for (i = 0U; i < model->waveform_count; i++)
  {
    uint16_t x = (uint16_t)(17U + ((uint32_t)i * 445U) /
                            (uint32_t)(model->waveform_count - 1U));
    int32_t glow = ((int32_t)model->waveform[i] * 15) /
                   waveform_scale;
    int16_t y;
    if (glow > 15) glow = 15;
    if (glow < -15) glow = -15;
    y = (int16_t)(222 - glow);

    if (i != 0U)
    {
      BSP_LCD_DrawLine(previous_x, (uint16_t)(previous_y - 2),
                       x, (uint16_t)(y - 2));
    }
    previous_x = x;
    previous_y = y;
  }

  previous_x = 17U;
  previous_y = 222;
  BSP_LCD_SetTextColor(UI_COLOR_WAVE);
  for (i = 0U; i < model->waveform_count; i++)
  {
    uint16_t x = (uint16_t)(17U + ((uint32_t)i * 445U) /
                            (uint32_t)(model->waveform_count - 1U));
    int32_t scaled = ((int32_t)model->waveform[i] * 17) /
                     waveform_scale;
    int16_t y;

    if (scaled > 17)
    {
      scaled = 17;
    }
    else if (scaled < -17)
    {
      scaled = -17;
    }
    y = (int16_t)(222 - scaled);

    if (i != 0U)
    {
      BSP_LCD_DrawLine(previous_x, (uint16_t)previous_y, x, (uint16_t)y);
      BSP_LCD_DrawLine(previous_x, (uint16_t)(previous_y + 1),
                       x, (uint16_t)(y + 1));
    }
    previous_x = x;
    previous_y = y;
  }
}

uint8_t AppUI_AdjustTemperatureByTouch(uint16_t x, uint16_t y)
{
  int16_t t;
  if (y < 44U || y > 66U) return 0U;
  if (x >= 422U && x <= 464U) {
    AppTemperature_SetAutomatic((uint8_t)!AppTemperature_IsAutomatic());
    return 1U;
  }
  t = AppTemperature_IsAutomatic() && AppTemperature_IsReady() ?
      AppTemperature_GetTemperature() : AppTemperature_GetManual();
  if (x >= 362U && x <= 389U) t = (t > -195) ? (int16_t)(t - 5) : -200;
  else if (x >= 392U && x <= 419U) t = (t < 595) ? (int16_t)(t + 5) : 600;
  else return 0U;
  AppTemperature_SetManual(t);
  return 1U;
}

uint8_t AppUI_AdjustTriggerByTouch(AppUiModel *model, uint16_t x, uint16_t y)
{
  uint16_t next;

  if ((model == NULL) || (y < 177U) || (y > 205U) ||
      (model->touch_available == 0U))
  {
    return 0U;
  }
  next = model->trigger_threshold_permille;
  if ((x >= 285U) && (x <= 323U))
  {
    next = (next > 5U) ? (uint16_t)(next - 5U) :
           APP_AUDIO_TRIGGER_MIN_PERMILLE;
  }
  else if ((x >= 427U) && (x <= 464U))
  {
    next = (next < (APP_AUDIO_TRIGGER_MAX_PERMILLE - 5U)) ?
           (uint16_t)(next + 5U) : APP_AUDIO_TRIGGER_MAX_PERMILLE;
  }
  if (next == model->trigger_threshold_permille)
  {
    return 0U;
  }
  model->trigger_threshold_permille = next;
  model->trigger_valid = 0U;
  return 1U;
}

static void ui_draw_footer(const AppUiModel *model)
{
  char text[64];
  uint32_t color = UI_COLOR_GREEN;
  const uint16_t *status_zh = zh_peer_ready;
  uint16_t status_zh_count =
      (uint16_t)(sizeof(zh_peer_ready) / sizeof(zh_peer_ready[0]));

  if (model->network_link_up == 0U)
  {
    color = UI_COLOR_RED;
    status_zh = zh_link_down;
    status_zh_count = (uint16_t)(sizeof(zh_link_down) /
                                 sizeof(zh_link_down[0]));
  }
  else if (model->network_has_address == 0U)
  {
    color = UI_COLOR_AMBER;
    status_zh = zh_autoip;
    status_zh_count = (uint16_t)(sizeof(zh_autoip) /
                                 sizeof(zh_autoip[0]));
  }
  else if (model->peer_online == 0U)
  {
    color = UI_COLOR_AMBER;
    status_zh = zh_discover;
    status_zh_count = (uint16_t)(sizeof(zh_discover) /
                                 sizeof(zh_discover[0]));
  }
  if (model->microphone_state == 2U)
  {
    color = UI_COLOR_RED;
    status_zh = zh_mic_error;
    status_zh_count = (uint16_t)(sizeof(zh_mic_error) /
                                 sizeof(zh_mic_error[0]));
  }
  else if (model->microphone_state == 0U)
  {
    color = UI_COLOR_AMBER;
    status_zh = zh_armed;
    status_zh_count = (uint16_t)(sizeof(zh_armed) /
                                 sizeof(zh_armed[0]));
  }
  else if (model->touch_available == 0U)
  {
    color = UI_COLOR_AMBER;
    status_zh = zh_touch_off;
    status_zh_count = (uint16_t)(sizeof(zh_touch_off) /
                                 sizeof(zh_touch_off[0]));
  }
  if (model->signal_clipped != 0U)
  {
    color = UI_COLOR_RED;
    status_zh = zh_clipped;
    status_zh_count = (uint16_t)(sizeof(zh_clipped) /
                                 sizeof(zh_clipped[0]));
  }

  ui_fill_rect(0, 254, UI_WIDTH, 18, UI_COLOR_HEADER);
  BSP_LCD_SetTextColor(UI_COLOR_BLUE);
  BSP_LCD_DrawHLine(0, 254, UI_WIDTH);
  ui_fill_rect(12, 260, 6, 6, color);
  AppFontZh_DrawText(24, 255, status_zh, status_zh_count,
                     LCD_COLOR_WHITE, UI_COLOR_HEADER);

  /* Local microphone arrival in the synchronized clock domain. Six
     fractional digits expose microseconds, without implying us accuracy. */
  if (model->arrival_valid != 0U)
  {
    (void)snprintf(text, sizeof(text), "TOA#%03lu %lu.%06lus",
        (unsigned long)(model->arrival_event_id % 1000U),
        (unsigned long)(model->arrival_ns / 1000000000ULL),
        (unsigned long)((model->arrival_ns % 1000000000ULL) / 1000ULL));
  }
  else
  {
    (void)strcpy(text, "TOA --- .------s");
  }
  ui_draw_text(112, 258, text, &Font8, UI_COLOR_TEXT, UI_COLOR_HEADER);

  (void)snprintf(text, sizeof(text), "RX %05lu | TX %05lu | DROP %03lu",
                 (unsigned long)(model->network_rx_count % 100000UL),
                 (unsigned long)(model->network_tx_count % 100000UL),
                 (unsigned long)(model->network_drop_count % 1000UL));
  ui_draw_text(294, 258, text, &Font8, LCD_COLOR_LIGHTGRAY, UI_COLOR_HEADER);
}

AppUiInitStatus AppUI_Init(void)
{
  if (BSP_LCD_Init() != LCD_OK)
  {
    return APP_UI_INIT_LTDC_ERROR;
  }
  if ((HAL_LTDC_GetState(&hLtdcHandler) != HAL_LTDC_STATE_READY) ||
      ((LTDC->GCR & LTDC_GCR_LTDCEN) == 0U))
  {
    return APP_UI_INIT_LTDC_ERROR;
  }
  if (ui_sdram_self_test() == 0U)
  {
    return APP_UI_INIT_SDRAM_ERROR;
  }

  ui_front_buffer = UI_FRAMEBUFFER_0_ADDRESS;
  ui_back_buffer = UI_FRAMEBUFFER_1_ADDRESS;
  ui_fill_buffer(ui_front_buffer, UI_COLOR_HEADER);
  ui_fill_buffer(ui_back_buffer, UI_COLOR_HEADER);

  BSP_LCD_LayerDefaultInit(0U, ui_front_buffer);
  BSP_LCD_SelectLayer(0U);
  BSP_LCD_DisplayOn();
  if ((ui_framebuffer_self_test(ui_front_buffer) == 0U) ||
      (ui_framebuffer_self_test(ui_back_buffer) == 0U))
  {
    return APP_UI_INIT_FRAMEBUFFER_ERROR;
  }
  ui_fill_buffer(ui_front_buffer, UI_COLOR_HEADER);
  ui_fill_buffer(ui_back_buffer, UI_COLOR_HEADER);
  ui_run_boot_animation();
  return APP_UI_INIT_OK;
}

void AppUI_Render(const AppUiModel *model)
{
  if (model == NULL)
  {
    return;
  }

  ui_begin_frame();
  BSP_LCD_Clear(UI_COLOR_BG);
  if (model->waveform_fullscreen != 0U)
  {
    ui_draw_waveform_fullscreen(model);
    ui_present_frame();
    return;
  }
  ui_draw_header(model);
  ui_draw_measurement(model);
  ui_draw_environment(model);
  ui_draw_waveform(model);
  ui_draw_footer(model);
  /*ui_draw_text(30, 50, "HELLO",
                 &Font24,
                 LCD_COLOR_RED,
                 UI_COLOR_BG);

// 画一个长方形：位置 (225,160)，宽60，高18
ui_fill_rect(225, 160, 60, 18, UI_COLOR_BLUE_SOFT);

// 在长方形里面写上 25C
ui_draw_text(232, 163, "25C", &Font12,
             UI_COLOR_TEXT, UI_COLOR_BLUE_SOFT);

*/
  ui_present_frame();
}

void AppUI_CreateDemo(AppUiModel *model, int16_t *waveform, uint16_t count)
{
  uint16_t i;

  if ((model == NULL) || (waveform == NULL))
  {
    return;
  }

  (void)memset(model, 0, sizeof(*model));
  model->role = APP_UI_ROLE_UNKNOWN;
  model->sync_state = APP_UI_SYNC_SEARCHING;
  model->direction = APP_UI_DIRECTION_A;
  model->quality = APP_UI_QUALITY_EXCELLENT;
  model->peer_online = 0U;
  model->measurement_valid = 1U;
  model->event_id = 42U;
  model->frame_id = 184U;
  model->distance_mm = 1234U;
  model->clock_offset_ns = 2400;
  model->clock_drift_ppb = -3100;
  model->temperature_deci_c = 250;
  model->sound_speed_mm_s = 346450U;
  model->correlation_per_mille = 964U;
  model->snr_deci_db = 278U;
  model->result_latency_ms = 86U;
  model->waveform = waveform;
  model->waveform_count = count;

  for (i = 0U; i < count; i++)
  {
    waveform[i] = (int16_t)(demo_wave_shape[i % 32U] * 28);
  }
}

void AppUI_StepDemo(AppUiModel *model, int16_t *waveform, uint16_t count)
{
  uint16_t i;
  int16_t first;

  if ((model == NULL) || (waveform == NULL) || (count < 2U))
  {
    return;
  }

  first = waveform[0];
  for (i = 0U; i < (uint16_t)(count - 1U); i++)
  {
    waveform[i] = waveform[i + 1U];
  }
  waveform[count - 1U] = first;
  model->frame_id++;
}
