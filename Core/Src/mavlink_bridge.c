/**
 * @file    mavlink_bridge.c
 * @brief   MAVLink Bridge with Configurable Encoding (HEX or Base64)
 */

#include "mavlink_bridge.h"
#include "debug_log.h"
#include <string.h>

/* ==================== ENCODING OPTIONS ==================== */
/* Change this to select encoding mode:
 *   ENCODE_HEX    - Output: "FD1C0000..." (100% size increase)
 *   ENCODE_BASE64 - Output: "/RwAAA..."  (33% size increase)
 */
#define ENCODE_HEX      0
#define ENCODE_BASE64   1

#define BRIDGE_ENCODING ENCODE_BASE64  /* <-- CHANGE THIS TO SELECT */

/* ==================== MAVLink 2 Constants ==================== */
#define MAVLINK_V2_MAGIC        0xFD
#define MAVLINK_HEADER_LEN      10
#define MAVLINK_CHECKSUM_LEN    2
#define MAVLINK_SIG_LEN         13
#define MAVLINK_IFLAG_SIGNED    0x01

/* Config */
#define FRAME_TIMEOUT_MS        50

/* Encoding tables */
static const char hex_table[] = "0123456789ABCDEF";
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Internal State */
static struct {
    UART_DMA_Handle_t *uart;
    A7600_MQTT_Handle_t *mqtt;
    uint8_t rx_buf[512];
    size_t rx_len;
    uint32_t last_rx_tick;
    char tx_buf[1100];  /* Max: 512 * 2 + 1 for hex, or 512 * 4/3 + 4 for base64 */
} bridge;

/* ==================== Encoding Functions ==================== */

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
 * @brief Convert binary to Base64 string
 */
static size_t to_base64(const uint8_t *data, size_t len, char *out)
{
    size_t i, j;
    uint32_t octet_a, octet_b, octet_c, triple;
    
    for (i = 0, j = 0; i < len; ) {
        octet_a = i < len ? data[i++] : 0;
        octet_b = i < len ? data[i++] : 0;
        octet_c = i < len ? data[i++] : 0;
        
        triple = (octet_a << 16) | (octet_b << 8) | octet_c;
        
        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = b64_table[(triple >> 6) & 0x3F];
        out[j++] = b64_table[triple & 0x3F];
    }
    
    /* Add padding */
    size_t mod = len % 3;
    if (mod == 1) {
        out[j - 1] = '=';
        out[j - 2] = '=';
    } else if (mod == 2) {
        out[j - 1] = '=';
    }
    
    out[j] = '\0';
    return j;
}

/**
 * @brief Convert Hex string to binary
 */
static size_t from_hex(const char *src, size_t len, uint8_t *out)
{
    size_t j = 0;
    for (size_t i = 0; i < len; i += 2) {
        if (i + 1 >= len) break;
        
        char c1 = src[i];
        char c2 = src[i+1];
        
        uint8_t v1 = (c1 >= '0' && c1 <= '9') ? c1 - '0' : 
                     (c1 >= 'A' && c1 <= 'F') ? c1 - 'A' + 10 : 
                     (c1 >= 'a' && c1 <= 'f') ? c1 - 'a' + 10 : 0;
                     
        uint8_t v2 = (c2 >= '0' && c2 <= '9') ? c2 - '0' : 
                     (c2 >= 'A' && c2 <= 'F') ? c2 - 'A' + 10 : 
                     (c2 >= 'a' && c2 <= 'f') ? c2 - 'a' + 10 : 0;
                     
        out[j++] = (v1 << 4) | v2;
    }
    return j;
}

/**
 * @brief Convert Base64 string to binary
 */
