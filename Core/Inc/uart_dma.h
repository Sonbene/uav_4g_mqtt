/**
 * @file    uart_dma.h
 * @brief   UART DMA Library for A7600 SIM Module
 * @author  Auto-generated
 * @version 1.2 - Simplified TX without ring buffer
 */

#ifndef UART_DMA_H
#define UART_DMA_H

#include "main.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Buffer sizes - adjusted for MAVLink */
/* Buffer sizes - adjusted for MAVLink (and RAM constraints) */
#define UART_DMA_RX_BUFFER_SIZE     512
#define UART_DMA_TX_BUFFER_SIZE     512

/**
 * @brief UART DMA Handle structure (simplified RX)
 */
typedef struct {
    UART_HandleTypeDef *huart;          /**< UART handle */
    
    /* RX DMA circular buffer - read directly from here */
    uint8_t rx_buffer[UART_DMA_RX_BUFFER_SIZE];      /**< DMA RX buffer (circular) */
    volatile size_t rx_read_pos;                      /**< Application read position */
    
    /* TX buffer - simple DMA */
    uint8_t tx_buffer[UART_DMA_TX_BUFFER_SIZE];      /**< TX DMA buffer */
    volatile bool tx_busy;                            /**< TX in progress flag */
    
} UART_DMA_Handle_t;

/**
 * @brief Initialize UART DMA with circular RX
 * @param handle Pointer to UART DMA handle
 * @param huart Pointer to HAL UART handle
 * @return HAL_OK on success
 */
HAL_StatusTypeDef UART_DMA_Init(UART_DMA_Handle_t *handle, UART_HandleTypeDef *huart);

/**
 * @brief Process UART IDLE interrupt - call from USART IRQ handler
 * @param handle Pointer to UART DMA handle
 */
void UART_DMA_IDLE_IRQHandler(UART_DMA_Handle_t *handle);

/**
 * @brief Process DMA TX Complete callback
 * @param handle Pointer to UART DMA handle
 */
void UART_DMA_TxCplt_Callback(UART_DMA_Handle_t *handle);

/**
 * @brief Get number of bytes available to read
 * @param handle Pointer to UART DMA handle
 * @return Number of bytes available
 */
size_t UART_DMA_Available(UART_DMA_Handle_t *handle);

/**
 * @brief Get current DMA write position
 * @param handle Pointer to UART DMA handle
 * @return Current DMA position
 */
size_t UART_DMA_GetDMAPos(UART_DMA_Handle_t *handle);

/**
 * @brief Read data from RX buffer
 * @param handle Pointer to UART DMA handle
 * @param data Pointer to store data
 * @param len Maximum bytes to read
 * @return Actual bytes read
 */
size_t UART_DMA_Read(UART_DMA_Handle_t *handle, uint8_t *data, size_t len);

/**
 * @brief Read one byte from RX buffer
 * @param handle Pointer to UART DMA handle
 * @param data Pointer to store byte
 * @return true if byte read, false if empty
 */
bool UART_DMA_ReadByte(UART_DMA_Handle_t *handle, uint8_t *data);

/**
 * @brief Transmit data via DMA
 * @param handle Pointer to UART DMA handle
 * @param data Pointer to data to send
 * @param len Number of bytes to send
 * @return HAL_OK on success, HAL_BUSY if TX in progress
 */
HAL_StatusTypeDef UART_DMA_Transmit(UART_DMA_Handle_t *handle, const uint8_t *data, size_t len);

/**
 * @brief Transmit string via DMA
 * @param handle Pointer to UART DMA handle
 * @param str Null-terminated string
 * @return HAL_OK on success
 */
HAL_StatusTypeDef UART_DMA_TransmitString(UART_DMA_Handle_t *handle, const char *str);

/**
 * @brief Check if TX is busy
 * @param handle Pointer to UART DMA handle
 * @return true if transmitting, false if idle
 */
bool UART_DMA_IsTxBusy(UART_DMA_Handle_t *handle);

/**
 * @brief Flush RX buffer
 * @param handle Pointer to UART DMA handle
 */
void UART_DMA_FlushRx(UART_DMA_Handle_t *handle);

#endif /* UART_DMA_H */
