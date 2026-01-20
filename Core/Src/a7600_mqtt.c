/**
 * @file    a7600_mqtt.c
 * @brief   MQTT Library Implementation for A7600 SIM Module
 * @version 1.0
 */

#include "a7600_mqtt.h"
#include "debug_log.h"
#include <string.h>
#include <stdio.h>

/* Private defines */
#define AT_CMD_MAX_LEN      256
#define RESPONSE_WAIT_MS    100

/* Private functions */
static bool send_at_cmd(A7600_MQTT_Handle_t *handle, const char *cmd);
static bool wait_response(A7600_MQTT_Handle_t *handle, const char *expected, uint32_t timeout_ms);
static bool send_and_wait(A7600_MQTT_Handle_t *handle, const char *cmd, const char *expected, uint32_t timeout_ms);
static void clear_rx_buffer(A7600_MQTT_Handle_t *handle);
static void clear_rx_buffer(A7600_MQTT_Handle_t *handle);

/* ==================== Private Functions ==================== */

static void clear_rx_buffer(A7600_MQTT_Handle_t *handle)
{
    handle->rx_len = 0;
    memset(handle->rx_buffer, 0, sizeof(handle->rx_buffer));
}

static bool send_at_cmd(A7600_MQTT_Handle_t *handle, const char *cmd)
{
    clear_rx_buffer(handle);
    
    /* Wait for TX to be free */
    uint32_t start = HAL_GetTick();
    while (UART_DMA_IsTxBusy(handle->uart)) {
        if (HAL_GetTick() - start > 1000) {
            return false;
        }
    }
    
    HAL_StatusTypeDef status = UART_DMA_TransmitString(handle->uart, cmd);
    return (status == HAL_OK);
}



static bool wait_response(A7600_MQTT_Handle_t *handle, const char *expected, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    
    while (HAL_GetTick() - start < timeout_ms) {
        /* Read any available data */
        size_t available = UART_DMA_Available(handle->uart);
        if (available > 0) {
            size_t space = sizeof(handle->rx_buffer) - handle->rx_len - 1;
            if (available > space) available = space;
            
            size_t len = UART_DMA_Read(handle->uart, 
                                       &handle->rx_buffer[handle->rx_len], 
                                       available);
            handle->rx_len += len;
            handle->rx_buffer[handle->rx_len] = '\0';
        }
        
        /* Check for expected response */
        if (strstr((char *)handle->rx_buffer, expected) != NULL) {
            return true;
        }
        
        /* Check for error */
        if (strstr((char *)handle->rx_buffer, "ERROR") != NULL) {
            return false;
        }
        
        HAL_Delay(10);
    }
    
    return false;
}

static bool send_and_wait(A7600_MQTT_Handle_t *handle, const char *cmd, const char *expected, uint32_t timeout_ms)
{
    if (!send_at_cmd(handle, cmd)) {
        return false;
    }
    HAL_Delay(50);  /* Small delay after sending */
    return wait_response(handle, expected, timeout_ms);
}

/* ==================== Public Functions ==================== */

MQTT_Result_t A7600_MQTT_Init(A7600_MQTT_Handle_t *handle, UART_DMA_Handle_t *uart, MQTT_Config_t *config)
{
    if (handle == NULL || uart == NULL || config == NULL) {
        return MQTT_ERROR;
    }
    
    /* Store handles */
    handle->uart = uart;
    memcpy(&handle->config, config, sizeof(MQTT_Config_t));
    
    /* Initialize state */
    handle->state = MQTT_STATE_IDLE;
    handle->msg_callback = NULL;
    handle->rx_len = 0;
    handle->response_ready = false;
    handle->cmd_ok = false;
    handle->connected = false;
    
    handle->connected = false;
    
    clear_rx_buffer(handle);
    
    LOG_INFO("A7600 MQTT Initialized");
    return MQTT_OK;
}

void A7600_MQTT_SetMessageCallback(A7600_MQTT_Handle_t *handle, MQTT_MessageCallback_t callback)
{
    if (handle != NULL) {
        handle->msg_callback = callback;
    }
}

