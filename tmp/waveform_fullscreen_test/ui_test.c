
#include "app_ui.h"
#include "app_settings.h"
#include "app_audio.h"
#include <stdio.h>
#include <assert.h>
#include <math.h>
typedef struct { int Width, Height; } sFONT;
static sFONT Font8={5,8}, Font12={7,12}, Font16={11,16};
static uint8_t wave_mode, audio_mode;
static uint32_t color;
static int recording;
uint8_t AppSettings_GetWaveformMode(void) { return wave_mode; }
void AppSettings_SetWaveformMode(uint8_t v) { wave_mode=v; }
uint8_t AppSettings_GetAudioMode(void) { return audio_mode; }
void AppSettings_SetAudioMode(uint8_t v) { audio_mode=v; }
static void ui_fill_rect(int x,int y,int w,int h,uint32_t c) {
 assert(x>=0 && y>=0 && x+w<=480 && y+h<=272);
 if(recording) printf("R %d %d %d %d %u\n",x,y,w,h,c);
}
static void ui_draw_text(int x,int y,const char *s,sFONT *f,uint32_t c,uint32_t b) {
 int n=0; while(s[n]) n++;
 assert(x>=0 && y>=0 && x+n*f->Width<=480 && y+f->Height<=272);
 if(recording) printf("T %d %d %d %d %u %u %s\n",x,y,f->Width,f->Height,c,b,s);
}
static void BSP_LCD_SetTextColor(uint32_t c) { color=c; }
static void BSP_LCD_DrawLine(int x,int y,int a,int b) {
 assert(x>=0 && x<480 && a>=0 && a<480 && y>=0 && y<272 && b>=0 && b<272);
 if(recording) printf("L %d %d %d %d %u\n",x,y,a,b,color);
}
static void BSP_LCD_DrawHLine(int x,int y,int w) { BSP_LCD_DrawLine(x,y,x+w-1,y); }
static void BSP_LCD_DrawVLine(int x,int y,int h) { BSP_LCD_DrawLine(x,y,x,y+h-1); }

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


int main(void) {
 AppUiModel m={0}; int16_t pcm[160]; int i;
 m.touch_available=1; m.waveform=pcm; m.waveform_count=160; m.waveform_valid=1;
 m.role=APP_UI_ROLE_MASTER; m.microphone_state=1; m.sync_state=APP_UI_SYNC_LOCKED;
 m.trigger_threshold_permille=5;
 for(i=0;i<160;i++) pcm[i]=(int16_t)(6000*sin(i*0.17));
 assert(AppUI_HandleWaveformTouch(&m,30,190,0)==0 && !m.waveform_fullscreen);
 assert(AppUI_HandleWaveformTouch(&m,30,190,1)==1 && m.waveform_fullscreen);
 assert(AppUI_HandleWaveformTouch(&m,400,20,0)==0 && m.waveform_fullscreen);
 assert(AppUI_HandleWaveformTouch(&m,145,20,1)&APP_UI_TOUCH_WAVE_MODE);
 assert(wave_mode==APP_WAVEFORM_EVENT);
 assert(AppUI_HandleWaveformTouch(&m,225,20,1)&APP_UI_TOUCH_AUDIO_MODE);
 assert(audio_mode==APP_AUDIO_MODE_PHONE);
 assert(AppUI_HandleWaveformTouch(&m,80,240,1)==1);
 assert(m.waveform_amplitude_span>0 && m.waveform_amplitude_span<6000);
 for(i=0;i<20;i++) AppUI_HandleWaveformTouch(&m,80,240,0);
 assert(m.waveform_amplitude_span==64);
 for(i=0;i<20;i++) AppUI_HandleWaveformTouch(&m,20,240,0);
 assert(m.waveform_amplitude_span==32768);
 assert(AppUI_HandleWaveformTouch(&m,140,240,0)==0);
 assert(AppUI_HandleWaveformTouch(&m,140,240,1)==1 && !m.waveform_amplitude_span);
 for(i=0;i<50;i++) AppUI_HandleWaveformTouch(&m,210,240,0);
 assert(m.trigger_threshold_permille==APP_AUDIO_TRIGGER_MIN_PERMILLE);
 for(i=0;i<50;i++) AppUI_HandleWaveformTouch(&m,275,240,0);
 assert(m.trigger_threshold_permille==APP_AUDIO_TRIGGER_MAX_PERMILLE);
 assert(AppUI_HandleWaveformTouch(&m,30,80,1)==0);
 assert(AppUI_HandleWaveformTouch(&m,400,20,1)==1 && !m.waveform_fullscreen);
 assert(AppUI_HandleWaveformTouch(&m,275,240,1)==0);
 m.touch_available=0;
 assert(AppUI_HandleWaveformTouch(&m,30,190,1)==0);
 recording=1; m.trigger_threshold_permille=5;
 puts("SCENE live"); m.waveform_mode=APP_WAVEFORM_LIVE;
 ui_draw_waveform_fullscreen(&m);
 puts("SCENE clipped"); m.waveform_amplitude_span=512;
 for(i=0;i<160;i++) pcm[i]=(i%2)?32767:-32768;
 ui_draw_waveform_fullscreen(&m);
 puts("SCENE event_wait"); m.waveform_mode=APP_WAVEFORM_EVENT; m.waveform_valid=0;
 ui_draw_waveform_fullscreen(&m);
 puts("PASS touch actions, long press, limits, clipping and drawing bounds");
 return 0;
}
