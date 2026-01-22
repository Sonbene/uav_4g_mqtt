# STM32 A7600 MQTT MAVLink Bridge

STM32F0-based 4G Telemetry Bridge for UAVs. Connects a Flight Controller (MAVLink) to a Cloud (MQTT) via SIMCom A7600C LTE module.

## Architecture

```
┌───────────────┐      UART1       ┌───────────────┐      UART2       ┌───────────────┐      4G LTE       ┌───────────────┐
│  Flight       │ ◄──────────────► │  STM32F0      │ ◄──────────────► │  A7600C       │ ◄────────────────► │  MQTT Broker  │
│  Controller   │     (MAVLink)    │  (This Code)  │   (AT Commands)  │  Module       │    (SSL/TLS)      │  (HiveMQ)     │
└───────────────┘                  └───────────────┘                  └───────────────┘                   └───────────────┘
```

## Features

| Feature | Description |
|---------|-------------|
| **MQTT over 4G** | Secure MQTT 3.1.1 via A7600C (SSL/TLS) |
| **MAVLink Bridge** | Bidirectional UART ↔ MQTT forwarding |
| **DMA UART** | Non-blocking circular buffer (512B RX/TX) |
| **Auto-Reconnect** | Handles network drops gracefully |
| **Fast Startup** | Optimized AT sequence (~5-10s boot) |
| **Debug Logging** | Optional UART1 debug output |

## Hardware

| STM32 Pin | Function | Connection |
|-----------|----------|------------|
| PA9 | UART1 TX | FC RX (Telemetry) |
| PA10 | UART1 RX | FC TX (Telemetry) |
| PA2 | UART2 TX | A7600C RX |
| PA3 | UART2 RX | A7600C TX |
| PC13 | LED | Status (optional) |
| PB11 | Power Key | A7600 PWR (active high) |
| PA15 | GPIO | A7600 Control (optional) |

## MQTT Topics

| Topic | Direction | Description |
|-------|-----------|-------------|
| `uav4g/mavlink/tx` | UAV → Cloud | MAVLink from FC, Hex-encoded |
| `uav4g/mavlink/rx` | Cloud → UAV | MAVLink to FC, Hex-decoded |
| `uav4g/status` | UAV → Cloud | Online/Offline heartbeat |

## Configuration

Edit `Core/Inc/app.h`:

```c
#define APP_MQTT_BROKER     "your-broker.hivemq.cloud"
#define APP_MQTT_PORT       8883    // SSL
#define APP_MQTT_USERNAME   "user"
#define APP_MQTT_PASSWORD   "pass"
#define APP_MQTT_CLIENT_ID  "stm32_uav4g"
```

Edit `Core/Inc/mavlink_bridge.h` for topic names:

```c
#define BRIDGE_TOPIC_TX "uav4g/mavlink/tx"
#define BRIDGE_TOPIC_RX "uav4g/mavlink/rx"
```

## Project Structure

```
Core/
├── Inc/
│   ├── app.h            # Application config (broker, topics, timing)
│   ├── a7600_mqtt.h     # MQTT driver API
│   ├── mavlink_bridge.h # Bridge config (topics)
│   ├── uart_dma.h       # DMA buffer sizes
│   └── debug_log.h      # Enable/disable debug
├── Src/
│   ├── main.c           # Init, main loop, interrupts
│   ├── app.c            # State machine, reconnect logic
│   ├── a7600_mqtt.c     # AT command sequences (10 steps)
│   ├── mavlink_bridge.c # UART → MQTT & MQTT → UART
│   ├── uart_dma.c       # DMA circular buffer driver
│   └── debug_log.c      # Debug UART output
MDK-ARM/
└── test_a7600.uvprojx   # Keil uVision project
```

## Connection Sequence

`A7600_MQTT_Connect()` executes these steps:

1. **AT** - Module ready check
2. **AT+CPIN?** - SIM card status
3. **AT+CREG?** - Network registration
4. **AT+CGREG?** - GPRS/LTE registration
5. **AT+CGACT** - PDP context activation
6. **AT+CSQ** - Signal quality (info only)
7. **AT+CMQTTSTART** - Start MQTT service
8. **AT+CMQTTACCQ** - Acquire MQTT client
9. **SSL Config** - TLS 1.2, SNI, No Verify
10. **AT+CMQTTCONNECT** - Connect to broker

## Building

1. Open `MDK-ARM/test_a7600.uvprojx` in **Keil uVision5**
2. Build (`F7`)
3. Flash to STM32F0

## Debug

Enable in `Core/Inc/debug_log.h`:

```c
#define DEBUG_ENABLE  // Uncomment to enable
```

View logs on UART1 @ 115200 baud.

## Error Troubleshooting

Use `A7600_MQTT_GetErrorStep()` to identify failure point:

| Step | Failure | Check |
|------|---------|-------|
| 1 | Module | Power, UART2 wiring |
| 2 | SIM | SIM inserted, PIN disabled |
| 3-4 | Network | Antenna, coverage |
| 5 | PDP | APN settings |
| 7-8 | MQTT | Broker reachable |
| 10 | Connect | Credentials, SSL |

## License

MIT
