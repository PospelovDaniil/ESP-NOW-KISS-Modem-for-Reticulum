# Modem RNS ESP-NOW

KISS modem firmware for ESP32 and ESP8266, bridging [Reticulum Network Stack](https://github.com/markqvist/Reticulum) (RNS/NomadNet) to ESP-NOW wireless.

## What is this

Firmware that turns cheap ESP32/ESP8266 boards into wireless KISS modems using ESP-NOW protocol. Two devices run this firmware and transparently pass RNS traffic between each other over ESP-NOW — including cross-chip communication (ESP32 ↔ ESP8266).

## Why ESP-NOW

ESP-NOW is a lightweight, connectionless protocol from Espressif that works on ESP32 and ESP8266. These microcontrollers cost $2-5 and are widely available.

ESP-NOW fills the gap between:

- **WiFi** — too short range for many use cases
- **LoRa** — long range but expensive modules and low bandwidth
- **Wi-Fi HaLow** — expensive, limited availability

Use case: short to medium distance links where WiFi doesn't reach, but you don't need (or want to pay for) LoRa.

## Development environment

**ESP32:**
- [ESP-IDF v6.0.2](https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32/get-started/index.html)
- VS Code extension: [ESP-IDF](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension)

**ESP8266:**
- [ESP8266 RTOS SDK](https://github.com/espressif/ESP8266_RTOS_SDK)
- VS Code extension: [ESP8266-IDF](https://marketplace.visualstudio.com/items?itemName=Dzantemir.esp8266-idf)

## RNS config example

```ini
[[KISS ESP-NOW]]
    type = KISSInterface

    enabled = yes

    port = /dev/ttyUSB0
    speed = 57600
    databits = 8
    parity = none
    stopbits = 1
    preamble = 1
    txtail = 1
    persistence = 1
    slottime = 1
    flow_control = false
```

## Hardware

Tested on ESP32 DevKit and ESP8266 boards. ESP32 firmware should also build for ESP32-S3, ESP32-C3 and other ESP32 variants.

## Configuration

Radio parameters (PHY rate, Long Range mode) are hardcoded in `radio.cpp`. Default rate is `WIFI_PHY_RATE_1M_L` for cross-chip ESP-NOW compatibility. Changing the rate requires editing the source and reflashing.

## Current state

- ESP32 and ESP8266 firmware working with RNS over KISS
- Cross-chip ESP-NOW: ESP32 ↔ ESP8266 compatible
- ESP-NOW channel 13, broadcast mode, no encryption
- Fragmentation/reassembly for frames up to 500 bytes
- CRC16 transport integrity check
- Long Range mode + configurable PHY rate (ESP32)
- No guarantees of support or maintenance — sharing my work for anyone interested

## Future plans

- `ESPNowInterface` for RNS — similar to `RNodeInterface`, but for ESP-NOW, allowing convenient configuration of radio parameters at runtime
