#include "app_clock.h"

#include "app_network.h"
#include "ethernetif.h"
#include "lwip.h"
#include "lwip/ip4_addr.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

#include <limits.h>
#include <string.h>

#define CLOCK_PORT             50001U
#define CLOCK_PACKET_SIZE      40U
#define CLOCK_MAGIC            0x534E4350UL
#define CLOCK_VERSION          1U
#define CLOCK_SYNC             1U
#define CLOCK_DELAY_REQ        2U
#define CLOCK_FOLLOW_UP        3U
#define CLOCK_PERIOD_MS        100U
#define CLOCK_TIMEOUT_MS       500U
#define CLOCK_LOST_MS          1000U
#define CLOCK_MAX_RTT_NS       100000U
#define CLOCK_LOCK_ERROR_NS    5000LL
#define CLOCK_NANOSECOND       1000000000ULL
#define CLOCK_PTP_ADDEND       0xED097B42UL

extern ETH_HandleTypeDef heth;
extern struct netif gnetif;

static netif_input_fn original_input;
static netif_linkoutput_fn original_linkoutput;
static struct udp_pcb *clock_pcb;
static AppClockState clock_state;
static AppNetworkRole previous_role;
static uint32_t sequence;
static uint32_t pending_sequence;
static uint32_t last_send_ms;
static uint32_t last_sample_ms;
static uint32_t frequency_reference_ms;
static uint64_t t1_ns;
static uint64_t t2_ns;
static uint64_t t3_ns;
static uint64_t rx_timestamp_ns;
static uint64_t sample_raw_ns;
static int64_t raw_offset_ns;
static int64_t frequency_reference_offset_ns;
static int32_t error_ns;
static int32_t drift_ppb;
static uint32_t round_trip_ns;
static uint8_t ptp_ready;
static uint8_t rx_timestamp_valid;
static uint8_t t1_valid;
static uint8_t t3_valid;
static uint8_t tx_capture_kind;
static uint8_t good_samples;

static void write_u32(uint8_t *buffer, uint32_t value)
{
  buffer[0] = (uint8_t)(value >> 24);
  buffer[1] = (uint8_t)(value >> 16);
  buffer[2] = (uint8_t)(value >> 8);
  buffer[3] = (uint8_t)value;
}

static uint32_t read_u32(const uint8_t *buffer)
{
  return ((uint32_t)buffer[0] << 24) | ((uint32_t)buffer[1] << 16) |
         ((uint32_t)buffer[2] << 8) | (uint32_t)buffer[3];
}

static void write_u64(uint8_t *buffer, uint64_t value)
{
  write_u32(buffer, (uint32_t)(value >> 32));
  write_u32(buffer + 4, (uint32_t)value);
}

static uint64_t read_u64(const uint8_t *buffer)
{
  return ((uint64_t)read_u32(buffer) << 32) | read_u32(buffer + 4);
}

static uint32_t clock_crc32(const uint8_t *buffer, uint16_t length)
{
  uint32_t crc = 0xFFFFFFFFUL;
  uint16_t index;
  uint8_t bit;

  for (index = 0U; index < length; index++)
  {
    crc ^= buffer[index];
    for (bit = 0U; bit < 8U; bit++)
    {
      crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0UL);
    }
  }
  return ~crc;
}

static int32_t clamp_i32(int64_t value)
{
  if (value > INT32_MAX)
  {
    return INT32_MAX;
  }
  if (value < INT32_MIN)
  {
    return INT32_MIN;
  }
  return (int32_t)value;
}

static uint8_t stamp_to_ns(const ETH_TimeStampTypeDef *stamp,
                           uint64_t *nanoseconds)
{
  if ((stamp->TimeStampLow >= CLOCK_NANOSECOND) ||
      ((stamp->TimeStampHigh == UINT32_MAX) &&
       (stamp->TimeStampLow == UINT32_MAX)))
  {
    return 0U;
  }
  *nanoseconds = ((uint64_t)stamp->TimeStampHigh * CLOCK_NANOSECOND) +
                 stamp->TimeStampLow;
  return 1U;
}

static uint8_t read_ptp_ns(uint64_t *nanoseconds)
{
  uint32_t seconds_before;
  uint32_t seconds_after;
  uint32_t subsecond;

  if ((ptp_ready == 0U) || (nanoseconds == NULL))
  {
    return 0U;
  }
  do
  {
    seconds_before = ETH->PTPTSHR;
    subsecond = ETH->PTPTSLR;
    seconds_after = ETH->PTPTSHR;
  } while (seconds_before != seconds_after);

  if (subsecond >= CLOCK_NANOSECOND)
  {
    return 0U;
  }
  *nanoseconds = ((uint64_t)seconds_before * CLOCK_NANOSECOND) + subsecond;
  return 1U;
}