static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static size_t from_base64(const char *src, size_t len, uint8_t *out)
{
    size_t i = 0, j = 0;
    int v1, v2, v3, v4;
    
    while (i < len) {
        /* Skip non-base64 chars (newlines, etc) */
        while (i < len && b64_val(src[i]) == -1 && src[i] != '=') i++;
        if (i >= len) break;
        
        v1 = b64_val(src[i++]);
        
        while (i < len && b64_val(src[i]) == -1 && src[i] != '=') i++;
        if (i >= len) break;
        v2 = b64_val(src[i++]);
        
        while (i < len && b64_val(src[i]) == -1 && src[i] != '=') i++;
        if (i >= len) break;
        char c3 = src[i++];
        v3 = (c3 == '=') ? 0 : b64_val(c3);
        
        while (i < len && b64_val(src[i]) == -1 && src[i] != '=') i++;
        if (i >= len) break;
        char c4 = src[i++];
        v4 = (c4 == '=') ? 0 : b64_val(c4);
        
        if (v1 >= 0 && v2 >= 0) {
            out[j++] = (v1 << 2) | ((v2 >> 4) & 0x03);
            if (c3 != '=') {
                out[j++] = ((v2 << 4) & 0xF0) | ((v3 >> 2) & 0x0F);
                if (c4 != '=') {
                    out[j++] = ((v3 << 6) & 0xC0) | (v4 & 0x3F);
                }
            }
        }
    }
    return j;
}

/**
 * @brief Send MAVLink frame with selected encoding
 */
static void send_frame(const uint8_t *frame, size_t len)
{
    if (bridge.mqtt && A7600_MQTT_IsConnected(bridge.mqtt)) {
        size_t encoded_len;
        
        #if BRIDGE_ENCODING == ENCODE_BASE64
            encoded_len = to_base64(frame, len, bridge.tx_buf);
        #else
            encoded_len = to_hex(frame, len, bridge.tx_buf);
        #endif
        
        A7600_MQTT_PublishString(bridge.mqtt, BRIDGE_TOPIC_TX, bridge.tx_buf, MQTT_QOS_0);
    }
}

/* ==================== Public Functions ==================== */

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
        bridge.rx_len = 0;
        return;
    }

    /* 3. Parse MAVLink frames */
    while (bridge.rx_len > 0) {
        /* Sync: Find magic byte */
        if (bridge.rx_buf[0] != MAVLINK_V2_MAGIC) {
            memmove(bridge.rx_buf, &bridge.rx_buf[1], bridge.rx_len - 1);
            bridge.rx_len--;
            continue;
        }

        /* Need at least 3 bytes */
        if (bridge.rx_len < 3) {
            break;
        }

        uint8_t payload_len = bridge.rx_buf[1];
        uint8_t incompat_flags = bridge.rx_buf[2];
        
        uint16_t packet_len = MAVLINK_HEADER_LEN + payload_len + MAVLINK_CHECKSUM_LEN;
        if (incompat_flags & MAVLINK_IFLAG_SIGNED) {
            packet_len += MAVLINK_SIG_LEN;
        }

        /* Sanity check */
        if (packet_len > 300) {
            memmove(bridge.rx_buf, &bridge.rx_buf[1], bridge.rx_len - 1);
            bridge.rx_len--;
            continue;
        }

        /* Check if complete frame available */
        if (bridge.rx_len >= packet_len) {
            /* Complete frame! Send with selected encoding */
            send_frame(bridge.rx_buf, packet_len);
            
            /* Remove from buffer */
            if (bridge.rx_len > packet_len) {
                memmove(bridge.rx_buf, &bridge.rx_buf[packet_len], bridge.rx_len - packet_len);
            }
            bridge.rx_len -= packet_len;
        } else {
            break;
        }
    }
}

void MavlinkBridge_OnMessage(const char *topic, const uint8_t *payload, size_t len)
{
    if (bridge.uart == NULL) return;

    LOG_INFO("Bridge Rx Msg: Topic=%s Len=%d", topic, len);

    /* Check if topic matches RX topic (flexible match) */
    if (strstr(topic, "mavlink/rx") != NULL) {
        static uint8_t decode_buf[512];
        size_t decoded_len = 0;

        #if BRIDGE_ENCODING == ENCODE_BASE64
            decoded_len = from_base64((const char*)payload, len, decode_buf);
            LOG_INFO("Decoded Base64: %d bytes", decoded_len);
        #else
            decoded_len = from_hex((const char*)payload, len, decode_buf);
            LOG_INFO("Decoded Hex: %d bytes", decoded_len);
        #endif

        if (decoded_len > 0) {
            /* Forward to UART */
            UART_DMA_Transmit(bridge.uart, decode_buf, decoded_len);
        } else {
            LOG_ERROR("Decode Failed or Empty");
        }
    } else {
        LOG_WARN("Bridge Ignored Topic: %s", topic);
    }
}
