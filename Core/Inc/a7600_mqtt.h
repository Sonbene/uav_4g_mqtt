/**
 * @file    a7600_mqtt.h
 * @brief   MQTT Library for A7600 SIM Module with HiveMQ support
 * @version 1.0
 */

#ifndef A7600_MQTT_H
#define A7600_MQTT_H

#include "main.h"
#include "uart_dma.h"
#include <stdint.h>
#include <stdbool.h>

/* Configuration */
#define MQTT_BROKER_MAX_LEN         64
#define MQTT_USERNAME_MAX_LEN       32
#define MQTT_PASSWORD_MAX_LEN       32
#define MQTT_CLIENT_ID_MAX_LEN      32
#define MQTT_TOPIC_MAX_LEN          64
#define MQTT_PAYLOAD_MAX_LEN        256
#define MQTT_RESPONSE_TIMEOUT       10000   /* 10 seconds */
#define MQTT_CMD_TIMEOUT            5000    /* 5 seconds for AT commands */

/**
 * @brief MQTT QoS levels
 */
typedef enum {
    MQTT_QOS_0 = 0,     /**< At most once */
    MQTT_QOS_1 = 1,     /**< At least once */
    MQTT_QOS_2 = 2      /**< Exactly once */
} MQTT_QoS_t;

/**
 * @brief MQTT connection state
 */
typedef enum {
    MQTT_STATE_IDLE = 0,
    MQTT_STATE_STARTING,
    MQTT_STATE_ACQUIRING,
    MQTT_STATE_SSL_CONFIG,
    MQTT_STATE_CONNECTING,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_SUBSCRIBING,
    MQTT_STATE_PUBLISHING,
    MQTT_STATE_DISCONNECTING,
    MQTT_STATE_ERROR
} MQTT_State_t;

/**
 * @brief MQTT result codes
 */
typedef enum {
    MQTT_OK = 0,
    MQTT_ERROR,
    MQTT_BUSY,
    MQTT_TIMEOUT,
    MQTT_NOT_CONNECTED
} MQTT_Result_t;

/**
 * @brief Callback for received messages
 */
typedef void (*MQTT_MessageCallback_t)(const char *topic, const uint8_t *payload, size_t len);

/**
 * @brief MQTT configuration structure
 */
typedef struct {
    char broker[MQTT_BROKER_MAX_LEN];       /**< Broker hostname */
    uint16_t port;                           /**< Broker port (8883 for SSL, 1883 for non-SSL) */
    char username[MQTT_USERNAME_MAX_LEN];   /**< Username */
    char password[MQTT_PASSWORD_MAX_LEN];   /**< Password */
    char client_id[MQTT_CLIENT_ID_MAX_LEN]; /**< Client ID */
    bool use_ssl;                            /**< Enable SSL/TLS */
    uint16_t keepalive;                      /**< Keepalive interval in seconds */
} MQTT_Config_t;

/**
 * @brief MQTT handle structure
 */
typedef struct {
    UART_DMA_Handle_t *uart;                /**< UART handle for communication */
    MQTT_Config_t config;                   /**< MQTT configuration */
    MQTT_State_t state;                     /**< Current state */
    MQTT_MessageCallback_t msg_callback;    /**< Message received callback */
    
    /* Internal buffers */
    uint8_t rx_buffer[512];                 /**< Response buffer */
    size_t rx_len;                          /**< Current response length */
    uint32_t cmd_start_tick;                /**< Command start time */
    
    /* Debug info */
    uint8_t error_step;                     /**< Step where error occurred (1-10) */
    char last_response[128];                /**< Last response for debugging */
    
    /* Flags */
    volatile bool response_ready;           /**< Response received flag */
    volatile bool cmd_ok;                   /**< Command success flag */
    volatile bool connected;                /**< Connection status */
} A7600_MQTT_Handle_t;

/**
 * @brief Initialize MQTT handle
 * @param handle Pointer to MQTT handle
 * @param uart Pointer to UART DMA handle
 * @param config Pointer to MQTT configuration
 * @return MQTT_OK on success
 */