static uint8_t wait_ptp_control(uint32_t flag)
{
  uint32_t start = HAL_GetTick();
  while ((ETH->PTPTSCR & flag) != 0U)
  {
    if ((HAL_GetTick() - start) > 10U)
    {
      return 0U;
    }
  }
  return 1U;
}

static uint8_t start_ptp_clock(void)
{
  /* HCLK=216 MHz. 5 ns * (0xED097B42 / 2^32) * 216 MHz = 1 second/s. */
  if (HAL_RCC_GetHCLKFreq() != 216000000U)
  {
    return 0U;
  }
  ETH->PTPTSCR = ETH_PTPTSCR_TSE | ETH_PTPTSCR_TSFCU |
                 ETH_PTPTSCR_TSSSR | ETH_PTPTSCR_TSSARFE;
  ETH->PTPSSIR = 5U;
  ETH->PTPTSAR = CLOCK_PTP_ADDEND;
  ETH->PTPTSCR |= ETH_PTPTSCR_TSARU;
  if (wait_ptp_control(ETH_PTPTSCR_TSARU) == 0U)
  {
    return 0U;
  }
  ETH->PTPTSHUR = 0U;
  ETH->PTPTSLUR = 0U;
  ETH->PTPTSCR |= ETH_PTPTSCR_TSSTI;
  if (wait_ptp_control(ETH_PTPTSCR_TSSTI) == 0U)
  {
    return 0U;
  }
  heth.IsPtpConfigured = HAL_ETH_PTP_CONFIGURED;
  return 1U;
}

static uint8_t is_clock_frame(const struct pbuf *packet)
{
  uint8_t header[38];
  uint8_t ip_header_length;

  if ((packet == NULL) || (packet->next != NULL) ||
      (packet->tot_len < sizeof(header)) ||
      (pbuf_copy_partial(packet, header, sizeof(header), 0U) != sizeof(header)) ||
      (header[12] != 0x08U) || (header[13] != 0x00U) ||
      ((header[14] >> 4) != 4U) || (header[23] != 17U))
  {
    return 0U;
  }
  ip_header_length = (uint8_t)((header[14] & 0x0FU) * 4U);
  if (ip_header_length != 20U)
  {
    return 0U;
  }
  return ((header[34] == (uint8_t)(CLOCK_PORT >> 8)) &&
          (header[35] == (uint8_t)CLOCK_PORT) &&
          (header[36] == (uint8_t)(CLOCK_PORT >> 8)) &&
          (header[37] == (uint8_t)CLOCK_PORT)) ? 1U : 0U;
}

static err_t clock_linkoutput(struct netif *netif, struct pbuf *packet)
{
  ETH_DMADescTypeDef *descriptor = NULL;
  ETH_TimeStampTypeDef stamp;
  uint8_t capture = 0U;
  err_t result;

  if ((ptp_ready != 0U) && (tx_capture_kind != 0U) &&
      (is_clock_frame(packet) != 0U))
  {
    descriptor = (ETH_DMADescTypeDef *)
                 heth.TxDescList.TxDesc[heth.TxDescList.CurTxDesc];
    if ((descriptor->DESC0 & ETH_DMATXDESC_OWN) == 0U)
    {
      descriptor->DESC0 &= ~ETH_DMATXDESC_TTSS;
      descriptor->DESC6 = UINT32_MAX;
      descriptor->DESC7 = UINT32_MAX;
      capture = (HAL_ETH_PTP_InsertTxTimestamp(&heth) == HAL_OK) ? 1U : 0U;
    }
  }
  result = original_linkoutput(netif, packet);
  if ((capture != 0U) && (result == ERR_OK) &&
      ((descriptor->DESC0 & (ETH_DMATXDESC_OWN | ETH_DMATXDESC_TTSS)) ==
       ETH_DMATXDESC_TTSS))
  {
    stamp.TimeStampLow = descriptor->DESC6;
    stamp.TimeStampHigh = descriptor->DESC7;
    if (tx_capture_kind == CLOCK_SYNC)
    {
      t1_valid = stamp_to_ns(&stamp, &t1_ns);
    }
    else if (tx_capture_kind == CLOCK_DELAY_REQ)
    {
      t3_valid = stamp_to_ns(&stamp, &t3_ns);
    }
  }
  return result;
}

