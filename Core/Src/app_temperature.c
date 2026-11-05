#include "app_temperature.h"
#include "app_settings.h"
#include "app_network.h"
#include "stm32f7xx_hal.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/ip4_addr.h"
#include <string.h>

#define TEMP_PORT 50003U
#define TEMP_MAGIC 0x54454D31UL
#define TEMP_TIMEOUT_MS 3000U
static struct udp_pcb *temperature_pcb;
static int16_t local_temperature, peer_temperature;
static int16_t manual_temperature = 250, selected_temperature = 250;
static uint8_t local_valid, peer_valid, automatic = 1U, ready, source;
static uint8_t command_pending, master_ready;
static uint32_t local_tick, peer_tick, last_send_tick, command_sequence;
static uint32_t peer_command_sequence;
static uint8_t peer_command_seen;
static uint8_t audio_mode=APP_AUDIO_MODE_CLAP;

static void put32(uint8_t *p, uint32_t v) {
  p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v;
}
static uint32_t get32(const uint8_t *p) {
  return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static void put16(uint8_t *p, int16_t v) { p[0]=(uint8_t)((uint16_t)v>>8); p[1]=(uint8_t)v; }
static int16_t get16(const uint8_t *p) { return (int16_t)(((uint16_t)p[0]<<8)|p[1]); }
static uint32_t crc32(const uint8_t *p) {
  uint32_t c=0xFFFFFFFFUL; uint8_t i,b;
  for(i=0;i<28;i++) { c^=p[i]; for(b=0;b<8;b++) c=(c>>1)^((c&1U)?0xEDB88320UL:0U); }
  return ~c;
}
static uint8_t in_range(int16_t t) { return (t>=-200 && t<=600)?1U:0U; }
static void receive_temperature(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                                const ip_addr_t *address, u16_t port) {
  uint8_t data[32],ip[4]; AppNetworkRole role=AppNetwork_GetRole();
  (void)arg; (void)pcb;
  if(p==NULL) return;
  AppNetwork_GetPeerIPv4(ip);
  if(p->tot_len!=32 || pbuf_copy_partial(p,data,32,0)!=32 ||
     port!=TEMP_PORT || !AppNetwork_IsPeerOnline() || get32(data)!=TEMP_MAGIC ||
     data[4]!=2 || data[26]>APP_AUDIO_MODE_PHONE || data[5]==(uint8_t)role ||
     (data[5]!=APP_NETWORK_ROLE_MASTER && data[5]!=APP_NETWORK_ROLE_SLAVE) ||
     get32(data+28)!=crc32(data) ||
     ip[0]!=ip4_addr1(ip_2_ip4(address)) || ip[1]!=ip4_addr2(ip_2_ip4(address)) ||
     ip[2]!=ip4_addr3(ip_2_ip4(address)) || ip[3]!=ip4_addr4(ip_2_ip4(address))) {
    pbuf_free(p); return;
  }
  peer_temperature=get16(data+10);
  peer_valid=(uint8_t)(data[7] && in_range(peer_temperature));
  peer_tick=HAL_GetTick();
  if(role==APP_NETWORK_ROLE_MASTER && data[8]) {
    uint32_t seq=get32(data+16);
    if(!peer_command_seen || seq!=peer_command_sequence) {
      if(in_range(get16(data+12)) && data[6]<=1) {
        automatic=data[6]; manual_temperature=get16(data+12); audio_mode=data[26];
        peer_command_sequence=seq; peer_command_seen=1;
      }
    }
  } else if(role==APP_NETWORK_ROLE_SLAVE && data[5]==APP_NETWORK_ROLE_MASTER) {
    if(command_pending && data[25] && get32(data+20)==command_sequence) command_pending=0;
    if(!command_pending && data[6]<=1 && in_range(get16(data+12))) {
      automatic=data[6]; manual_temperature=get16(data+12); audio_mode=data[26];
    }
    selected_temperature=get16(data+14);
    master_ready=(uint8_t)(data[24] && in_range(selected_temperature)); source=data[9];
  }
  pbuf_free(p);
}
void AppTemperature_Init(void) {
  temperature_pcb=udp_new();
  if(temperature_pcb!=NULL && udp_bind(temperature_pcb,IP_ADDR_ANY,TEMP_PORT)==ERR_OK)
    udp_recv(temperature_pcb,receive_temperature,NULL);
  else if(temperature_pcb!=NULL) { udp_remove(temperature_pcb); temperature_pcb=NULL; }
}
void AppTemperature_SetLocalSample(int16_t t,uint8_t valid) {
  local_valid=(uint8_t)(valid && in_range(t));
  if(local_valid) { local_temperature=t; local_tick=HAL_GetTick(); }
}
static void changed(void) {
  command_sequence++; command_pending=(AppNetwork_GetRole()!=APP_NETWORK_ROLE_MASTER)?1U:0U;
  last_send_tick=HAL_GetTick()-250U;
}
void AppTemperature_SetManual(int16_t t) {
  if(!in_range(t)) return;
  manual_temperature=t; automatic=0; changed();
}
void AppTemperature_SetAutomatic(uint8_t enabled) { automatic=enabled?1U:0U; changed(); }
void AppTemperature_Process(void) {
  uint32_t now=HAL_GetTick(); AppNetworkRole role=AppNetwork_GetRole();
  if(now-local_tick>TEMP_TIMEOUT_MS) local_valid=0;
  if(!AppNetwork_IsPeerOnline() || now-peer_tick>TEMP_TIMEOUT_MS) {
    peer_valid=0; master_ready=0; peer_command_seen=0;
  }
  if(role==APP_NETWORK_ROLE_MASTER || !AppNetwork_IsPeerOnline()) {
    if(!automatic) { selected_temperature=manual_temperature; ready=1; source=0; }
    else if(local_valid) { selected_temperature=local_temperature; ready=1; source=1; }
    else if(peer_valid) { selected_temperature=peer_temperature; ready=1; source=2; }
    else ready=0;
    if(role==APP_NETWORK_ROLE_MASTER) command_pending=0;
  } else ready=(uint8_t)(master_ready && !command_pending);
  if(temperature_pcb!=NULL && AppNetwork_IsPeerOnline() && now-last_send_tick>=250U) {
    uint8_t data[32]={0},ip[4]; ip_addr_t address; struct pbuf *p;
    last_send_tick=now;
    put32(data,TEMP_MAGIC); data[4]=2; data[5]=(uint8_t)role;
    data[6]=automatic; data[7]=local_valid; data[8]=command_pending; data[9]=source;
    put16(data+10,local_temperature); put16(data+12,manual_temperature);
    put16(data+14,selected_temperature); put32(data+16,command_sequence);
    put32(data+20,peer_command_sequence); data[24]=ready; data[25]=peer_command_seen; data[26]=audio_mode;
    put32(data+28,crc32(data));
    AppNetwork_GetPeerIPv4(ip); IP_ADDR4(&address,ip[0],ip[1],ip[2],ip[3]);
    p=pbuf_alloc(PBUF_TRANSPORT,32,PBUF_RAM);
    if(p!=NULL) { if(pbuf_take(p,data,32)==ERR_OK) (void)udp_sendto(temperature_pcb,p,&address,TEMP_PORT); pbuf_free(p); }
  }
}
int16_t AppTemperature_GetTemperature(void) { return selected_temperature; }
int16_t AppTemperature_GetManual(void) { return manual_temperature; }
uint32_t AppTemperature_GetSoundSpeed(void) {
  /* c = 331.3 + 0.606*T m/s, rounded once to mm/s. */
  return (uint32_t)(331300L+(606L*selected_temperature+((selected_temperature>=0)?5:-5))/10L);
}
uint8_t AppTemperature_IsAutomatic(void) { return automatic; }
uint8_t AppTemperature_IsReady(void) { return ready; }
uint8_t AppTemperature_GetSource(void) { return source; }

uint8_t AppSettings_GetAudioMode(void) { return audio_mode; }
void AppSettings_SetAudioMode(uint8_t mode) {
  if(mode>APP_AUDIO_MODE_PHONE || mode==audio_mode) return;
  audio_mode=mode; changed();
}
uint8_t AppSettings_IsPending(void) { return command_pending; }
