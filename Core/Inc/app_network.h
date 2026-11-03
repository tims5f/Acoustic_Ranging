#ifndef APP_NETWORK_H
#define APP_NETWORK_H

#include <stdint.h>

typedef enum
{
  APP_NETWORK_ROLE_UNKNOWN = 0,
  APP_NETWORK_ROLE_MASTER,
  APP_NETWORK_ROLE_SLAVE
} AppNetworkRole;

void AppNetwork_Init(void);
void AppNetwork_Process(void);
uint8_t AppNetwork_IsLinkUp(void);
uint8_t AppNetwork_HasAddress(void);
uint8_t AppNetwork_IsPeerOnline(void);
AppNetworkRole AppNetwork_GetRole(void);
void AppNetwork_GetIPv4(uint8_t address[4]);
void AppNetwork_GetPeerIPv4(uint8_t address[4]);
uint32_t AppNetwork_GetRxCount(void);
uint32_t AppNetwork_GetTxCount(void);
uint32_t AppNetwork_GetDropCount(void);

#endif