static err_t clock_input(struct pbuf *packet, struct netif *netif)
{
  ETH_TimeStampTypeDef stamp;
  err_t result;

  rx_timestamp_valid = 0U;
  if ((ptp_ready != 0U) &&
      (HAL_ETH_PTP_GetRxTimestamp(&heth, &stamp) == HAL_OK))
  {
    rx_timestamp_valid = stamp_to_ns(&stamp, &rx_timestamp_ns);
  }
  result = original_input(packet, netif);
  rx_timestamp_valid = 0U;
  return result;
}

static uint8_t send_packet(uint8_t kind, uint32_t packet_sequence,
                           uint64_t a, uint64_t b, const ip_addr_t *address)
{
  uint8_t buffer[CLOCK_PACKET_SIZE];
  struct pbuf *packet;
  err_t result;

  if ((clock_pcb == NULL) || (address == NULL))
  {
    return 0U;
  }
  (void)memset(buffer, 0, sizeof(buffer));
  write_u32(&buffer[0], CLOCK_MAGIC);
  buffer[4] = CLOCK_VERSION;
  buffer[5] = kind;
  write_u32(&buffer[8], packet_sequence);
  write_u64(&buffer[12], a);
  write_u64(&buffer[20], b);
  write_u32(&buffer[36], clock_crc32(buffer, 36U));

  packet = pbuf_alloc(PBUF_TRANSPORT, CLOCK_PACKET_SIZE, PBUF_RAM);
  if (packet == NULL)
  {
    return 0U;
  }
  if (pbuf_take(packet, buffer, CLOCK_PACKET_SIZE) != ERR_OK)
  {
    pbuf_free(packet);
    return 0U;
  }
  tx_capture_kind = kind;
  result = udp_sendto(clock_pcb, packet, address, CLOCK_PORT);
  tx_capture_kind = 0U;
  pbuf_free(packet);
  return (result == ERR_OK) ? 1U : 0U;
}

static void reset_sync(AppClockState state)
{
  clock_state = state;
  pending_sequence = 0U;
  t1_valid = 0U;
  t3_valid = 0U;
  good_samples = 0U;
  error_ns = 0;
  drift_ppb = 0;
  round_trip_ns = 0U;
  raw_offset_ns = 0;
  sample_raw_ns = 0U;
  frequency_reference_ms = 0U;
}

static void accept_sample(int64_t offset, uint64_t midpoint,
                          uint32_t rtt)
{
  int64_t prediction;
  int64_t residual;
  int64_t delta;
  uint32_t elapsed;

  if (good_samples == 0U)
  {
    residual = 0;
    frequency_reference_ms = HAL_GetTick();
    frequency_reference_offset_ns = offset;
  }
  else
  {
    prediction = raw_offset_ns +
                 ((int64_t)drift_ppb * (int64_t)(midpoint - sample_raw_ns)) /
                 (int64_t)CLOCK_NANOSECOND;
    residual = offset - prediction;
  }
  error_ns = clamp_i32(residual);
  round_trip_ns = rtt;
  raw_offset_ns = offset;
  sample_raw_ns = midpoint;
  last_sample_ms = HAL_GetTick();

  elapsed = last_sample_ms - frequency_reference_ms;
  if ((elapsed >= 500U) && (elapsed <= 3000U))
  {
    delta = offset - frequency_reference_offset_ns;
    if ((delta > -500000LL) && (delta < 500000LL))
    {
      int64_t measured = (delta * 1000LL) / (int64_t)elapsed;
      if ((measured > -200000LL) && (measured < 200000LL))
      {
        drift_ppb = (int32_t)((3LL * drift_ppb + measured) / 4LL);
      }
    }
    frequency_reference_ms = last_sample_ms;
    frequency_reference_offset_ns = offset;
  }

  if ((residual <= CLOCK_LOCK_ERROR_NS) &&
      (residual >= -CLOCK_LOCK_ERROR_NS))
  {
    if (good_samples < 5U)
    {
      good_samples++;
    }
  }
  else
  {
    good_samples = 1U;
  }
  clock_state = (good_samples >= 5U) ? APP_CLOCK_LOCKED : APP_CLOCK_ACQUIRING;
}

