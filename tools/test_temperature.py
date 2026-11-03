"""Host test of the actual temperature C module as two networked nodes."""
from pathlib import Path
import re, subprocess
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'tmp/temperature_test'
OUT.mkdir(parents=True, exist_ok=True)
(OUT/'lwip').mkdir(exist_ok=True)
(OUT/'mock.h').write_text(r"""
#ifndef MOCK_H
#define MOCK_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef uint16_t u16_t;
typedef struct { uint8_t ip[4]; } ip_addr_t;
struct pbuf { uint16_t tot_len; uint8_t payload[64]; };
struct udp_pcb { int board; void (*callback)(void*,struct udp_pcb*,struct pbuf*,const ip_addr_t*,u16_t); };
#define IP_ADDR_ANY NULL
#define ERR_OK 0
#define PBUF_TRANSPORT 0
#define PBUF_RAM 0
#define ip_2_ip4(x) (x)
#define ip4_addr1(x) ((x)->ip[0])
#define ip4_addr2(x) ((x)->ip[1])
#define ip4_addr3(x) ((x)->ip[2])
#define ip4_addr4(x) ((x)->ip[3])
#define IP_ADDR4(p,a,b,c,d) do { (p)->ip[0]=(a); (p)->ip[1]=(b); (p)->ip[2]=(c); (p)->ip[3]=(d); } while(0)
uint32_t HAL_GetTick(void);
struct udp_pcb *udp_new(void);
int udp_bind(struct udp_pcb*,const ip_addr_t*,u16_t);
void udp_recv(struct udp_pcb*,void(*)(void*,struct udp_pcb*,struct pbuf*,const ip_addr_t*,u16_t),void*);
void udp_remove(struct udp_pcb*);
struct pbuf *pbuf_alloc(int,int,int);
int pbuf_take(struct pbuf*,const void*,int);
int pbuf_copy_partial(struct pbuf*,void*,int,int);
int udp_sendto(struct udp_pcb*,struct pbuf*,const ip_addr_t*,u16_t);
void pbuf_free(struct pbuf*);
#endif
""")
for file in ['stm32f7xx_hal.h','lwip/udp.h','lwip/pbuf.h','lwip/ip4_addr.h']:
    (OUT/file).write_text('#include "mock.h"\n')
header=(ROOT/'Core/Inc/app_temperature.h').read_text()
names=re.findall(r'\b(AppTemperature_\w+)\(',header)
prototypes='\n'.join(re.sub(r'AppTemperature_',tag+'_',header) for tag in ['A','B'])
# Distinct include guards for the two compiled node interfaces.
prototypes=prototypes.replace('APP_TEMPERATURE_H','NODE_A_H',2).replace('APP_TEMPERATURE_H','NODE_B_H')
harness=r"""
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
  puts("PASS: sensor on A/B, shared speed, manual from slave with packet loss, auto restore, stale samples, negative/invalid temperature");
  return 0;
}
"""
(OUT/'test.c').write_text(prototypes+harness)
gcc=Path('D:/mingw64/bin/gcc.exe')
base=[str(gcc),'-std=c99','-Wall','-Wextra','-Werror','-I'+str(OUT),'-I'+str(ROOT/'Core/Inc')]
for tag in ['A','B']:
    flags=['-D'+name+'='+name.replace('AppTemperature_',tag+'_') for name in names]
    subprocess.run(base+flags+['-c',str(ROOT/'Core/Src/app_temperature.c'),'-o',str(OUT/(tag+'.o'))],check=True)
subprocess.run(base+[str(OUT/'test.c'),str(OUT/'A.o'),str(OUT/'B.o'),'-o',str(OUT/'test.exe')],check=True)
subprocess.run([str(OUT/'test.exe')],check=True)