MQTT_Result_t A7600_MQTT_Init(A7600_MQTT_Handle_t *handle, UART_DMA_Handle_t *uart, MQTT_Config_t *config);

/**
 * @brief Set message received callback
 * @param handle Pointer to MQTT handle
 * @param callback Callback function
 */
void A7600_MQTT_SetMessageCallback(A7600_MQTT_Handle_t *handle, MQTT_MessageCallback_t callback);

/**
 * @brief Connect to MQTT broker (blocking)
 * @param handle Pointer to MQTT handle
 * @return MQTT_OK on success
 */
MQTT_Result_t A7600_MQTT_Connect(A7600_MQTT_Handle_t *handle);

/**
 * @brief Disconnect from MQTT broker
 * @param handle Pointer to MQTT handle
 * @return MQTT_OK on success
 */
MQTT_Result_t A7600_MQTT_Disconnect(A7600_MQTT_Handle_t *handle);

/**
 * @brief Upload certificate file to module file system
 * @param handle MQTT handle
 * @param filename Name of file on module (e.g. "ca_cert.pem")
 * @param data Certificate data pointer
 * @param len Length of data
 * @return true on success
 */
bool A7600_UploadCert(A7600_MQTT_Handle_t *handle, const char *filename, const char *data, size_t len);

/**
 * @brief Subscribe to a topic
 * @param handle Pointer to MQTT handle
 * @param topic Topic to subscribe
 * @param qos QoS level
 * @return MQTT_OK on success
 */
MQTT_Result_t A7600_MQTT_Subscribe(A7600_MQTT_Handle_t *handle, const char *topic, MQTT_QoS_t qos);

/**
 * @brief Unsubscribe from a topic
 * @param handle Pointer to MQTT handle
 * @param topic Topic to unsubscribe
 * @return MQTT_OK on success
 */
MQTT_Result_t A7600_MQTT_Unsubscribe(A7600_MQTT_Handle_t *handle, const char *topic);

/**
 * @brief Publish message to a topic
 * @param handle Pointer to MQTT handle
 * @param topic Topic to publish to
 * @param payload Message payload
 * @param len Payload length
 * @param qos QoS level
 * @param retain Retain flag
 * @return MQTT_OK on success
 */
MQTT_Result_t A7600_MQTT_Publish(A7600_MQTT_Handle_t *handle, const char *topic, 
                                  const uint8_t *payload, size_t len, 
                                  MQTT_QoS_t qos, bool retain);

/**
 * @brief Publish string message
 * @param handle Pointer to MQTT handle
 * @param topic Topic to publish to
 * @param message Null-terminated string message
 * @param qos QoS level
 * @return MQTT_OK on success
 */
MQTT_Result_t A7600_MQTT_PublishString(A7600_MQTT_Handle_t *handle, const char *topic,
                                        const char *message, MQTT_QoS_t qos);

/**
 * @brief Process MQTT - call regularly in main loop
 * @param handle Pointer to MQTT handle
 */
void A7600_MQTT_Process(A7600_MQTT_Handle_t *handle);

/**
 * @brief Check if connected to broker
 * @param handle Pointer to MQTT handle
 * @return true if connected
 */
bool A7600_MQTT_IsConnected(A7600_MQTT_Handle_t *handle);

/**
 * @brief Get current state
 * @param handle Pointer to MQTT handle
 * @return Current MQTT state
 */
MQTT_State_t A7600_MQTT_GetState(A7600_MQTT_Handle_t *handle);

/**
 * @brief Get last error step (1-10)
 * @param handle Pointer to MQTT handle
 * @return Step number where error occurred
 *         1=Module, 2=SIM, 3=Network, 4=GPRS, 5=PDP,
 *         6=Signal, 7=MQTT Start, 8=Client, 9=SSL, 10=Connect
 */
uint8_t A7600_MQTT_GetErrorStep(A7600_MQTT_Handle_t *handle);

/**
 * @brief Get last response string for debugging
 * @param handle Pointer to MQTT handle
 * @return Pointer to last response buffer
 */
const char* A7600_MQTT_GetLastResponse(A7600_MQTT_Handle_t *handle);

#endif /* A7600_MQTT_H */