static void receive_packet(void *argument, struct udp_pcb *pcb,
                           struct pbuf *packet, const ip_addr_t *address,
                           u16_t port)
{
  uint8_t buffer[CLOCK_PACKET_SIZE];
  uint8_t kind;
  uint32_t received_sequence;
  uint64_t received_timestamp;
  uint64_t received_a;
  uint64_t received_b;
  uint64_t peer_midpoint;
  uint64_t local_midpoint;
  int64_t offset;
  int64_t rtt;
  AppNetworkRole role;
  uint8_t peer_ip[4];

  (void)argument;
  (void)pcb;
  if (packet == NULL)
  {
    return;
  }
  if ((port != CLOCK_PORT) || (ptp_ready == 0U) ||
      (rx_timestamp_valid == 0U) ||
      (packet->tot_len != CLOCK_PACKET_SIZE) ||
      (pbuf_copy_partial(packet, buffer, CLOCK_PACKET_SIZE, 0U) !=
       CLOCK_PACKET_SIZE) ||
      (read_u32(&buffer[0]) != CLOCK_MAGIC) ||
      (buffer[4] != CLOCK_VERSION) ||
      (read_u32(&buffer[36]) != clock_crc32(buffer, 36U)) ||
      (AppNetwork_IsPeerOnline() == 0U))
  {
    pbuf_free(packet);
    return;
  }
  AppNetwork_GetPeerIPv4(peer_ip);
  if ((peer_ip[0] != ip4_addr1(ip_2_ip4(address))) ||
      (peer_ip[1] != ip4_addr2(ip_2_ip4(address))) ||
      (peer_ip[2] != ip4_addr3(ip_2_ip4(address))) ||
      (peer_ip[3] != ip4_addr4(ip_2_ip4(address))))
  {
    pbuf_free(packet);
    return;
  }
  kind = buffer[5];
  received_sequence = read_u32(&buffer[8]);
  received_a = read_u64(&buffer[12]);
  received_b = read_u64(&buffer[20]);
  received_timestamp = rx_timestamp_ns;
  role = AppNetwork_GetRole();

  if ((role == APP_NETWORK_ROLE_SLAVE) && (kind == CLOCK_SYNC))
  {
    pending_sequence = received_sequence;
    t2_ns = received_timestamp;
    t3_valid = 0U;
    (void)send_packet(CLOCK_DELAY_REQ, received_sequence, t2_ns, 0U,
                      address);
  }
  else if ((role == APP_NETWORK_ROLE_MASTER) &&
           (kind == CLOCK_DELAY_REQ) && (t1_valid != 0U) &&
           (received_sequence == pending_sequence))
  {
    if (send_packet(CLOCK_FOLLOW_UP, received_sequence,
                    t1_ns, received_timestamp, address) != 0U)
    {
      last_sample_ms = HAL_GetTick();
      clock_state = APP_CLOCK_LOCKED;
    }
    pending_sequence = 0U;
  }
  else if ((role == APP_NETWORK_ROLE_SLAVE) &&
           (kind == CLOCK_FOLLOW_UP) && (t3_valid != 0U) &&
           (received_sequence == pending_sequence))
  {
    /* a=t1 at master Tx, b=t4 at master Rx, t2/t3 at slave MAC. */
    if ((received_b > received_a) && (t3_ns >= t2_ns))
    {
      rtt = (int64_t)(received_b - received_a) -
            (int64_t)(t3_ns - t2_ns);
      if ((rtt >= 0) && (rtt <= CLOCK_MAX_RTT_NS))
      {
        peer_midpoint = received_a + ((received_b - received_a) / 2U);
        local_midpoint = t2_ns + ((t3_ns - t2_ns) / 2U);
        offset = (int64_t)local_midpoint - (int64_t)peer_midpoint;
        accept_sample(offset, local_midpoint, (uint32_t)rtt);
      }
    }
    pending_sequence = 0U;
    t3_valid = 0U;
  }
  pbuf_free(packet);
}

void AppClock_Init(void)
{
  reset_sync(APP_CLOCK_SEARCHING);
  ptp_ready = start_ptp_clock();
  if (ptp_ready == 0U)
  {
    clock_state = APP_CLOCK_LOST;
    return;
  }
  original_input = gnetif.input;
  original_linkoutput = gnetif.linkoutput;
  gnetif.input = clock_input;
  gnetif.linkoutput = clock_linkoutput;
  clock_pcb = udp_new();
  if ((clock_pcb == NULL) ||
      (udp_bind(clock_pcb, IP_ADDR_ANY, CLOCK_PORT) != ERR_OK))
  {
    if (clock_pcb != NULL)
    {
      udp_remove(clock_pcb);
      clock_pcb = NULL;
    }
    clock_state = APP_CLOCK_LOST;
    return;
  }
  udp_recv(clock_pcb, receive_packet, NULL);
  last_send_ms = HAL_GetTick() - CLOCK_PERIOD_MS;
}

