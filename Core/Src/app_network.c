#include "app_network.h"

#include "app_protocol.h"
#include "ethernetif.h"
#include "lwip.h"
#include "lwip/autoip.h"
#include "lwip/ip.h"
#include "lwip/ip4_addr.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

#define APP_NETWORK_PORT              50000U
#define APP_NETWORK_DISCOVER_PERIOD   500U
#define APP_NETWORK_HEARTBEAT_PERIOD  200U
#define APP_NETWORK_PEER_TIMEOUT      1500U

extern struct netif gnetif;

static struct udp_pcb *network_pcb;
static ip_addr_t peer_address;
static uint32_t local_node_id[3];
static uint32_t peer_node_id[3];
static uint32_t sequence_number;
static uint32_t last_peer_sequence;
static uint32_t last_peer_tick;
static uint32_t last_discover_tick;
static uint32_t last_heartbeat_tick;
static uint32_t last_link_check_tick;
static uint32_t rx_count;
static uint32_t tx_count;
static uint32_t drop_count;
static uint8_t peer_online;
static uint8_t peer_sequence_valid;
static uint8_t previous_link_state;
static AppNetworkRole local_role;

static uint8_t network_node_is_equal(const uint32_t first[3],
                                     const uint32_t second[3])
{
  return ((first[0] == second[0]) && (first[1] == second[1]) &&
          (first[2] == second[2])) ? 1U : 0U;
}

static AppNetworkRole network_elect_role(const uint32_t peer_id[3])
{
  uint8_t index;

  for (index = 0U; index < 3U; index++)
  {
    if (local_node_id[index] < peer_id[index])
    {
      return APP_NETWORK_ROLE_MASTER;
    }
    if (local_node_id[index] > peer_id[index])
    {
      return APP_NETWORK_ROLE_SLAVE;
    }
  }

  return APP_NETWORK_ROLE_UNKNOWN;
}

static void network_clear_peer(void)
{
  peer_online = 0U;
  peer_sequence_valid = 0U;
  local_role = APP_NETWORK_ROLE_UNKNOWN;
  peer_node_id[0] = 0U;
  peer_node_id[1] = 0U;
  peer_node_id[2] = 0U;
  ip_addr_set_zero(&peer_address);
}

static void network_send(AppProtocolType type, const ip_addr_t *destination)
{
  AppProtocolMessage message;
  struct pbuf *packet;
  uint8_t buffer[APP_PROTOCOL_PACKET_SIZE];
  uint16_t length;

  if ((network_pcb == NULL) || (destination == NULL))
  {
    return;
  }

  message.type = type;
  message.sequence = ++sequence_number;
  message.node_id[0] = local_node_id[0];
  message.node_id[1] = local_node_id[1];
  message.node_id[2] = local_node_id[2];
  message.uptime_ms = HAL_GetTick();
  message.role = (uint8_t)local_role;
  length = AppProtocol_Encode(buffer, sizeof(buffer), &message);
  if (length == 0U)
  {
    return;
  }

  packet = pbuf_alloc(PBUF_TRANSPORT, length, PBUF_RAM);
  if (packet == NULL)
  {
    return;
  }

  if ((pbuf_take(packet, buffer, length) == ERR_OK) &&
      (udp_sendto(network_pcb, packet, destination,
                  APP_NETWORK_PORT) == ERR_OK))
  {
    tx_count++;
  }
  pbuf_free(packet);
}

static void network_receive(void *argument, struct udp_pcb *pcb,
                            struct pbuf *packet, const ip_addr_t *address,
                            u16_t port)
{
  AppProtocolMessage message;
  uint8_t buffer[APP_PROTOCOL_PACKET_SIZE];
  uint32_t sequence_delta;

  (void)argument;
  (void)pcb;
  (void)port;

  if (packet == NULL)
  {
    return;
  }

  if ((packet->tot_len == APP_PROTOCOL_PACKET_SIZE) &&
      (pbuf_copy_partial(packet, buffer, APP_PROTOCOL_PACKET_SIZE, 0U) ==
       APP_PROTOCOL_PACKET_SIZE) &&
      (AppProtocol_Decode(&message, buffer, APP_PROTOCOL_PACKET_SIZE) != 0U) &&
      (network_node_is_equal(local_node_id, message.node_id) == 0U))
  {
    if ((peer_online == 0U) ||
        (network_node_is_equal(peer_node_id, message.node_id) != 0U))
    {
      peer_node_id[0] = message.node_id[0];
      peer_node_id[1] = message.node_id[1];
      peer_node_id[2] = message.node_id[2];
      ip_addr_copy(peer_address, *address);
      local_role = network_elect_role(peer_node_id);
      last_peer_tick = HAL_GetTick();
      peer_online = 1U;
      rx_count++;

      if (peer_sequence_valid != 0U)
      {
        sequence_delta = message.sequence - last_peer_sequence;
        if ((sequence_delta > 1U) && (sequence_delta < 0x80000000UL))
        {
          drop_count += sequence_delta - 1U;
        }
      }
      last_peer_sequence = message.sequence;
      peer_sequence_valid = 1U;

      if (message.type == APP_PROTOCOL_DISCOVER)
      {
        network_send(APP_PROTOCOL_ANNOUNCE, address);
      }
    }
  }

  pbuf_free(packet);
}

