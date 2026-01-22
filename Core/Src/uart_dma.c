/**
 * @file    uart_dma.c
 * @brief   UART DMA Library Implementation
 * @author  Auto-generated
 * @version 1.2 - No ring buffer, simple DMA TX
 */

#include "uart_dma.h"
#include <string.h>

/**
 * @brief Get current DMA write position
 */
size_t UART_DMA_GetDMAPos(UART_DMA_Handle_t *handle)
{
    /* DMA Counter decreases, so position = SIZE - Counter */
    return UART_DMA_RX_BUFFER_SIZE - __HAL_DMA_GET_COUNTER(handle->huart->hdmarx);
}

HAL_StatusTypeDef UART_DMA_Init(UART_DMA_Handle_t *handle, UART_HandleTypeDef *huart)
{
    HAL_StatusTypeDef status;
    
    /* Store UART handle */
    handle->huart = huart;
    
    /* Clear buffers */
    memset(handle->rx_buffer, 0, UART_DMA_RX_BUFFER_SIZE);
    memset(handle->tx_buffer, 0, UART_DMA_TX_BUFFER_SIZE);
    
    /* Initialize state - read position starts at 0 */
    handle->rx_read_pos = 0;
    handle->tx_busy = false;
    
    /* Enable IDLE interrupt */
    __HAL_UART_ENABLE_IT(huart, UART_IT_IDLE);
    
    /* Start DMA reception in circular mode */
    status = HAL_UART_Receive_DMA(huart, handle->rx_buffer, UART_DMA_RX_BUFFER_SIZE);
    
    return status;
}

void UART_DMA_IDLE_IRQHandler(UART_DMA_Handle_t *handle)
{
    if (__HAL_UART_GET_FLAG(handle->huart, UART_FLAG_IDLE)) {
        /* Clear IDLE flag by reading SR then DR */
        __HAL_UART_CLEAR_IDLEFLAG(handle->huart);
        
        /* IDLE detected - data is now available in rx_buffer */
        /* No processing needed, just clear the flag */
    }
}

size_t UART_DMA_Available(UART_DMA_Handle_t *handle)
{
    size_t dma_pos = UART_DMA_GetDMAPos(handle);
    size_t read_pos = handle->rx_read_pos;
    
    if (dma_pos >= read_pos) {
        return dma_pos - read_pos;
    } else {
        /* Wrap around case */
        return UART_DMA_RX_BUFFER_SIZE - read_pos + dma_pos;
    }
}

size_t UART_DMA_Read(UART_DMA_Handle_t *handle, uint8_t *data, size_t len)
{
    size_t available = UART_DMA_Available(handle);
    size_t to_read = (len < available) ? len : available;
    size_t read_count = 0;
    size_t read_pos = handle->rx_read_pos;
    
    for (size_t i = 0; i < to_read; i++) {
        data[i] = handle->rx_buffer[read_pos];
        read_pos = (read_pos + 1) % UART_DMA_RX_BUFFER_SIZE;
        read_count++;
    }
    
    /* Update read position */
    handle->rx_read_pos = read_pos;
    
    return read_count;
}

bool UART_DMA_ReadByte(UART_DMA_Handle_t *handle, uint8_t *data)
{
    if (UART_DMA_Available(handle) == 0) {
        return false;
    }
    
    *data = handle->rx_buffer[handle->rx_read_pos];
    handle->rx_read_pos = (handle->rx_read_pos + 1) % UART_DMA_RX_BUFFER_SIZE;
    
    return true;
}

void UART_DMA_TxCplt_Callback(UART_DMA_Handle_t *handle)
{
    /* TX complete - mark as idle */
    handle->tx_busy = false;
}

HAL_StatusTypeDef UART_DMA_Transmit(UART_DMA_Handle_t *handle, const uint8_t *data, size_t len)
{
    if (len == 0) {
        return HAL_OK;
    }
    
    if (handle->tx_busy) {
        /* TX in progress - return busy */
        return HAL_BUSY;
    }
    
    /* Limit to buffer size */
    size_t to_send = (len > UART_DMA_TX_BUFFER_SIZE) ? UART_DMA_TX_BUFFER_SIZE : len;
    
    /* Copy data to TX buffer */
    memcpy(handle->tx_buffer, data, to_send);
    
    /* Start transmission */
    handle->tx_busy = true;
    HAL_StatusTypeDef status = HAL_UART_Transmit_DMA(handle->huart, handle->tx_buffer, to_send);
    
    if (status != HAL_OK) {
        handle->tx_busy = false;
    }
    
    return status;
}

HAL_StatusTypeDef UART_DMA_TransmitString(UART_DMA_Handle_t *handle, const char *str)
{
    return UART_DMA_Transmit(handle, (const uint8_t *)str, strlen(str));
}

bool UART_DMA_IsTxBusy(UART_DMA_Handle_t *handle)
{
    return handle->tx_busy;
}

void UART_DMA_FlushRx(UART_DMA_Handle_t *handle)
{
    /* Set read position to current DMA position */
    handle->rx_read_pos = UART_DMA_GetDMAPos(handle);
}
