/**
 * @file    mavlink_bridge.h
 * @brief   MAVLink Bridge (UART <-> MQTT)
 */

#ifndef MAVLINK_BRIDGE_H
#define MAVLINK_BRIDGE_H

#include "main.h"
#include "uart_dma.h"
#include "a7600_mqtt.h"

#define BRIDGE_TOPIC_TX "uav4g/mavlink/tx"  /* Data from UART -> MQTT */
#define BRIDGE_TOPIC_RX "uav4g/mavlink/rx"  /* Data from MQTT -> UART */

/**
 * @brief Initialize Bridge
 * @param uart Pointer to Telemetry UART handle
 * @param mqtt Pointer to MQTT handle
 */
void MavlinkBridge_Init(UART_DMA_Handle_t *uart, A7600_MQTT_Handle_t *mqtt);

/**
 * @brief Process Bridge (Call in main loop)
 * Checks UART buffer and sends to MQTT
 */
void MavlinkBridge_Process(void);

/**
 * @brief Handle incoming MQTT data
 * @param topic Topic string
 * @param payload Data
 * @param len Data length
 */
void MavlinkBridge_OnMessage(const char *topic, const uint8_t *payload, size_t len);

#endif /* MAVLINK_BRIDGE_H */
