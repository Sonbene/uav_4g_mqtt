/**
 * @file    app.h
 * @brief   Application Layer for UAV 4G Project
 * @version 1.0
 */

#ifndef APP_H
#define APP_H

#include "main.h"
#include "uart_dma.h"
#include "a7600_mqtt.h"

/* ==================== Configuration ==================== */

/* HiveMQ Broker Settings */
#define APP_MQTT_BROKER         "d3fd0fd59ed14b6d9fe037c0ef1bf662.s1.eu.hivemq.cloud"
#define APP_MQTT_PORT           8883
#define APP_MQTT_USERNAME       "uav4g"
#define APP_MQTT_PASSWORD       "Uav4g_timelapse"
#define APP_MQTT_CLIENT_ID      "stm32_uav4g"
#define APP_MQTT_KEEPALIVE      120

/* MQTT Topics */
#define APP_TOPIC_STATUS        "uav4g/status"
#define APP_TOPIC_SENSOR        "uav4g/sensor"
#define APP_TOPIC_COMMAND       "uav4g/command"
#define APP_TOPIC_RESPONSE      "uav4g/response"

/* Timing */
#define APP_PUBLISH_INTERVAL    5000    /* Publish status every 5 seconds */
#define APP_RECONNECT_INTERVAL  30000   /* Reconnect attempt every 30 seconds */

/**
 * @brief Application state
 */
typedef enum {
    APP_STATE_INIT = 0,
    APP_STATE_WAIT_MODULE,
    APP_STATE_CONNECTING,
    APP_STATE_CONNECTED,
    APP_STATE_ERROR
} App_State_t;

/**
 * @brief Application handle
 */
typedef struct {
    UART_DMA_Handle_t *uart;
    A7600_MQTT_Handle_t mqtt;
    App_State_t state;
    uint32_t last_publish_tick;
    uint32_t last_reconnect_tick;
    uint32_t error_count;
} App_Handle_t;

/**
 * @brief Initialize application
 * @param app Pointer to app handle
 * @param uart Pointer to UART DMA handle
 * @return true on success
 */
bool App_Init(App_Handle_t *app, UART_DMA_Handle_t *uart);

/**
 * @brief Run application main loop (call in while(1))
 * @param app Pointer to app handle
 */
void App_Run(App_Handle_t *app);

/**
 * @brief Connect to MQTT broker
 * @param app Pointer to app handle
 * @return true on success
 */
bool App_Connect(App_Handle_t *app);

/**
 * @brief Disconnect from MQTT broker
 * @param app Pointer to app handle
 */
void App_Disconnect(App_Handle_t *app);

/**
 * @brief Publish sensor data
 * @param app Pointer to app handle
 * @param data Sensor data string
 * @return true on success
 */
bool App_PublishSensor(App_Handle_t *app, const char *data);

/**
 * @brief Publish status message
 * @param app Pointer to app handle
 * @param status Status string
 * @return true on success
 */
bool App_PublishStatus(App_Handle_t *app, const char *status);

/**
 * @brief Check if connected
 * @param app Pointer to app handle
 * @return true if connected
 */
bool App_IsConnected(App_Handle_t *app);

/**
 * @brief Get current app state
 * @param app Pointer to app handle
 * @return Current state
 */
App_State_t App_GetState(App_Handle_t *app);

#endif /* APP_H */
