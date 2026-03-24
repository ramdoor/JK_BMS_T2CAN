#pragma once

// Lilygo T-2CAN (ESP32-S3) pin definitions
// CAN Bus 1 — TWAI (ESP32-S3 built-in) — BMS/sensor @ 250 kbps
#define CAN_TX 7
#define CAN_RX 6

// No ME2107_EN or CAN_SPEED_MODE on T-2CAN (transceiver always enabled)

// Contactor GPIO assignments
#define GPIO_MAIN_CONTACTOR      0   // UNUSED
#define GPIO_PRECHARGE_CONTACTOR 14
#define GPIO_CHG_CONTACTOR       15
#define GPIO_DSG_CONTACTOR       16

// MCP2515 (second CAN bus for inverter) — default SPI bus
// Only enabled when MCP2515_CS is defined via build flags (t2can environment)
#ifndef MCP2515_CS
// Defaults commented out; defined in platformio.ini for t2can board
// #define MCP2515_CS    10
// #define MCP2515_SCK   12
// #define MCP2515_MOSI  11
// #define MCP2515_MISO  13
// #define MCP2515_RST   9
#endif

// Output polarity
// If your relay/driver board is ACTIVE-LOW, set to 1.
#define CONTACTOR_ACTIVE_LOW 0
