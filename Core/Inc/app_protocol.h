#ifndef APP_PROTOCOL_H
#define APP_PROTOCOL_H

#include <stdint.h>

#define APP_PROTOCOL_PACKET_SIZE 36U

typedef enum
{
  APP_PROTOCOL_DISCOVER = 1,
  APP_PROTOCOL_ANNOUNCE = 2,
  APP_PROTOCOL_HEARTBEAT = 3
} AppProtocolType;

typedef struct
{
  AppProtocolType type;
  uint32_t sequence;
  uint32_t node_id[3];
  uint32_t uptime_ms;
  uint8_t role;
} AppProtocolMessage;

uint16_t AppProtocol_Encode(uint8_t *buffer, uint16_t capacity,
                            const AppProtocolMessage *message);
uint8_t AppProtocol_Decode(AppProtocolMessage *message,
                           const uint8_t *buffer, uint16_t length);

#endif