void AppNetwork_Init(void)
{
  local_node_id[0] = HAL_GetUIDw0();
  local_node_id[1] = HAL_GetUIDw1();
  local_node_id[2] = HAL_GetUIDw2();
  network_clear_peer();

  MX_LWIP_Init();
  autoip_start(&gnetif);

  network_pcb = udp_new();
  if ((network_pcb != NULL) &&
      (udp_bind(network_pcb, IP_ADDR_ANY, APP_NETWORK_PORT) == ERR_OK))
  {
    ip_set_option(network_pcb, SOF_BROADCAST);
    udp_recv(network_pcb, network_receive, NULL);
  }
  else if (network_pcb != NULL)
  {
    udp_remove(network_pcb);
    network_pcb = NULL;
  }

  previous_link_state = AppNetwork_IsLinkUp();
  last_link_check_tick = HAL_GetTick();
  last_discover_tick = HAL_GetTick() - APP_NETWORK_DISCOVER_PERIOD;
  last_heartbeat_tick = HAL_GetTick();
}

void AppNetwork_Process(void)
{
  ip_addr_t broadcast_address;
  uint32_t now;
  uint8_t link_state;

  MX_LWIP_Process();
  now = HAL_GetTick();
  if ((now - last_link_check_tick) >= 100U)
  {
    last_link_check_tick = now;
    ethernet_link_update(&gnetif);
  }
  link_state = AppNetwork_IsLinkUp();

  if (link_state != previous_link_state)
  {
    previous_link_state = link_state;
    network_clear_peer();
    if (link_state != 0U)
    {
      autoip_start(&gnetif);
      last_discover_tick = now - APP_NETWORK_DISCOVER_PERIOD;
    }
    else
    {
      autoip_stop(&gnetif);
    }
  }

  if ((link_state == 0U) || (AppNetwork_HasAddress() == 0U) ||
      (network_pcb == NULL))
  {
    return;
  }

  if ((peer_online != 0U) &&
      ((now - last_peer_tick) >= APP_NETWORK_PEER_TIMEOUT))
  {
    network_clear_peer();
    last_discover_tick = now - APP_NETWORK_DISCOVER_PERIOD;
  }

  if (peer_online == 0U)
  {
    if ((now - last_discover_tick) >= APP_NETWORK_DISCOVER_PERIOD)
    {
      IP_ADDR4(&broadcast_address, 169, 254, 255, 255);
      network_send(APP_PROTOCOL_DISCOVER, &broadcast_address);
      last_discover_tick = now;
    }
  }
  else if ((now - last_heartbeat_tick) >= APP_NETWORK_HEARTBEAT_PERIOD)
  {
    network_send(APP_PROTOCOL_HEARTBEAT, &peer_address);
    last_heartbeat_tick = now;
  }
}

uint8_t AppNetwork_IsLinkUp(void)
{
  return netif_is_link_up(&gnetif) ? 1U : 0U;
}

uint8_t AppNetwork_HasAddress(void)
{
  return (!ip4_addr_isany_val(*netif_ip4_addr(&gnetif))) ? 1U : 0U;
}

uint8_t AppNetwork_IsPeerOnline(void)
{
  return peer_online;
}

AppNetworkRole AppNetwork_GetRole(void)
{
  return local_role;
}

void AppNetwork_GetIPv4(uint8_t address[4])
{
  const ip4_addr_t *ip;

  if (address == NULL)
  {
    return;
  }

  ip = netif_ip4_addr(&gnetif);
  address[0] = ip4_addr1(ip);
  address[1] = ip4_addr2(ip);
  address[2] = ip4_addr3(ip);
  address[3] = ip4_addr4(ip);
}

void AppNetwork_GetPeerIPv4(uint8_t address[4])
{
  const ip4_addr_t *ip;

  if (address == NULL)
  {
    return;
  }

  ip = ip_2_ip4(&peer_address);
  address[0] = ip4_addr1(ip);
  address[1] = ip4_addr2(ip);
  address[2] = ip4_addr3(ip);
  address[3] = ip4_addr4(ip);
}

uint32_t AppNetwork_GetRxCount(void)
{
  return rx_count;
}

uint32_t AppNetwork_GetTxCount(void)
{
  return tx_count;
}

uint32_t AppNetwork_GetDropCount(void)
{
  return drop_count;
}