MQTT_Result_t A7600_MQTT_Connect(A7600_MQTT_Handle_t *handle)
{
    char cmd[AT_CMD_MAX_LEN];
    int retry;
    
    if (handle == NULL) {
        return MQTT_ERROR;
    }
    
    /* Clear error info */
    handle->error_step = 0;
    memset(handle->last_response, 0, sizeof(handle->last_response));
    
    /* ========== Step 1: Test module communication ========== */
    LOG_INFO("Step 1: Testing module communication...");
    handle->state = MQTT_STATE_STARTING;
    retry = 3;
    while (retry-- > 0) {
        if (send_and_wait(handle, "AT\r\n", "OK", 2000)) {
            break;
        }
        HAL_Delay(1000);
    }
    if (retry < 0) {
        LOG_ERROR("Module not responding to AT commands");
        handle->error_step = 1;
        strncpy(handle->last_response, (char *)handle->rx_buffer, sizeof(handle->last_response) - 1);
        handle->state = MQTT_STATE_ERROR;
        return MQTT_ERROR;  /* Module not responding */
    }
    HAL_Delay(500);
    
    /* ========== Step 2: Check SIM card ========== */
    LOG_INFO("Step 2: Checking SIM card...");
    if (!send_and_wait(handle, "AT+CPIN?\r\n", "+CPIN: READY", MQTT_CMD_TIMEOUT)) {
        handle->error_step = 2;
        strncpy(handle->last_response, (char *)handle->rx_buffer, sizeof(handle->last_response) - 1);
        handle->state = MQTT_STATE_ERROR;
        LOG_ERROR("SIM Card Error or PIN Required");
        return MQTT_ERROR;  /* SIM not inserted or PIN required */
    }
    HAL_Delay(500);
    
    /* ========== Step 3: Check network registration ========== */
    LOG_INFO("Step 3: Checking Network Registration...");
    retry = 30;  /* Wait up to 30 seconds for network */
    while (retry-- > 0) {
        if (send_and_wait(handle, "AT+CREG?\r\n", "+CREG: 0,1", 2000) ||
            send_and_wait(handle, "AT+CREG?\r\n", "+CREG: 0,5", 2000)) {
            break;  /* Registered home (1) or roaming (5) */
        }
        HAL_Delay(1000);
    }
    if (retry < 0) {
        handle->error_step = 3;
        strncpy(handle->last_response, (char *)handle->rx_buffer, sizeof(handle->last_response) - 1);
        handle->state = MQTT_STATE_ERROR;
        LOG_ERROR("Network Registration Failed");
        return MQTT_ERROR;  /* Not registered to network */
    }
    HAL_Delay(500);
    
    /* ========== Step 4: Check GPRS/LTE registration ========== */
    retry = 30;
    while (retry-- > 0) {
        if (send_and_wait(handle, "AT+CGREG?\r\n", "+CGREG: 0,1", 2000) ||
            send_and_wait(handle, "AT+CGREG?\r\n", "+CGREG: 0,5", 2000)) {
            break;  /* GPRS registered */
        }
        HAL_Delay(1000);
    }
    if (retry < 0) {
        handle->error_step = 4;
        strncpy(handle->last_response, (char *)handle->rx_buffer, sizeof(handle->last_response) - 1);
        handle->state = MQTT_STATE_ERROR;
        return MQTT_ERROR;  /* GPRS not registered */
    }
    HAL_Delay(500);
    
    /* ========== Step 5: Activate PDP context ========== */
    /* First deactivate any existing context */
    send_and_wait(handle, "AT+CGACT=0,1\r\n", "OK", 5000);
    HAL_Delay(500);
    
    /* Set APN (default, may need to change for specific carrier) */
    send_and_wait(handle, "AT+CGDCONT=1,\"IP\",\"internet\"\r\n", "OK", 2000);
    HAL_Delay(200);
    
    /* Activate PDP context */
    if (!send_and_wait(handle, "AT+CGACT=1,1\r\n", "OK", 10000)) {
        handle->error_step = 5;
        strncpy(handle->last_response, (char *)handle->rx_buffer, sizeof(handle->last_response) - 1);
        handle->state = MQTT_STATE_ERROR;
        return MQTT_ERROR;  /* Failed to activate data */
    }
    HAL_Delay(1000);
    
    /* ========== Step 6: Check signal quality ========== */
    send_and_wait(handle, "AT+CSQ\r\n", "OK", 2000);
    HAL_Delay(200);
    
    /* ========== Step 7: Start MQTT service ========== */
    /* First stop any existing MQTT session */
    send_and_wait(handle, "AT+CMQTTDISC=0,60\r\n", "OK", 2000);
    HAL_Delay(200);
    send_and_wait(handle, "AT+CMQTTREL=0\r\n", "OK", 2000);
    HAL_Delay(200);
    send_and_wait(handle, "AT+CMQTTSTOP\r\n", "OK", 2000);
    HAL_Delay(500);
    
    /* Start MQTT service */
    if (!send_and_wait(handle, "AT+CMQTTSTART\r\n", "OK", MQTT_CMD_TIMEOUT)) {
        /* May already be started, check with +CMQTTSTART: 0 */
        if (strstr((char *)handle->rx_buffer, "+CMQTTSTART: 0") == NULL) {
            handle->error_step = 7;
            strncpy(handle->last_response, (char *)handle->rx_buffer, sizeof(handle->last_response) - 1);
            handle->state = MQTT_STATE_ERROR;
            return MQTT_ERROR;
        }
    }
    HAL_Delay(500);
    
    /* ========== Step 8: Acquire MQTT client ========== */
    handle->state = MQTT_STATE_ACQUIRING;
    snprintf(cmd, sizeof(cmd), "AT+CMQTTACCQ=0,\"%s\",1\r\n", handle->config.client_id);
    if (!send_and_wait(handle, cmd, "OK", MQTT_CMD_TIMEOUT)) {
        handle->error_step = 8;
        strncpy(handle->last_response, (char *)handle->rx_buffer, sizeof(handle->last_response) - 1);
        handle->state = MQTT_STATE_ERROR;
        return MQTT_ERROR;
    }
    HAL_Delay(500);
    
    /* ========== Step 9: Configure SSL ========== */
    if (handle->config.use_ssl) {
        handle->state = MQTT_STATE_SSL_CONFIG;
        
        /* Configure SSL Context 0 */
        /* TLS 1.2 */
        send_and_wait(handle, "AT+CSSLCFG=\"sslversion\",0,4\r\n", "OK", 2000);
        HAL_Delay(100);
        
        /* Configure Server CA Certificate - REMOVED */
        /* send_and_wait(handle, "AT+CSSLCFG=\"cacert\",0,\"customer_root_ca.pem\"\r\n", "OK", 2000); */
        /* HAL_Delay(100); */

        /* Set Authentication Mode: 0 (No Verify) */
        send_and_wait(handle, "AT+CSSLCFG=\"authmode\",0,0\r\n", "OK", 2000);
        HAL_Delay(100);
        
        /* Enable SNI (Required for HiveMQ Cloud) */
        send_and_wait(handle, "AT+CSSLCFG=\"enableSNI\",0,1\r\n", "OK", 2000);
        HAL_Delay(100);
        
        /* Ignore time check */
        send_and_wait(handle, "AT+CSSLCFG=\"ignorelocaltime\",0,1\r\n", "OK", 2000);
        HAL_Delay(100);

        /* Bind SSL Context 0 to MQTT Session 0 */
        if (!send_and_wait(handle, "AT+CMQTTSSLCFG=0,0\r\n", "OK", MQTT_CMD_TIMEOUT)) {
            handle->error_step = 9;
            strncpy(handle->last_response, (char *)handle->rx_buffer, sizeof(handle->last_response) - 1);
            handle->state = MQTT_STATE_ERROR;
            return MQTT_ERROR;
        }
        HAL_Delay(200);
    }
    
    /* ========== Step 10: Connect to MQTT broker ========== */
    handle->state = MQTT_STATE_CONNECTING;
    snprintf(cmd, sizeof(cmd), 
             "AT+CMQTTCONNECT=0,\"tcp://%s:%d\",%d,1,\"%s\",\"%s\"\r\n",
             handle->config.broker,
             handle->config.port,
             handle->config.keepalive,
             handle->config.username,
             handle->config.password);
    
    if (!send_and_wait(handle, cmd, "+CMQTTCONNECT: 0,0", MQTT_RESPONSE_TIMEOUT)) {
        handle->error_step = 10;
        strncpy(handle->last_response, (char *)handle->rx_buffer, sizeof(handle->last_response) - 1);
        handle->state = MQTT_STATE_ERROR;
        LOG_ERROR("MQTT Connect Failed. Response: %s", handle->rx_buffer);
        return MQTT_ERROR;
    }
    
    handle->state = MQTT_STATE_CONNECTED;
    handle->connected = true;
    
    LOG_INFO("MQTT Connected Successfully to %s", handle->config.broker);
    return MQTT_OK;
}

