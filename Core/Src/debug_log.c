/**
 * @file    debug_log.c
 * @brief   Debug Logging Implementation
 */

#include "debug_log.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#ifdef DEBUG_ENABLE

/* External handles from main.c */
extern UART_HandleTypeDef huart1;

/* Config */
#define DEBUG_UART      &huart1
#define LOG_BUFFER_SIZE 512

static char log_buffer[LOG_BUFFER_SIZE];
static volatile bool tx_busy = false;

/* Prototype for internal TX complete callback wrapper if needed */
void Debug_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        tx_busy = false;
    }
}

void Debug_Init(void)
{
    /* UART1 is already initialized in main.c, just ensuring state */
    tx_busy = false;
    LOG_INFO("Debug Logging Initialized");
}

void Debug_Log(const char *fmt, ...)
{
    va_list args;
    int len;
    
    va_start(args, fmt);
    len = vsnprintf(log_buffer, LOG_BUFFER_SIZE, fmt, args);
    va_end(args);

    if (len > 0) {
        if (len > LOG_BUFFER_SIZE) len = LOG_BUFFER_SIZE;
        
        /* Use blocking transmit to avoid DMA conflicts with MAVLink/UART_DMA wrapper */
        HAL_UART_Transmit(DEBUG_UART, (uint8_t*)log_buffer, len, 1000);
    }
}

#endif /* DEBUG_ENABLE */
