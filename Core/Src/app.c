/**
 * @file    app.c
 * @brief   Application Layer Implementation
 * @version 1.0
 */

#include "app.h"
#include "certificates.h"
#include "debug_log.h"
#include <stdio.h>
#include <string.h>

/* Private variables */
static char publish_buffer[128];

/* Private function prototypes */
static void mqtt_message_callback(const char *topic, const uint8_t *payload, size_t len);

/* ==================== Private Functions ==================== */

static void mqtt_message_callback(const char *topic, const uint8_t *payload, size_t len)
{
    /* Handle incoming MQTT messages */
    /* User can implement command handling here */
    
    /* Example: Parse command and respond */
    /* if (strstr(topic, "command") != NULL) { ... } */
}

/* ==================== Public Functions ==================== */

bool App_Init(App_Handle_t *app, UART_DMA_Handle_t *uart)
{
    if (app == NULL || uart == NULL) {
        return false;
    }
    
    /* Store UART handle */
    app->uart = uart;
    app->state = APP_STATE_INIT;
    app->last_publish_tick = 0;
    app->last_reconnect_tick = 0;
    app->error_count = 0;
    
    /* Configure MQTT */
    MQTT_Config_t mqtt_config = {
        .port = APP_MQTT_PORT,
        .use_ssl = true,
        .keepalive = APP_MQTT_KEEPALIVE
    };
    
    /* Copy string configurations */
    strncpy(mqtt_config.broker, APP_MQTT_BROKER, MQTT_BROKER_MAX_LEN - 1);
    strncpy(mqtt_config.username, APP_MQTT_USERNAME, MQTT_USERNAME_MAX_LEN - 1);
    strncpy(mqtt_config.password, APP_MQTT_PASSWORD, MQTT_PASSWORD_MAX_LEN - 1);
    strncpy(mqtt_config.client_id, APP_MQTT_CLIENT_ID, MQTT_CLIENT_ID_MAX_LEN - 1);
    
    /* Initialize MQTT */
    if (A7600_MQTT_Init(&app->mqtt, uart, &mqtt_config) != MQTT_OK) {
        LOG_ERROR("Failed to init MQTT driver");
        app->state = APP_STATE_ERROR;
        return false;
    }
    
    /* Upload CA Certificate - REMOVED per user request */
    /* A7600_UploadCert(&app->mqtt, "customer_root_ca.pem", isrg_root_x1, strlen(isrg_root_x1)); */
    /* HAL_Delay(500); */
    
    /* Set message callback */
    A7600_MQTT_SetMessageCallback(&app->mqtt, mqtt_message_callback);
    
    app->state = APP_STATE_WAIT_MODULE;
    return true;
}

bool App_Connect(App_Handle_t *app)
{
    if (app == NULL) {
        return false;
    }
    
    app->state = APP_STATE_CONNECTING;
    
    /* Connect to MQTT broker */
    MQTT_Result_t result = A7600_MQTT_Connect(&app->mqtt);
    
    if (result == MQTT_OK) {
        app->state = APP_STATE_CONNECTED;
        app->error_count = 0;
        
        /* Subscribe to command topic */
        A7600_MQTT_Subscribe(&app->mqtt, APP_TOPIC_COMMAND, MQTT_QOS_0);
        
        /* Publish online status */
        App_PublishStatus(app, "online");
        
        return true;
    } else {
        app->state = APP_STATE_ERROR;
        app->error_count++;
        return false;
    }
}

void App_Disconnect(App_Handle_t *app)
{
    if (app == NULL) {
        return;
    }
    
    /* Publish offline status before disconnect */
    if (app->state == APP_STATE_CONNECTED) {
        App_PublishStatus(app, "offline");
        HAL_Delay(500);
    }
    
    A7600_MQTT_Disconnect(&app->mqtt);
    app->state = APP_STATE_WAIT_MODULE;
}

bool App_PublishSensor(App_Handle_t *app, const char *data)
{
    if (app == NULL || data == NULL) {
        return false;
    }
    
    if (app->state != APP_STATE_CONNECTED) {
        return false;
    }
    
    return (A7600_MQTT_PublishString(&app->mqtt, APP_TOPIC_SENSOR, data, MQTT_QOS_0) == MQTT_OK);
}

bool App_PublishStatus(App_Handle_t *app, const char *status)
{
    if (app == NULL || status == NULL) {
        return false;
    }
    
    if (app->state != APP_STATE_CONNECTED) {
        return false;
    }
    
    return (A7600_MQTT_PublishString(&app->mqtt, APP_TOPIC_STATUS, status, MQTT_QOS_1) == MQTT_OK);
}

void App_Run(App_Handle_t *app)
{
    if (app == NULL) {
        return;
    }
    
    uint32_t current_tick = HAL_GetTick();
    
    switch (app->state) {
        case APP_STATE_INIT:
            /* Wait for initialization */
            break;
            
        case APP_STATE_WAIT_MODULE:
            /* Try to connect */
            if (current_tick - app->last_reconnect_tick >= 5000) {
                app->last_reconnect_tick = current_tick;
                App_Connect(app);
            }
            break;
            
        case APP_STATE_CONNECTING:
            /* Handled by App_Connect() */
            break;
            
        case APP_STATE_CONNECTED:
            /* Process MQTT */
            A7600_MQTT_Process(&app->mqtt);
            
            /* Check connection */
            if (!A7600_MQTT_IsConnected(&app->mqtt)) {
                app->state = APP_STATE_ERROR;
                break;
            }
            
            /* Periodic status publish */
            if (current_tick - app->last_publish_tick >= APP_PUBLISH_INTERVAL) {
                app->last_publish_tick = current_tick;
                
                /* Publish heartbeat */
                snprintf(publish_buffer, sizeof(publish_buffer), 
                         "{\"uptime\":%lu,\"errors\":%lu}", 
                         current_tick / 1000, app->error_count);
                LOG_INFO("Publishing Sensor Data: %s", publish_buffer);
                App_PublishSensor(app, publish_buffer);
            }
            break;
            
        case APP_STATE_ERROR:
            /* Try to reconnect */
            if (current_tick - app->last_reconnect_tick >= APP_RECONNECT_INTERVAL) {
                app->last_reconnect_tick = current_tick;
                
                /* Disconnect and reconnect */
                A7600_MQTT_Disconnect(&app->mqtt);
                HAL_Delay(1000);
                
                app->state = APP_STATE_WAIT_MODULE;
            }
            break;
            
        default:
            app->state = APP_STATE_INIT;
            break;
    }
}

bool App_IsConnected(App_Handle_t *app)
{
    if (app == NULL) {
        return false;
    }
    return (app->state == APP_STATE_CONNECTED) && A7600_MQTT_IsConnected(&app->mqtt);
}

App_State_t App_GetState(App_Handle_t *app)
{
    if (app == NULL) {
        return APP_STATE_INIT;
    }
    return app->state;
}