void AppClock_Process(void)
{
  AppNetworkRole role = AppNetwork_GetRole();
  ip_addr_t address;
  uint8_t peer_ip[4];
  uint32_t now = HAL_GetTick();

  if ((ptp_ready == 0U) || (clock_pcb == NULL))
  {
    return;
  }
  if ((AppNetwork_IsPeerOnline() == 0U) ||
      (AppNetwork_IsLinkUp() == 0U))
  {
    if (previous_role != APP_NETWORK_ROLE_UNKNOWN)
    {
      reset_sync(APP_CLOCK_SEARCHING);
    }
    previous_role = APP_NETWORK_ROLE_UNKNOWN;
    return;
  }
  if (role != previous_role)
  {
    reset_sync(APP_CLOCK_ACQUIRING);
    previous_role = role;
    last_send_ms = now - CLOCK_PERIOD_MS;
  }
  if ((role == APP_NETWORK_ROLE_SLAVE) &&
      (good_samples != 0U) &&
      ((now - last_sample_ms) > CLOCK_LOST_MS))
  {
    reset_sync(APP_CLOCK_LOST);
  }
  if ((role == APP_NETWORK_ROLE_MASTER) &&
      (clock_state == APP_CLOCK_LOCKED) &&
      ((now - last_sample_ms) > CLOCK_LOST_MS))
  {
    clock_state = APP_CLOCK_LOST;
  }
  if ((role != APP_NETWORK_ROLE_MASTER) ||
      ((now - last_send_ms) < CLOCK_PERIOD_MS))
  {
    return;
  }
  if ((pending_sequence != 0U) &&
      ((now - last_send_ms) < CLOCK_TIMEOUT_MS))
  {
    return;
  }
  AppNetwork_GetPeerIPv4(peer_ip);
  IP_ADDR4(&address, peer_ip[0], peer_ip[1], peer_ip[2], peer_ip[3]);
  sequence++;
  if (sequence == 0U)
  {
    sequence++;
  }
  pending_sequence = sequence;
  t1_valid = 0U;
  last_send_ms = now;
  if ((send_packet(CLOCK_SYNC, sequence, 0U, 0U, &address) == 0U) ||
      (t1_valid == 0U))
  {
    pending_sequence = 0U;
  }
}

AppClockState AppClock_GetState(void)
{
  return clock_state;
}

int32_t AppClock_GetErrorNs(void)
{
  return error_ns;
}

int32_t AppClock_GetDriftPpb(void)
{
  return drift_ppb;
}

uint32_t AppClock_GetRoundTripNs(void)
{
  return round_trip_ns;
}

uint64_t AppClock_NowNs(void)
{
  uint64_t raw_ns;
  int64_t correction;
  int64_t aligned;

  if (read_ptp_ns(&raw_ns) == 0U)
  {
    return 0U;
  }
  if ((previous_role != APP_NETWORK_ROLE_SLAVE) || (good_samples == 0U))
  {
    return raw_ns;
  }
  correction = raw_offset_ns +
               ((int64_t)drift_ppb * (int64_t)(raw_ns - sample_raw_ns)) /
               (int64_t)CLOCK_NANOSECOND;
  aligned = (int64_t)raw_ns - correction;
  return (aligned > 0) ? (uint64_t)aligned : 0U;
}

uint8_t AppClock_RawDeadlineForGlobal(uint64_t global_target_ns,
                                      uint64_t *raw_target_ns)
{
  uint64_t raw_now_ns;
  int64_t correction_ns = 0;
  int64_t global_now_ns;
  int64_t lead_ns;

  if ((raw_target_ns == NULL) || (clock_state != APP_CLOCK_LOCKED) ||
      (read_ptp_ns(&raw_now_ns) == 0U))
  {
    return 0U;
  }

  if ((previous_role == APP_NETWORK_ROLE_SLAVE) && (good_samples != 0U))
  {
    correction_ns = raw_offset_ns +
                    ((int64_t)drift_ppb *
                     ((int64_t)raw_now_ns - (int64_t)sample_raw_ns)) /
                    (int64_t)CLOCK_NANOSECOND;
  }
  global_now_ns = (int64_t)raw_now_ns - correction_ns;
  lead_ns = (int64_t)global_target_ns - global_now_ns;
  if ((lead_ns < 100000000LL) || (lead_ns > 2000000000LL))
  {
    return 0U;
  }
  if ((previous_role == APP_NETWORK_ROLE_SLAVE) && (good_samples != 0U))
  {
    lead_ns += ((int64_t)drift_ppb * lead_ns) /
               (int64_t)CLOCK_NANOSECOND;
  }
  *raw_target_ns = raw_now_ns + (uint64_t)lead_ns;
  return 1U;
}
