/**
 * @file    mavlink_bridge.c
 * @brief   MAVLink Bridge with Frame Parsing and Debug
 */

#include "mavlink_bridge.h"
#include <string.h>
#include <stdio.h>

/* MAVLink 2 Constants */
#define MAVLINK_V2_MAGIC        0xFD
#define MAVLINK_HEADER_LEN      10
#define MAVLINK_CHECKSUM_LEN    2
#define MAVLINK_SIG_LEN         13
#define MAVLINK_IFLAG_SIGNED    0x01

/* Config */
#define FRAME_TIMEOUT_MS        200     /* Max time to wait for complete frame */

/* Hex encoding table */
static const char hex_table[] = "0123456789ABCDEF";

/* Internal State */
static struct {
    UART_DMA_Handle_t *uart;
    A7600_MQTT_Handle_t *mqtt;
    uint8_t rx_buf[512];       /* Raw buffer */
    size_t rx_len;
    uint32_t last_rx_tick;     /* Last time we received data */
    char tx_buf[1100];         /* For hex encoding (512*2 + header) */
} bridge;

/**
 * @brief Convert binary to hex string
 */
static size_t to_hex(const uint8_t *data, size_t len, char *out)
{
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        out[j++] = hex_table[(data[i] >> 4) & 0x0F];
        out[j++] = hex_table[data[i] & 0x0F];
    }
    out[j] = '\0';
    return j;
}

/**
 * @brief Send MAVLink frame as hex-encoded text (no prefix)
 */
static void send_frame_hex(const uint8_t *frame, size_t len)
{
    if (bridge.mqtt && A7600_MQTT_IsConnected(bridge.mqtt)) {
        to_hex(frame, len, bridge.tx_buf);
        A7600_MQTT_PublishString(bridge.mqtt, BRIDGE_TOPIC_TX, bridge.tx_buf, MQTT_QOS_0);
    }
}



void MavlinkBridge_Init(UART_DMA_Handle_t *uart, A7600_MQTT_Handle_t *mqtt)
{
    bridge.uart = uart;
    bridge.mqtt = mqtt;
    bridge.rx_len = 0;
    bridge.last_rx_tick = 0;
    memset(bridge.rx_buf, 0, sizeof(bridge.rx_buf));
}

void MavlinkBridge_Process(void)
{
    if (bridge.uart == NULL || bridge.mqtt == NULL) return;
    if (!A7600_MQTT_IsConnected(bridge.mqtt)) return;

    uint32_t now = HAL_GetTick();

    /* 1. Read available data */
    size_t available = UART_DMA_Available(bridge.uart);
    if (available > 0) {
        size_t space = sizeof(bridge.rx_buf) - bridge.rx_len;
        if (available > space) available = space;

        if (available > 0) {
            size_t read = UART_DMA_Read(bridge.uart, &bridge.rx_buf[bridge.rx_len], available);
            bridge.rx_len += read;
            bridge.last_rx_tick = now;
        }
    }

    /* 2. Check for timeout (incomplete frame) - discard silently */
    if (bridge.rx_len > 0 && (now - bridge.last_rx_tick > FRAME_TIMEOUT_MS)) {
        bridge.rx_len = 0;  /* Discard */
        return;
    }

    /* 3. Try to parse MAVLink frames */
    while (bridge.rx_len > 0) {
        /* Sync: Find magic byte */
        if (bridge.rx_buf[0] != MAVLINK_V2_MAGIC) {
            /* Not MAVLink 2, discard silently */
            memmove(bridge.rx_buf, &bridge.rx_buf[1], bridge.rx_len - 1);
            bridge.rx_len--;
            continue;
        }

        /* Need at least 3 bytes for length and flags */
        if (bridge.rx_len < 3) {
            break; /* Wait for more */
        }

        uint8_t payload_len = bridge.rx_buf[1];
        uint8_t incompat_flags = bridge.rx_buf[2];
        
        uint16_t packet_len = MAVLINK_HEADER_LEN + payload_len + MAVLINK_CHECKSUM_LEN;
        if (incompat_flags & MAVLINK_IFLAG_SIGNED) {
            packet_len += MAVLINK_SIG_LEN;
        }

        /* Sanity check */
        if (packet_len > 300) {
            /* Invalid length, discard byte */
            memmove(bridge.rx_buf, &bridge.rx_buf[1], bridge.rx_len - 1);
            bridge.rx_len--;
            continue;
        }

        /* Check if complete frame available */
        if (bridge.rx_len >= packet_len) {
            /* Complete frame! Send as hex */
            send_frame_hex(bridge.rx_buf, packet_len);
            
            /* Remove from buffer */
            if (bridge.rx_len > packet_len) {
                memmove(bridge.rx_buf, &bridge.rx_buf[packet_len], bridge.rx_len - packet_len);
            }
            bridge.rx_len -= packet_len;
        } else {
            /* Not enough data yet */
            break;
        }
    }
}

void MavlinkBridge_OnMessage(const char *topic, const uint8_t *payload, size_t len)
{
    if (bridge.uart == NULL) return;

    if (strstr(topic, "mavlink/rx") != NULL) {
        /* TODO: Decode hex if needed, for now pass through */
        UART_DMA_Transmit(bridge.uart, payload, len);
    }
}
