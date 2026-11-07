"""Run actual UI hit handling on the host and record full-screen drawings."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
out = root / 'tmp/waveform_fullscreen_test'
out.mkdir(parents=True, exist_ok=True)
source = (root / 'Core/Src/app_ui.c').read_bytes().decode('gbk')
block = source[source.index('static int32_t ui_waveform_span('):
               source.index('static void ui_draw_waveform(const')]
colors = '\n'.join(line for line in source.splitlines()
                   if line.startswith('#define UI_COLOR_'))
prefix = r'''
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
'''
suffix = r'''
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
'''
cpath = out / 'ui_test.c'
cpath.write_text(prefix + '\n' + colors + '\n' + block + suffix)
exe = out / 'ui_test.exe'
subprocess.run(['D:/mingw64/bin/gcc.exe', '-O2', '-ICore/Inc', str(cpath),
                '-o', str(exe)], cwd=root, check=True)
result = subprocess.run([str(exe)], capture_output=True, text=True, check=True)
(out / 'drawings.txt').write_text(result.stdout)
from PIL import Image, ImageDraw, ImageFont

def rgb(v):
    return int(v) & 0xffffff

canvas = draw = None
name = None
for line in result.stdout.splitlines():
    if line.startswith('SCENE '):
        if canvas is not None:
            canvas.save(out / (name + '.png'))
        name = line.split()[1]
        canvas = Image.new('RGB', (480,272), '#07131f')
        draw = ImageDraw.Draw(canvas)
    elif line.startswith('R '):
        _,x,y,w,h,c=line.split(); x,y,w,h=map(int,(x,y,w,h))
        draw.rectangle((x,y,x+w-1,y+h-1), fill=f'#{rgb(c):06x}')
    elif line.startswith('L '):
        _,x,y,a,b,c=line.split()
        draw.line(tuple(map(int,(x,y,a,b))), fill=f'#{rgb(c):06x}')
    elif line.startswith('T '):
        _,x,y,w,h,c,b,label=line.split(' ',7)
        font=ImageFont.truetype('C:/Windows/Fonts/consola.ttf', int(h))
        draw.text((int(x),int(y)-2),label,font=font,fill=f'#{rgb(c):06x}')
if canvas is not None:
    canvas.save(out / (name + '.png'))
print(result.stdout.splitlines()[-1])
print('UI previews:', out)
