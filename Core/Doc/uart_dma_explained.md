# UART DMA Ring Buffer - Giải Thích Chi Tiết

## Tổng Quan Nguyên Lý

Thư viện `uart_dma.c` sử dụng kỹ thuật **DMA Circular Mode** kết hợp với **IDLE Line Interrupt** để nhận dữ liệu UART một cách hiệu quả mà không cần CPU can thiệp liên tục.

---

## 1. Cấu Trúc Dữ Liệu

```c
typedef struct {
    UART_HandleTypeDef *huart;          // Handle UART của HAL
    
    // RX: 2-stage buffer
    uint8_t rx_dma_buffer[512];         // Buffer DMA ghi trực tiếp vào
    RingBuffer_t rx_ring;               // Ring buffer cho application đọc
    volatile size_t rx_dma_old_pos;     // Vị trí DMA cũ để detect dữ liệu mới
    
    // TX
    uint8_t tx_buffer[512];             // Buffer gửi qua DMA
    RingBuffer_t tx_ring;               // Queue cho dữ liệu chờ gửi
    volatile bool tx_busy;              // Flag đang truyền
    
} UART_DMA_Handle_t;
```

### Tại sao cần 2-stage buffer cho RX?

```
┌─────────────────────────────────────────────────────────────────┐
│                    LUỒNG DỮ LIỆU RX                             │
│                                                                 │
│   UART RX Pin                                                   │
│       │                                                         │
│       ▼                                                         │
│   ┌───────────────────┐     DMA liên tục ghi                    │
│   │  rx_dma_buffer[]  │ ◄── (circular mode)                     │
│   │   [0][1][2]...[N] │                                         │
│   └───────────────────┘                                         │
│       │                                                         │
│       │ IDLE interrupt hoặc Half/Full Complete                  │
│       ▼                                                         │
│   ┌───────────────────┐     Copy dữ liệu mới                    │
│   │    rx_ring        │ ◄── (so sánh old_pos với DMA counter)   │
│   │   Ring Buffer     │                                         │
│   └───────────────────┘                                         │
│       │                                                         │
│       ▼                                                         │
│   Application đọc qua UART_DMA_Read()                           │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

- **rx_dma_buffer**: DMA controller ghi trực tiếp, application KHÔNG nên đọc trực tiếp
- **rx_ring**: Safe buffer cho application, dữ liệu được copy khi có interrupt

---

## 2. DMA Circular Mode

### Cấu hình trong `stm32f0xx_hal_msp.c`:
```c
hdma_usart2_rx.Init.Mode = DMA_CIRCULAR;  // Thay vì DMA_NORMAL
```

### Hoạt động:
```
Initial:
┌───┬───┬───┬───┬───┬───┬───┬───┐
│ 0 │ 1 │ 2 │ 3 │ 4 │ 5 │ 6 │ 7 │  ◄─ DMA buffer (size=8)
└───┴───┴───┴───┴───┴───┴───┴───┘
  ▲
  DMA write pointer (CNDTR=8)

Sau khi nhận 3 bytes 'A','B','C':
┌───┬───┬───┬───┬───┬───┬───┬───┐
│ A │ B │ C │   │   │   │   │   │
└───┴───┴───┴───┴───┴───┴───┴───┘
          ▲
          DMA write pointer (CNDTR=5)

Sau khi nhận thêm 6 bytes, DMA wrap around:
┌───┬───┬───┬───┬───┬───┬───┬───┐
│ I │ B │ C │ D │ E │ F │ G │ H │
└───┴───┴───┴───┴───┴───┴───┴───┘
  ▲
  DMA write pointer (CNDTR=7, đã ghi đè vị trí 0)
```

**Công thức tính vị trí DMA hiện tại:**
```c
pos = BUFFER_SIZE - __HAL_DMA_GET_COUNTER(hdmarx);
// CNDTR (Counter) giảm dần khi DMA ghi
```

---

## 3. IDLE Line Interrupt

### Vấn đề với DMA thông thường:
- DMA chỉ báo interrupt khi Half Complete hoặc Full Complete
- Nếu nhận 5 bytes trong buffer 512 bytes → không có interrupt!

### Giải pháp: IDLE Interrupt
- IDLE flag được set khi đường RX **im lặng** một khoảng thời gian (≈ 1 byte time)
- Báo hiệu: "Đã nhận xong một frame dữ liệu"

```
Dữ liệu: "AT\r\n" (4 bytes)

    ┌─┐ ┌─┐ ┌──┐ ┌──┐
────┘ └─┘ └─┘  └─┘  └────────────────
    'A' 'T' '\r' '\n'   ← IDLE detected!
                        └─ Trigger interrupt
