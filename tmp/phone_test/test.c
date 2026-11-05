#include <stdint.h>
#include <math.h>
#include <string.h>
#define MAX_LAG 960
#define SAMPLES 3072
static struct {int16_t pcm[SAMPLES];}local_event,peer_event;
static uint8_t phone_lag_consistent(int candidate)
{
  int window,offset,i;
  for(window=0;window<3;window++) {
    int start=1024+320*window,best_offset=0;
    float best=-1.0f;
    for(offset=-8;offset<=8;offset++) {
      int lag=candidate+offset; float dot=0.0f,aa=0.0f,bb=0.0f,value;
      if(lag < -MAX_LAG || lag>MAX_LAG || start+lag<1 || start+384+lag>SAMPLES) continue;
      for(i=start;i<start+384;i++) {
        float a=(float)((int32_t)local_event.pcm[i]-local_event.pcm[i-1]);
        float b=(float)((int32_t)peer_event.pcm[i+lag]-peer_event.pcm[i+lag-1]);
        dot+=a*b; aa+=a*a; bb+=b*b;
      }
      value=(aa>1.0f && bb>1.0f)?dot/sqrtf(aa*bb):-1.0f;
      if(value>best) { best=value; best_offset=offset; }
    }
    if(best<0.55f || best_offset < -3 || best_offset>3) return 0U;
  }
  return 1U;
}

int match(const int16_t*a,const int16_t*b,int lag){memcpy(local_event.pcm,a,6144);memcpy(peer_event.pcm,b,6144);return phone_lag_consistent(lag);}