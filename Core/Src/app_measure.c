#include "app_measure.h"
#include "app_clock.h"
#include "app_network.h"
#include "app_temperature.h"
#include "lwip/ip4_addr.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "stm32f7xx_hal.h"
#include <math.h>
#include <string.h>

#define PORT 50002U
#define MAGIC 0x4D455332UL
#define SAMPLES 3072U
#define PACKET_SIZE 1056U
#define MAX_LAG 960
typedef struct {
  uint64_t start_ns;
  uint64_t onset_ns;
  uint32_t id, received_ms;
  uint16_t peak;
  uint8_t valid, mask;
  uint8_t onset_valid;
  int16_t pcm[SAMPLES];
} MeasureEvent;
typedef struct {
  uint32_t local_id, peer_id, distance_mm;
  int32_t delta_ns;
  uint16_t local_peak, peer_peak;
} MeasureHistory;
static struct udp_pcb *measure_pcb;
static MeasureEvent local_event, peer_event;
static int16_t capture[APP_AUDIO_TRIGGER_CAPTURE_SAMPLES];
static AppMeasureResult ready_result;
static uint8_t result_pending, retransmits;
static uint8_t next_part, result_retries;
static uint8_t result_tx[32];
static uint32_t result_last_send_ms;
static volatile uint32_t measure_result_acks;
static uint32_t last_send_ms, last_paired_local_id, last_paired_peer_id;
static volatile MeasureHistory measure_history[32];
static volatile uint32_t measure_history_count, correlation_reject_count;
static volatile uint16_t last_correlation_permille;
static volatile int32_t last_alignment_lag, last_alignment_delta_ns;
static volatile int32_t last_peak_gap_permille;
static volatile uint8_t last_reject_reason;
static float scores[2*MAX_LAG+1];
static float envelope_a[SAMPLES], envelope_b[SAMPLES];
static float envelope_scores[2*MAX_LAG+1];
static volatile int32_t last_envelope_lag;
static volatile uint16_t last_envelope_correlation;
static uint8_t tx[PACKET_SIZE], rx[PACKET_SIZE];
static volatile int16_t debug_local_pcm[SAMPLES], debug_peer_pcm[SAMPLES];
static volatile uint8_t debug_pair_captured;
static volatile uint32_t measure_tx_packets, measure_tx_alloc_fail, measure_tx_send_fail;
static volatile uint32_t measure_rx_packets, measure_rx_crc_fail, measure_rx_filtered;