```

### Code xử lý trong `stm32f0xx_it.c`:
```c
void USART2_IRQHandler(void)
{
    // Gọi TRƯỚC HAL_UART_IRQHandler
    UART_DMA_IDLE_IRQHandler(&sim_uart);
    
    HAL_UART_IRQHandler(&huart2);
}
```

---

## 4. Hàm `UART_DMA_ProcessRxData()` - Trái Tim Của Thư Viện

```c
static void UART_DMA_ProcessRxData(UART_DMA_Handle_t *handle)
{
    // Lấy vị trí DMA hiện tại
    size_t pos = BUFFER_SIZE - __HAL_DMA_GET_COUNTER(hdmarx);
    size_t old_pos = handle->rx_dma_old_pos;
    
    if (pos != old_pos) {
        if (pos > old_pos) {
            // CASE 1: Bình thường, không wrap
            RingBuffer_Write(&rx_ring, &buffer[old_pos], pos - old_pos);
        } else {
            // CASE 2: DMA đã wrap around
            RingBuffer_Write(&rx_ring, &buffer[old_pos], SIZE - old_pos);
            RingBuffer_Write(&rx_ring, &buffer[0], pos);
        }
        handle->rx_dma_old_pos = pos;
    }
}
```

### Minh họa 2 cases:

**CASE 1: Không wrap (pos > old_pos)**
```
old_pos=2, pos=5, nhận được 3 bytes
┌───┬───┬───┬───┬───┬───┬───┬───┐
│   │   │ D │ E │ F │   │   │   │
└───┴───┴───┴───┴───┴───┴───┴───┘
          ▲       ▲
       old_pos   pos
          └───────┘
           Copy 3 bytes
```

**CASE 2: Wrap around (pos < old_pos)**
```
old_pos=6, pos=2 (DMA đã quay lại đầu)
┌───┬───┬───┬───┬───┬───┬───┬───┐
│ H │ I │   │   │   │   │ F │ G │
└───┴───┴───┴───┴───┴───┴───┴───┘
      ▲                   ▲
     pos               old_pos

Step 1: Copy [6] đến [7] (2 bytes: F, G)
Step 2: Copy [0] đến [1] (2 bytes: H, I)
```

---

## 5. Luồng Truyền TX

```
┌─────────────────────────────────────────────────────────────────┐
│                    LUỒNG DỮ LIỆU TX                             │
│                                                                 │
│   Application gọi UART_DMA_Transmit(data, len)                  │
│       │                                                         │
│       ▼                                                         │
│   ┌─────────────────┐                                           │
│   │ tx_busy == 0 ?  │──── No ───► Queue vào tx_ring             │
│   └─────────────────┘                                           │
│       │ Yes                                                     │
│       ▼                                                         │
│   Copy data vào tx_buffer                                       │
│   Set tx_busy = true                                            │
│   HAL_UART_Transmit_DMA()                                       │
│       │                                                         │
│       ▼                                                         │
│   DMA truyền xong → TX Complete Interrupt                       │
│       │                                                         │
│       ▼                                                         │
│   ┌─────────────────┐                                           │
│   │ tx_ring empty?  │──── Yes ──► tx_busy = false               │
│   └─────────────────┘                                           │
│       │ No                                                      │
│       ▼                                                         │
│   Đọc từ tx_ring, truyền tiếp                                   │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## 6. Các Interrupt Callback

| Callback | Khi nào | Mục đích |
|----------|---------|----------|
| `IDLE_IRQHandler` | RX line im lặng | Detect frame hoàn chỉnh |
| `RxHalfCplt` | DMA ghi tới 50% buffer | Tránh mất data nếu buffer đầy nhanh |
| `RxCplt` | DMA ghi tới 100% buffer | Xử lý trước khi wrap |
| `TxCplt` | TX DMA xong | Gửi data tiếp từ queue |

---

## 7. Thread Safety Considerations

```c
volatile size_t rx_dma_old_pos;  // volatile vì access từ ISR và main
volatile bool tx_busy;            // volatile vì access từ ISR và main
```

### Critical Sections:
- DMA và ISR có thể modify buffer bất cứ lúc nào
- `UART_DMA_Read()` gọi `ProcessRxData()` trước để đảm bảo data mới nhất
- Ring buffer operations phải atomic (không bị interrupt giữa chừng)

---

## 8. Ưu Điểm Của Thiết Kế Này

| Đặc điểm | Lợi ích |
|----------|---------|
| DMA Circular | CPU không cần restart DMA, liên tục nhận |
| IDLE Interrupt | Detect frame bất kỳ độ dài, không cần biết trước size |
| 2-stage Buffer | DMA và application không conflict |
| Ring Buffer TX | Queue nhiều lệnh, gửi tuần tự |
| Non-blocking | Application không bị block đợi UART |

---

## 9. Sequence Diagram

```
┌─────────┐     ┌─────────┐     ┌─────────┐     ┌─────────┐
│  A7600  │     │   DMA   │     │   ISR   │     │  Main   │
└────┬────┘     └────┬────┘     └────┬────┘     └────┬────┘
     │               │               │               │
     │──TX "OK\r\n"──│               │               │
     │               │               │               │
     │               │──Write to ────│               │
     │               │  rx_dma_buf   │               │
     │               │               │               │
     │──(IDLE)──────►│               │               │
     │               │──IDLE IRQ────►│               │
     │               │               │──Process──────│
     │               │               │  RxData       │
     │               │               │──Copy to──────│
     │               │               │  rx_ring      │
     │               │               │               │
     │               │               │               │──Available()?
     │               │               │               │
     │               │               │               │──Read() ──►
     │               │               │               │  "OK\r\n"
```
