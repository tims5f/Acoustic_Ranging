#include <stdint.h>
#include <math.h>
#include <string.h>
#define SAMPLES 3072
#define MAX_LAG 960
#define APP_AUDIO_TRIGGER_PRE_SAMPLES 1024
static struct {int16_t pcm[SAMPLES];} local_event,peer_event;
static float envelope_a[SAMPLES],envelope_b[SAMPLES],envelope_scores[1921];
static void service_measure_network(int lag){(void)lag;} static int last_envelope_lag; static uint16_t last_envelope_correlation;
static uint8_t correlate_envelope(int *selected_lag, float minimum_correlation, uint8_t impulse_mode) {
  int i,lag,edge=-1,start,end,best_lag=0;
  int lag_limit=impulse_mode?240:MAX_LAG;
  float aa=0.0f,bb=0.0f,base_a=0.0f,base_b=0.0f,peak_a=0.0f,peak_b=0.0f;
  float best=-1.0f,second=-1.0f,gate;
  for(i=0;i<SAMPLES;i++) {
    float a=(float)local_event.pcm[i],b=(float)peer_event.pcm[i];
    aa+=a*a; bb+=b*b;
    if(i>=48) {
      a=(float)local_event.pcm[i-48]; b=(float)peer_event.pcm[i-48];
      aa-=a*a; bb-=b*b;
    }
    envelope_a[i]=sqrtf((aa>0.0f)?aa/48.0f:0.0f);
    envelope_b[i]=sqrtf((bb>0.0f)?bb/48.0f:0.0f);
    if(i<128) { base_a+=envelope_a[i]/128.0f; base_b+=envelope_b[i]/128.0f; }
    if(envelope_a[i]>peak_a) peak_a=envelope_a[i];
    if(envelope_b[i]>peak_b) peak_b=envelope_b[i];
  }
  if(peak_a<base_a*4.0f || peak_b<base_b*4.0f || peak_a<32.0f || peak_b<32.0f) return 0;
  gate=base_a+0.1f*(peak_a-base_a);
  for(i=128;i<SAMPLES;i++) if(envelope_a[i]>gate) { edge=i; break; }
  if(edge<0) return 0;
  start=(edge>224)?edge-96:128;
  end=edge+384;
  if(end>=SAMPLES) end=SAMPLES-1;
  if(impulse_mode) {
    /* Both captures are anchored at their own trigger. Match the main
       attack around that anchor, not an arbitrary pretrigger fluctuation
       or a reflection many milliseconds later. The propagation time is
       carried separately in start_ns, so this is not a distance limit. */
    start=APP_AUDIO_TRIGGER_PRE_SAMPLES-96;
    end=APP_AUDIO_TRIGGER_PRE_SAMPLES+384;
  }
  if(end-start<256) return 0;
  for(lag=-lag_limit;lag<=lag_limit;lag++) {
    service_measure_network(lag);
    float dot=0.0f,energy_a=0.0f,energy_b=0.0f,value=-1.0f;
    if(start+lag>=24 && end+lag<SAMPLES) {
      for(i=start;i<end;i++) {
        float a=envelope_a[i]-envelope_a[i-24];
        float b=envelope_b[i+lag]-envelope_b[i+lag-24];
        dot+=a*b; energy_a+=a*a; energy_b+=b*b;
      }
      if(energy_a>1.0f && energy_b>1.0f) value=dot/sqrtf(energy_a*energy_b);
    }
    envelope_scores[lag+MAX_LAG]=value;
    if(value>best) { best=value; best_lag=lag; }
  }
  for(lag=-lag_limit;lag<=lag_limit;lag++)
    if((lag<best_lag-48 || lag>best_lag+48) && envelope_scores[lag+MAX_LAG]>second)
      second=envelope_scores[lag+MAX_LAG];
  last_envelope_lag=best_lag;
  last_envelope_correlation=(best>0.0f)?(uint16_t)(best*1000.0f):0;
  if(best<minimum_correlation || best-second<0.03f) return 0;
  *selected_lag=best_lag;
  return 1;
}
#if defined(__ICCARM__)
#pragma optimize=high
#endif
int match(const int16_t *a,const int16_t *b,int *lag,int *corr) { int ok;memcpy(local_event.pcm,a,6144);memcpy(peer_event.pcm,b,6144);ok=correlate_envelope(lag,0.60f,1);*corr=last_envelope_correlation;return ok;}