MQTT_Result_t A7600_MQTT_Disconnect(A7600_MQTT_Handle_t *handle)
{
    if (handle == NULL) {
        return MQTT_ERROR;
    }
    
    handle->state = MQTT_STATE_DISCONNECTING;
    
    /* Disconnect */
    send_and_wait(handle, "AT+CMQTTDISC=0,60\r\n", "OK", MQTT_CMD_TIMEOUT);
    HAL_Delay(500);
    
    /* Release client */
    send_and_wait(handle, "AT+CMQTTREL=0\r\n", "OK", MQTT_CMD_TIMEOUT);
    HAL_Delay(500);
    
    /* Stop MQTT service */
    send_and_wait(handle, "AT+CMQTTSTOP\r\n", "OK", MQTT_CMD_TIMEOUT);
    
    handle->state = MQTT_STATE_IDLE;
    handle->connected = false;
    
    return MQTT_OK;
}

bool A7600_UploadCert(A7600_MQTT_Handle_t *handle, const char *filename, const char *data, size_t len)
{
    char cmd[AT_CMD_MAX_LEN];
    const size_t CHUNK_SIZE = 512;
    size_t sent = 0;
    
    if (handle == NULL || filename == NULL || data == NULL) return false;
    
    LOG_INFO("Uploading Certificate: %s (%d bytes)", filename, len);
    
    /* Step 1: Send AT+CCERTDOWN command */
    snprintf(cmd, sizeof(cmd), "AT+CCERTDOWN=\"%s\",%d\r\n", filename, len);
    if (!send_and_wait(handle, cmd, ">", 2000)) {
        LOG_ERROR("Failed to start cert upload");
        return false;
    }
    
    /* Step 2: Send data in chunks */
    while (sent < len) {
        size_t chunk = len - sent;
        if (chunk > CHUNK_SIZE) chunk = CHUNK_SIZE;
        
        UART_DMA_Transmit(handle->uart, (uint8_t*)&data[sent], chunk);
        sent += chunk;
        
        /* Small delay to prevent UART buffer overflow if any */
        HAL_Delay(50);
    }
    
    /* Step 3: Wait for OK */
    if (!wait_response(handle, "OK", 5000)) {
        LOG_ERROR("Cert upload failed to receive OK");
        return false;
    }
    
    LOG_INFO("Certificate Uploaded Successfully");
    return true;
}

