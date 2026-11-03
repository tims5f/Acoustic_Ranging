
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
