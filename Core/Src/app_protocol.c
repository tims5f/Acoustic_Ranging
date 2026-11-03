#include "app_protocol.h"

#include <stddef.h>

#define APP_PROTOCOL_MAGIC   0x52414E47UL
#define APP_PROTOCOL_VERSION 1U

static void protocol_write_u32(uint8_t *buffer, uint32_t value)
{
  buffer[0] = (uint8_t)(value >> 24);
  buffer[1] = (uint8_t)(value >> 16);
  buffer[2] = (uint8_t)(value >> 8);
  buffer[3] = (uint8_t)value;
}

static uint32_t protocol_read_u32(const uint8_t *buffer)
{
  return ((uint32_t)buffer[0] << 24) |
         ((uint32_t)buffer[1] << 16) |
         ((uint32_t)buffer[2] << 8) |
         (uint32_t)buffer[3];
}

static uint32_t protocol_crc32(const uint8_t *data, uint16_t length)
{
  uint32_t crc = 0xFFFFFFFFUL;
  uint16_t index;
  uint8_t bit;

  for (index = 0U; index < length; index++)
  {
    crc ^= data[index];
    for (bit = 0U; bit < 8U; bit++)
    {
      crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0UL);
    }
  }

  return ~crc;
}

uint16_t AppProtocol_Encode(uint8_t *buffer, uint16_t capacity,
                            const AppProtocolMessage *message)
{
  uint32_t crc;

  if ((buffer == NULL) || (message == NULL) ||
      (capacity < APP_PROTOCOL_PACKET_SIZE))
  {
    return 0U;
  }

  protocol_write_u32(&buffer[0], APP_PROTOCOL_MAGIC);
  buffer[4] = APP_PROTOCOL_VERSION;
  buffer[5] = (uint8_t)message->type;
  buffer[6] = 0U;
  buffer[7] = APP_PROTOCOL_PACKET_SIZE;
  protocol_write_u32(&buffer[8], message->sequence);
  protocol_write_u32(&buffer[12], message->node_id[0]);
  protocol_write_u32(&buffer[16], message->node_id[1]);
  protocol_write_u32(&buffer[20], message->node_id[2]);
  protocol_write_u32(&buffer[24], message->uptime_ms);
  buffer[28] = message->role;
  buffer[29] = 0U;
  buffer[30] = 0U;
  buffer[31] = 0U;
  crc = protocol_crc32(buffer, 32U);
  protocol_write_u32(&buffer[32], crc);

  return APP_PROTOCOL_PACKET_SIZE;
}

uint8_t AppProtocol_Decode(AppProtocolMessage *message,
                           const uint8_t *buffer, uint16_t length)
{
  uint32_t expected_crc;

  if ((message == NULL) || (buffer == NULL) ||
      (length != APP_PROTOCOL_PACKET_SIZE))
  {
    return 0U;
  }

  if ((protocol_read_u32(&buffer[0]) != APP_PROTOCOL_MAGIC) ||
      (buffer[4] != APP_PROTOCOL_VERSION) ||
      (buffer[6] != 0U) ||
      (buffer[7] != APP_PROTOCOL_PACKET_SIZE) ||
      (buffer[5] < APP_PROTOCOL_DISCOVER) ||
      (buffer[5] > APP_PROTOCOL_HEARTBEAT))
  {
    return 0U;
  }

  expected_crc = protocol_crc32(buffer, 32U);
  if (protocol_read_u32(&buffer[32]) != expected_crc)
  {
    return 0U;
  }

  message->type = (AppProtocolType)buffer[5];
  message->sequence = protocol_read_u32(&buffer[8]);
  message->node_id[0] = protocol_read_u32(&buffer[12]);
  message->node_id[1] = protocol_read_u32(&buffer[16]);
  message->node_id[2] = protocol_read_u32(&buffer[20]);
  message->uptime_ms = protocol_read_u32(&buffer[24]);
  message->role = buffer[28];

  return 1U;
}