MQTT_Result_t A7600_MQTT_Subscribe(A7600_MQTT_Handle_t *handle, const char *topic, MQTT_QoS_t qos)
{
    char cmd[AT_CMD_MAX_LEN];
    
    if (handle == NULL || topic == NULL) {
        return MQTT_ERROR;
    }
    
    if (!handle->connected) {
        return MQTT_NOT_CONNECTED;
    }
    
    handle->state = MQTT_STATE_SUBSCRIBING;
    
    /* Subscribe to topic */
    snprintf(cmd, sizeof(cmd), "AT+CMQTTSUB=0,\"%s\",%d\r\n", topic, qos);
    if (!send_and_wait(handle, cmd, "+CMQTTSUB: 0,0", MQTT_CMD_TIMEOUT)) {
        handle->state = MQTT_STATE_CONNECTED;
        return MQTT_ERROR;
    }
    
    handle->state = MQTT_STATE_CONNECTED;
    return MQTT_OK;
}

MQTT_Result_t A7600_MQTT_Unsubscribe(A7600_MQTT_Handle_t *handle, const char *topic)
{
    char cmd[AT_CMD_MAX_LEN];
    
    if (handle == NULL || topic == NULL) {
        return MQTT_ERROR;
    }
    
    if (!handle->connected) {
        return MQTT_NOT_CONNECTED;
    }
    
    snprintf(cmd, sizeof(cmd), "AT+CMQTTUNSUB=0,\"%s\"\r\n", topic);
    if (!send_and_wait(handle, cmd, "OK", MQTT_CMD_TIMEOUT)) {
        return MQTT_ERROR;
    }
    
    return MQTT_OK;
}