static void put32(uint8_t *p,uint32_t v) {
  p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v;
}
static uint32_t get32(const uint8_t *p) {
  return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static uint32_t crc32(const uint8_t *p,uint16_t n) {
  uint32_t crc=0xFFFFFFFFUL; uint16_t i; uint8_t bit;
  for(i=0;i<n;i++) { crc^=p[i]; for(bit=0;bit<8;bit++) crc=(crc>>1)^((crc&1U)?0xEDB88320UL:0U); }
  return ~crc;
}
static void transmit(uint8_t *data,uint16_t n) {
  uint8_t ip[4]; ip_addr_t address; struct pbuf *packet;
  if(measure_pcb==NULL || !AppNetwork_IsPeerOnline()) return;
  AppNetwork_GetPeerIPv4(ip); IP_ADDR4(&address,ip[0],ip[1],ip[2],ip[3]);
  put32(data+n-4,crc32(data,n-4));
  packet=pbuf_alloc(PBUF_TRANSPORT,n,PBUF_RAM);
  if(packet!=NULL) {
    if(pbuf_take(packet,data,n)==ERR_OK && udp_sendto(measure_pcb,packet,&address,PORT)==ERR_OK)
      measure_tx_packets++;
    else measure_tx_send_fail++;
    pbuf_free(packet);
  }
  else measure_tx_alloc_fail++;
}
static void send_event(void) {
  uint16_t part=next_part,i;
  next_part=(uint8_t)((next_part+1U)%6U);
  {
    memset(tx,0,28); put32(tx,MAGIC); tx[4]=3; tx[5]=1;
    tx[6]=(uint8_t)AppNetwork_GetRole(); tx[7]=(uint8_t)part;
    put32(tx+8,local_event.id); put32(tx+12,(uint32_t)(local_event.start_ns>>32));
    put32(tx+16,(uint32_t)local_event.start_ns);
    tx[20]=(uint8_t)(local_event.peak>>8); tx[21]=(uint8_t)local_event.peak;
    put32(tx+22,(uint32_t)(local_event.onset_ns-local_event.start_ns));
    tx[26]=local_event.onset_valid;
    for(i=0;i<512;i++) {
      uint16_t v=(uint16_t)local_event.pcm[part*512+i];
      tx[28+i*2]=(uint8_t)(v>>8); tx[29+i*2]=(uint8_t)v;
    }
    transmit(tx,PACKET_SIZE);
  }
}
static void record_result(int32_t delta) {
  uint32_t i=measure_history_count%32U;
  measure_history[i].local_id=local_event.id; measure_history[i].peer_id=peer_event.id;
  measure_history[i].distance_mm=ready_result.distance_mm; measure_history[i].delta_ns=delta;
  measure_history[i].local_peak=local_event.peak; measure_history[i].peer_peak=peer_event.peak;
  measure_history_count++;
}
static void receive_event(void *arg,struct udp_pcb *pcb,struct pbuf *packet,const ip_addr_t *address,u16_t port) {
  uint8_t ip[4]; uint16_t n,i; (void)arg; (void)pcb;
  if(packet==NULL) return;
  measure_rx_packets++;
  n=packet->tot_len; AppNetwork_GetPeerIPv4(ip);
  if((n!=PACKET_SIZE && n!=32U) || pbuf_copy_partial(packet,rx,n,0)!=n)
    { measure_rx_filtered++; pbuf_free(packet); return; }
  if(get32(rx+n-4)!=crc32(rx,n-4))
    { measure_rx_crc_fail++; pbuf_free(packet); return; }
  if(port!=PORT || get32(rx)!=MAGIC || rx[4]!=3 ||
     !AppNetwork_IsPeerOnline() || AppClock_GetState()!=APP_CLOCK_LOCKED ||
     ip[0]!=ip4_addr1(ip_2_ip4(address)) || ip[1]!=ip4_addr2(ip_2_ip4(address)) ||
     ip[2]!=ip4_addr3(ip_2_ip4(address)) || ip[3]!=ip4_addr4(ip_2_ip4(address))) {
    measure_rx_filtered++; pbuf_free(packet); return;
  }
  if(rx[5]==1 && n==PACKET_SIZE && rx[7]<6 && rx[6]!=(uint8_t)AppNetwork_GetRole()) {
    uint32_t id=get32(rx+8);
    if(peer_event.id!=id || peer_event.mask==0) {
      peer_event.id=id; peer_event.mask=0; peer_event.valid=0;
      peer_event.start_ns=((uint64_t)get32(rx+12)<<32)|get32(rx+16);
      peer_event.onset_ns=peer_event.start_ns+get32(rx+22);
      peer_event.onset_valid=rx[26];
      peer_event.peak=(uint16_t)(((uint16_t)rx[20]<<8)|rx[21]);
    }
    for(i=0;i<512;i++) peer_event.pcm[(uint16_t)rx[7]*512+i]=(int16_t)(((uint16_t)rx[28+i*2]<<8)|rx[29+i*2]);
    peer_event.mask|=(uint8_t)(1U<<rx[7]); peer_event.received_ms=HAL_GetTick();
    peer_event.valid=(peer_event.mask==63)?1U:0U;
  } else if(rx[5]==3 && n==32U && AppNetwork_GetRole()==APP_NETWORK_ROLE_MASTER &&
            get32(rx+8)==get32(result_tx+8) && get32(rx+12)==get32(result_tx+12)) {
    result_retries=0;
    measure_result_acks++;
  } else if(rx[5]==2 && n==32U && AppNetwork_GetRole()==APP_NETWORK_ROLE_SLAVE &&
            (get32(rx+12)==local_event.id || get32(rx+12)==last_paired_local_id)) {
    uint8_t ack[32]={0};
    uint32_t result_speed=get32(rx+23);
    if(result_speed<319180U || result_speed>367660U) { pbuf_free(packet); return; }
    put32(ack,MAGIC); ack[4]=3; ack[5]=3;
    put32(ack+8,get32(rx+8)); put32(ack+12,get32(rx+12));
    transmit(ack,32);
    if(get32(rx+8)==last_paired_peer_id || get32(rx+12)!=local_event.id) {
      pbuf_free(packet); return;
    }
    ready_result.event_id=local_event.id; ready_result.distance_mm=get32(rx+16);
    ready_result.sound_speed_mm_s=result_speed;
    ready_result.source_at_master=rx[7]; ready_result.clipped=rx[22];
    ready_result.peak=local_event.peak;
    ready_result.latency_ms=(uint16_t)(HAL_GetTick()-local_event.received_ms);
    last_correlation_permille=(uint16_t)(((uint16_t)rx[20]<<8)|rx[21]);
    ready_result.correlation_permille=last_correlation_permille;
    last_paired_local_id=local_event.id; last_paired_peer_id=get32(rx+8);
    result_pending=1; record_result(0);
  }
  pbuf_free(packet);
}
void AppMeasure_Init(void) {
  measure_pcb=udp_new();
  if(measure_pcb!=NULL) {
    if(udp_bind(measure_pcb,IP_ADDR_ANY,PORT)==ERR_OK) udp_recv(measure_pcb,receive_event,NULL);
    else { udp_remove(measure_pcb); measure_pcb=NULL; }
  }
}
void AppMeasure_OnAudioEvent(const AppAudioTriggerEvent *event) {
  if(event==NULL || AppClock_GetState()!=APP_CLOCK_LOCKED || !AppNetwork_IsPeerOnline() ||
     !AppAudio_CopyTriggerCapture(event->event_id,capture,APP_AUDIO_TRIGGER_CAPTURE_SAMPLES)) return;
  memcpy(local_event.pcm,capture,SAMPLES*sizeof(int16_t));
  local_event.start_ns=event->sample_time_ns-APP_AUDIO_TRIGGER_PRE_SAMPLES*1000000000ULL/APP_AUDIO_SAMPLE_RATE_HZ;
  local_event.id=event->event_id; local_event.peak=event->peak;
  local_event.onset_ns=event->arrival_ns;
  local_event.onset_valid=event->onset_valid;
  local_event.received_ms=HAL_GetTick(); local_event.valid=1; retransmits=18; next_part=0;
  last_send_ms=HAL_GetTick()-10U;
}
static uint8_t correlate_envelope(int *selected_lag) {
  int i,lag,edge=-1,start,end,best_lag=0;
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
  if(end-start<256) return 0;
  for(lag=-MAX_LAG;lag<=MAX_LAG;lag++) {
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
  for(lag=-MAX_LAG;lag<=MAX_LAG;lag++)
    if((lag<best_lag-48 || lag>best_lag+48) && envelope_scores[lag+MAX_LAG]>second)
      second=envelope_scores[lag+MAX_LAG];
  last_envelope_lag=best_lag;
  last_envelope_correlation=(best>0.0f)?(uint16_t)(best*1000.0f):0;
  if(best<0.75f || best-second<0.03f) return 0;
  *selected_lag=best_lag;
  return 1;
}
static uint8_t correlate(int64_t *delta) {
  int lag,i,best_lag=0; float best=-1.0f,second=-1.0f,fraction=0.0f;
  if(!debug_pair_captured) {
    for(i=0;i<SAMPLES;i++) { debug_local_pcm[i]=local_event.pcm[i]; debug_peer_pcm[i]=peer_event.pcm[i]; }
    debug_pair_captured=1;
  }
  for(lag=-MAX_LAG;lag<=MAX_LAG;lag++) {
    float dot=0.0f,aa=0.0f,bb=0.0f;
    for(i=1024;i<2048;i++) {
      float a=(float)((int32_t)local_event.pcm[i]-local_event.pcm[i-1]);
      float b=(float)((int32_t)peer_event.pcm[i+lag]-peer_event.pcm[i+lag-1]);
      dot+=a*b; aa+=a*a; bb+=b*b;
    }
    scores[lag+MAX_LAG]=(aa>1.0f && bb>1.0f)?dot/sqrtf(aa*bb):-1.0f;
    if(scores[lag+MAX_LAG]>best) { best=scores[lag+MAX_LAG]; best_lag=lag; }
  }
  for(lag=-MAX_LAG;lag<=MAX_LAG;lag++)
    if((lag<best_lag-8 || lag>best_lag+8) && scores[lag+MAX_LAG]>second) second=scores[lag+MAX_LAG];
  last_correlation_permille=(best>0.0f)?(uint16_t)(best*1000.0f):0;
  last_alignment_lag=best_lag;
  last_peak_gap_permille=(int32_t)((best-second)*1000.0f);
  last_reject_reason=0;
  if(best<0.50f) { last_reject_reason=1; return 0; }
  if(best-second<0.035f) {
    int envelope_lag,selected=0;
    float nearest=1.0e9f,next=1.0e9f;
    if(!correlate_envelope(&envelope_lag)) { last_reject_reason=2; return 0; }
    for(lag=-MAX_LAG+1;lag<MAX_LAG;lag++) {
      int j=lag+MAX_LAG;
      if(scores[j]>=best-0.05f && scores[j]>=scores[j-1] && scores[j]>scores[j+1]) {
        float distance=fabsf((float)(lag-envelope_lag));
        if(distance<nearest) { next=nearest; nearest=distance; selected=lag; }
        else if(distance<next) next=distance;
      }
    }
    if(nearest>6.0f || next-nearest<3.0f) { last_reject_reason=2; return 0; }
    best_lag=selected; best=scores[best_lag+MAX_LAG];
    last_alignment_lag=best_lag;
    last_correlation_permille=(uint16_t)(best*1000.0f);
  }
  if(best_lag==-MAX_LAG || best_lag==MAX_LAG) { last_reject_reason=2; return 0; }
  {
    int j=best_lag+MAX_LAG;
    float denominator=scores[j-1]-2.0f*best+scores[j+1];
    if(denominator < -0.000001f) fraction=0.5f*(scores[j-1]-scores[j+1])/denominator;
    if(fraction>0.5f || fraction < -0.5f) fraction=0.0f;
  }
  *delta=(int64_t)local_event.start_ns-(int64_t)peer_event.start_ns-
    (int64_t)(((float)best_lag+fraction)*(1000000000.0f/APP_AUDIO_SAMPLE_RATE_HZ));
  last_alignment_delta_ns=(int32_t)*delta;
  if(*delta>7500000LL || *delta < -7500000LL) last_reject_reason=3;
  return (*delta<=7500000LL && *delta>=-7500000LL)?1U:0U;
}
void AppMeasure_Process(void) {
  uint32_t now=HAL_GetTick(); int64_t delta; uint64_t magnitude;
  uint8_t result[32]={0};
  if(AppClock_GetState()!=APP_CLOCK_LOCKED || !AppNetwork_IsPeerOnline()) {
    local_event.valid=0; peer_event.valid=0; peer_event.mask=0; retransmits=0; result_retries=0; result_pending=0; return;
  }
  if(now-local_event.received_ms>500U) local_event.valid=0;
  if(now-peer_event.received_ms>500U) { peer_event.valid=0; peer_event.mask=0; }
  if(result_retries && now-result_last_send_ms>=40U) {
    transmit(result_tx,32); result_retries--; result_last_send_ms=now;
  }
  if(retransmits && local_event.valid && now-last_send_ms>=10U) { send_event(); retransmits--; last_send_ms=now; }
  if(AppNetwork_GetRole()!=APP_NETWORK_ROLE_MASTER || !local_event.valid || !peer_event.valid ||
     (local_event.id==last_paired_local_id && peer_event.id==last_paired_peer_id)) return;
  if((int64_t)local_event.start_ns-(int64_t)peer_event.start_ns>35000000LL ||
     (int64_t)peer_event.start_ns-(int64_t)local_event.start_ns>35000000LL) return;
  last_paired_local_id=local_event.id; last_paired_peer_id=peer_event.id;
  if(!correlate(&delta)) { correlation_reject_count++; return; }
  if(!AppTemperature_IsReady()) return;
  magnitude=(delta<0)?(uint64_t)-delta:(uint64_t)delta;
  ready_result.sound_speed_mm_s=AppTemperature_GetSoundSpeed();
  ready_result.distance_mm=(uint32_t)((magnitude*ready_result.sound_speed_mm_s+500000000ULL)/1000000000ULL);
  ready_result.event_id=local_event.id; ready_result.source_at_master=(delta<0)?1U:0U;
  ready_result.peak=(local_event.peak<peer_event.peak)?local_event.peak:peer_event.peak;
  ready_result.clipped=(local_event.peak>=32000U || peer_event.peak>=32000U)?1U:0U;
  ready_result.latency_ms=(uint16_t)(now-local_event.received_ms); record_result((int32_t)delta); result_pending=1;
  ready_result.correlation_permille=last_correlation_permille;
  put32(result,MAGIC); result[4]=3; result[5]=2; result[6]=APP_NETWORK_ROLE_MASTER;
  result[7]=ready_result.source_at_master; put32(result+8,local_event.id); put32(result+12,peer_event.id);
  put32(result+16,ready_result.distance_mm); result[20]=(uint8_t)(last_correlation_permille>>8);
  result[21]=(uint8_t)last_correlation_permille; result[22]=ready_result.clipped;
  put32(result+23,ready_result.sound_speed_mm_s);
  memcpy(result_tx,result,32);
  result_retries=10;
  result_last_send_ms=now-40U;
}
uint8_t AppMeasure_PopResult(AppMeasureResult *result) {
  if(result==NULL || !result_pending) return 0;
  *result=ready_result; result_pending=0; return 1;
}
void AppMeasure_GetDiagnostics(uint16_t *correlation,uint8_t *reject_reason) {
  if(correlation!=NULL) *correlation=last_correlation_permille;
  if(reject_reason!=NULL) *reject_reason=last_reject_reason;
}
