#include "app_audio_filter.h"

void AppAudioFilter_Reset(AppAudioFilter *f)
{
  f->hp1=0.0f; f->hp2=0.0f; f->lp1=0.0f; f->lp2=0.0f; f->noise=16.0f;
}
int16_t AppAudioFilter_Process(AppAudioFilter *f, int16_t sample)
{
  /* Two Butterworth biquads, 250 Hz high-pass then 8 kHz low-pass,
     fs=48 kHz. Identical fixed coefficients and persistent state on both boards.
     Direct form II transposed; stored a1/a2 use the subtract convention. */
  float x=(float)sample;
  float y=0.97712563f*x+f->hp1;
  float z, magnitude, limit;
  f->hp1=-1.95425125f*x+1.95372795f*y+f->hp2;
  f->hp2=0.97712563f*x-0.95477456f*y;
  z=0.15505103f*y+f->lp1;
  f->lp1=0.31010205f*y+0.62020410f*z+f->lp2;
  f->lp2=0.15505103f*y-0.24040821f*z;
  magnitude=(z<0.0f)?-z:z;
  /* Capped, slow upward adaptation prevents one clap from becoming the
     background level; continuous in-band noise is tracked over seconds. */
  limit=4.0f*f->noise+16.0f;
  if(magnitude>limit) magnitude=limit;
  f->noise+=(magnitude-f->noise)*((magnitude>f->noise)?0.00002083333f:0.00010416667f);
  if(f->noise<2.0f) f->noise=2.0f;
  if(z>32767.0f) z=32767.0f;
  if(z < -32768.0f) z=-32768.0f;
  return (int16_t)z;
}
uint16_t AppAudioFilter_Noise(const AppAudioFilter *f)
{
  return (uint16_t)f->noise;
}

uint8_t AppAudioFilter_IsImpulse(const int16_t *pcm, uint16_t count, uint16_t pre, uint16_t peak)
{
  uint16_t i; float early=0.0f,total=0.0f;
  if(pcm==0 || count<=pre+384U) return 0U;
  for(i=pre;i<count;i++) {
    float v=(float)pcm[i]; total+=v*v;
    if(i<pre+384U) early+=v*v;
  }
  return (uint8_t)(total>1.0f && early>0.50f*total &&
      (float)peak*(float)peak>8.0f*total/(count-pre));
}

uint8_t AppAudioFilter_PhoneQuality(const int16_t *pcm, uint16_t count, uint16_t pre)
{
  uint16_t i,j; float noise=0.0f,late=0.0f,maximum=0.0f;
  if(pcm==0 || pre<256U || count<=pre+384U) return 0U;
  for(i=0;i<256U;i++) { float v=(float)pcm[i]; noise+=v*v/256.0f; }
  if(noise<4.0f) noise=4.0f;
  for(i=pre;i+128U<=count;i=(uint16_t)(i+128U)) {
    float energy=0.0f;
    for(j=0;j<128U;j++) { float v=(float)pcm[i+j]; energy+=v*v/128.0f; }
    if(energy>maximum) maximum=energy;
  }
  for(i=(uint16_t)(count-384U);i<count;i++) { float v=(float)pcm[i]; late+=v*v/384.0f; }
  /* Standard phone bursts last beyond the 42ms post-trigger capture.
     Reject weak/transient candidates whose tail is merely background. */
  return (uint8_t)(maximum>noise*16.0f && late>noise*9.0f && late>maximum*0.0225f);
}