MQTT_Result_t A7600_MQTT_Publish(A7600_MQTT_Handle_t *handle, const char *topic,
                                  const uint8_t *payload, size_t len,
                                  MQTT_QoS_t qos, bool retain)
{
    char cmd[AT_CMD_MAX_LEN];
    
    if (handle == NULL || topic == NULL || payload == NULL) {
        return MQTT_ERROR;
    }
    
    if (!handle->connected) {
        return MQTT_NOT_CONNECTED;
    }
    
    handle->state = MQTT_STATE_PUBLISHING;
    
    /* Step 1: Set topic */
    size_t topic_len = strlen(topic);
    snprintf(cmd, sizeof(cmd), "AT+CMQTTTOPIC=0,%d\r\n", topic_len);
    if (!send_and_wait(handle, cmd, ">", MQTT_CMD_TIMEOUT)) {
        LOG_ERROR("Publish Failed: Set Topic Length. Resp: %s", handle->rx_buffer);
        handle->state = MQTT_STATE_CONNECTED;
        return MQTT_ERROR;
    }
    
    /* Send topic */
    if (!send_and_wait(handle, topic, "OK", MQTT_CMD_TIMEOUT)) {
        LOG_ERROR("Publish Failed: Send Topic. Resp: %s", handle->rx_buffer);
        handle->state = MQTT_STATE_CONNECTED;
        return MQTT_ERROR;
    }
    HAL_Delay(100);
    
    /* Step 2: Set payload */
    snprintf(cmd, sizeof(cmd), "AT+CMQTTPAYLOAD=0,%d\r\n", len);
    if (!send_and_wait(handle, cmd, ">", MQTT_CMD_TIMEOUT)) {
        LOG_ERROR("Publish Failed: Set Payload Length. Resp: %s", handle->rx_buffer);
        handle->state = MQTT_STATE_CONNECTED;
        return MQTT_ERROR;
    }
    
    /* Send payload */
    clear_rx_buffer(handle);
    UART_DMA_Transmit(handle->uart, payload, len);
    if (!wait_response(handle, "OK", MQTT_CMD_TIMEOUT)) {
        LOG_ERROR("Publish Failed: Send Payload. Resp: %s", handle->rx_buffer);
        handle->state = MQTT_STATE_CONNECTED;
        return MQTT_ERROR;
    }
    HAL_Delay(100);
    
    /* Step 3: Publish */
    /* AT+CMQTTPUB=<client_index>,<qos>,<pub_timeout> */
    /* Note: retain is often not supported directly or requires different cmd structure. 
       Standard SIM7600 uses client,qos,timeout */
    snprintf(cmd, sizeof(cmd), "AT+CMQTTPUB=0,%d,60\r\n", qos);
    if (!send_and_wait(handle, cmd, "+CMQTTPUB: 0,0", MQTT_CMD_TIMEOUT)) {
        LOG_ERROR("Publish Failed: Execute Pub. Resp: %s", handle->rx_buffer);
        handle->state = MQTT_STATE_CONNECTED;
        return MQTT_ERROR;
    }
    
    handle->state = MQTT_STATE_CONNECTED;
    return MQTT_OK;
}

MQTT_Result_t A7600_MQTT_PublishString(A7600_MQTT_Handle_t *handle, const char *topic,
                                        const char *message, MQTT_QoS_t qos)
{
    return A7600_MQTT_Publish(handle, topic, (const uint8_t *)message, 
                               strlen(message), qos, false);
}

void A7600_MQTT_Process(A7600_MQTT_Handle_t *handle)
{
    if (handle == NULL || !handle->connected) {
        return;
    }
    
    /* Read any available data */
    size_t available = UART_DMA_Available(handle->uart);
    if (available > 0) {
        size_t space = sizeof(handle->rx_buffer) - handle->rx_len - 1;
        if (available > space) available = space;
        
        size_t len = UART_DMA_Read(handle->uart, 
                                   &handle->rx_buffer[handle->rx_len], 
                                   available);
        handle->rx_len += len;
        handle->rx_buffer[handle->rx_len] = '\0';
        
        /* Check for incoming message: +CMQTTRXSTART: ... */
        char *msg_start = strstr((char *)handle->rx_buffer, "+CMQTTRXSTART:");
        if (msg_start != NULL) {
            /* Parse incoming message */
            char *payload_start = strstr((char *)handle->rx_buffer, "+CMQTTRXPAYLOAD:");
            char *msg_end = strstr((char *)handle->rx_buffer, "+CMQTTRXEND:");
            
            if (payload_start != NULL && msg_end != NULL && handle->msg_callback != NULL) {
                /* Extract topic and payload (simplified parsing) */
                /* In real implementation, parse the full URC format */
                handle->msg_callback("", handle->rx_buffer, handle->rx_len);
            }
            
            /* Clear buffer after processing */
            if (msg_end != NULL) {
                clear_rx_buffer(handle);
            }
        }
        
        /* Check for disconnect */
        if (strstr((char *)handle->rx_buffer, "+CMQTTCONNLOST:") != NULL) {
            handle->connected = false;
            handle->state = MQTT_STATE_IDLE;
            clear_rx_buffer(handle);
        }
    }
}

bool A7600_MQTT_IsConnected(A7600_MQTT_Handle_t *handle)
{
    if (handle == NULL) {
        return false;
    }
    return handle->connected;
}

MQTT_State_t A7600_MQTT_GetState(A7600_MQTT_Handle_t *handle)
{
    if (handle == NULL) {
        return MQTT_STATE_IDLE;
    }
    return handle->state;
}

uint8_t A7600_MQTT_GetErrorStep(A7600_MQTT_Handle_t *handle)
{
    if (handle == NULL) {
        return 0;
    }
    return handle->error_step;
}

const char* A7600_MQTT_GetLastResponse(A7600_MQTT_Handle_t *handle)
{
    if (handle == NULL) {
        return "";
    }
    return handle->last_response;
}
