# Modem RNS ESP-NOW

KISS modem firmware for ESP32, bridging [Reticulum Network Stack](https://github.com/markqvist/Reticulum) (RNS/NomadNet) to ESP-NOW wireless.

## What is this

Firmware that turns cheap ESP32 boards into wireless KISS modems using ESP-NOW protocol. Two ESP32 devices run this firmware and transparently pass RNS traffic between each other over ESP-NOW.

## Why ESP-NOW

ESP-NOW is a lightweight, connectionless protocol from Espressif that works on ESP32 and ESP8266. These microcontrollers cost $2-5 and are widely available.

ESP-NOW fills the gap between:

- **WiFi** — too short range for many use cases
- **LoRa** — long range but expensive modules and low bandwidth
- **Wi-Fi HaLow** — expensive, limited availability

Use case: short to medium distance links where WiFi doesn't reach, but you don't need (or want to pay for) LoRa.

## Current state

- ESP32 firmware working with RNS over KISS
- ESP-NOW channel 13, broadcast mode, no encryption
- Fragmentation/reassembly for frames up to 500 bytes
- CRC16 transport integrity check
- Long Range mode + configurable PHY rate
- No guarantees of support or maintenance — sharing my work for anyone interested

## Future plans

- Backward compatibility between ESP-NOW on ESP32 and ESP8266
