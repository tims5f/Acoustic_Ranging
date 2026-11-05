#ifndef NODE_A_H
#define NODE_A_H
#include <stdint.h>

/* Temperature in 0.1 C, sound speed in mm/s. One sensor serves both boards. */
void A_Init(void);
void A_Process(void);
void A_SetLocalSample(int16_t temperature, uint8_t valid);
void A_SetManual(int16_t temperature);
void A_SetAutomatic(uint8_t automatic);
int16_t A_GetTemperature(void);
int16_t A_GetManual(void);
uint32_t A_GetSoundSpeed(void);
uint8_t A_IsAutomatic(void);
uint8_t A_IsReady(void);
uint8_t A_GetSource(void); /* 0 manual, 1 master sensor, 2 slave sensor */
#endif
#ifndef A_SETTINGS_H
#define A_SETTINGS_H
#include <stdint.h>
#define APP_AUDIO_MODE_CLAP 0U
#define APP_AUDIO_MODE_PHONE 1U
uint8_t A_GetAudioMode(void);
void A_SetAudioMode(uint8_t mode);
uint8_t A_IsPending(void);
#endif

#ifndef NODE_B_H
#define NODE_B_H
#include <stdint.h>

/* Temperature in 0.1 C, sound speed in mm/s. One sensor serves both boards. */
void B_Init(void);
void B_Process(void);
void B_SetLocalSample(int16_t temperature, uint8_t valid);
void B_SetManual(int16_t temperature);
void B_SetAutomatic(uint8_t automatic);
int16_t B_GetTemperature(void);
int16_t B_GetManual(void);
uint32_t B_GetSoundSpeed(void);
uint8_t B_IsAutomatic(void);
uint8_t B_IsReady(void);
uint8_t B_GetSource(void); /* 0 manual, 1 master sensor, 2 slave sensor */
#endif
#ifndef B_SETTINGS_H
#define B_SETTINGS_H
#include <stdint.h>
#define APP_AUDIO_MODE_CLAP 0U
#define APP_AUDIO_MODE_PHONE 1U
uint8_t B_GetAudioMode(void);
void B_SetAudioMode(uint8_t mode);
uint8_t B_IsPending(void);
#endif

#include "mock.h"
#include "app_network.h"
#include <assert.h>
#include <stdio.h>
static int board, drop;
static uint32_t tick=1000;
static struct udp_pcb pcbs[2];
uint32_t HAL_GetTick(void) { return tick; }
AppNetworkRole AppNetwork_GetRole(void) { return board?APP_NETWORK_ROLE_SLAVE:APP_NETWORK_ROLE_MASTER; }
uint8_t AppNetwork_IsPeerOnline(void) { return 1; }
void AppNetwork_GetPeerIPv4(uint8_t ip[4]) { ip[0]=169; ip[1]=254; ip[2]=0; ip[3]=(uint8_t)(2-board); }
struct udp_pcb *udp_new(void) { pcbs[board].board=board; return &pcbs[board]; }
int udp_bind(struct udp_pcb*p,const ip_addr_t*a,u16_t port) { (void)p;(void)a;(void)port;return 0; }
void udp_recv(struct udp_pcb*p,void(*fn)(void*,struct udp_pcb*,struct pbuf*,const ip_addr_t*,u16_t),void*a) { (void)a;p->callback=fn; }
void udp_remove(struct udp_pcb*p) { (void)p; }
struct pbuf *pbuf_alloc(int a,int n,int b) { struct pbuf*p=calloc(1,sizeof(*p));(void)a;(void)b;p->tot_len=n;return p; }
int pbuf_take(struct pbuf*p,const void*d,int n) { memcpy(p->payload,d,n);return 0; }
int pbuf_copy_partial(struct pbuf*p,void*d,int n,int off) { memcpy(d,p->payload+off,n);return n; }
void pbuf_free(struct pbuf*p) { free(p); }
int udp_sendto(struct udp_pcb*p,struct pbuf*buf,const ip_addr_t*a,u16_t port) {
  int old=board; struct pbuf *copy; ip_addr_t source={{169,254,0,(uint8_t)(board+1)}};
  (void)p;(void)a;
  if(drop) { drop--;return 0; }
  copy=malloc(sizeof(*copy));*copy=*buf;board=1-board;
  if(pcbs[board].callback) pcbs[board].callback(NULL,&pcbs[board],copy,&source,port);else free(copy);
  board=old;return 0;
}
static void pump(int n) { while(n--) { tick+=250;board=0;A_Process();board=1;B_Process(); } }
int main(void) {
  board=0;A_Init();board=1;B_Init();pump(2);
  assert(!A_IsReady() && !B_IsReady());
  board=1;B_SetLocalSample(250,1);pump(4);
  assert(A_IsReady() && B_IsReady());
  assert(A_GetSource()==2 && B_GetSource()==2);
  assert(A_GetSoundSpeed()==346450 && A_GetSoundSpeed()==B_GetSoundSpeed());
  board=1;drop=2;B_SetManual(200);pump(8);
  assert(!A_IsAutomatic() && !B_IsAutomatic());
  assert(A_GetTemperature()==200 && B_GetTemperature()==200);
  assert(A_GetSoundSpeed()==343420 && A_GetSoundSpeed()==B_GetSoundSpeed());
  board=0;A_SetLocalSample(300,1);A_SetAutomatic(1);pump(4);
  assert(A_IsAutomatic() && B_IsAutomatic());
  assert(A_GetTemperature()==300 && B_GetTemperature()==300 && A_GetSource()==1);
  pump(16);assert(!A_IsReady() && !B_IsReady());
  board=0;A_SetManual(-5);pump(4);
  assert(A_IsReady() && B_IsReady() && A_GetSoundSpeed()==330997);
  assert(A_GetSoundSpeed()==B_GetSoundSpeed());
  board=1;B_SetManual(999);pump(4);assert(A_GetTemperature()==-5);
  board=0;A_SetAudioMode(1);pump(4);
  assert(A_GetAudioMode()==1 && B_GetAudioMode()==1);
  board=1;drop=3;B_SetAudioMode(0);pump(8);
  assert(A_GetAudioMode()==0 && B_GetAudioMode()==0 && !B_IsPending());
  assert(A_GetTemperature()==-5 && B_GetTemperature()==-5);
  puts("PASS: sensor on A/B, shared speed, manual from slave with packet loss, auto restore, stale samples, negative/invalid temperature");
  return 0;
}
