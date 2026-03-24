# JK BMS T-2CAN Controller

![Status](https://img.shields.io/badge/status-in_development-yellow)
![Platform](https://img.shields.io/badge/platform-ESP32--S3-blue)
![Board](https://img.shields.io/badge/board-Lilygo_T--2CAN-orange)
![License](https://img.shields.io/badge/license-GPL--3.0-green)

A professional Battery Management System controller for aggregating multiple JK BMS modules with advanced SoC/SoH tracking, contactor control, and multi-protocol inverter communication. **This version is adapted for the Lilygo T-2CAN board (ESP32-S3).**

> Based on [JK_BMS_Master](https://github.com/ramdoor/JK_BMS_Master) (T-CAN485/ESP32 version).

---

## Features

### Implemented & Working
- **Multi-Module Support**: Manage up to 10 JK BMS modules simultaneously
- **Intelligent Contactor Control**: Automated precharge sequencing and directional control
- **Soft Disconnect**: 5-second CCL/DCL=0 ramp-down before opening contactors (arc prevention)
- **Current Monitoring**: AHBC-CANB sensor integration via CAN bus
- **Advanced Protection**: Cell/pack voltage, temperature, delta voltage, communication fault, and expected cell count validation
- **Tapering & Ratchet**: Gradual CCL/DCL reduction near limits with instant-drop/slow-rise logic
- **NTP Timestamps**: Real-time event log with NTP synchronization (CET/CEST), uptime fallback
- **Web Interface**: Real-time monitoring with multi-language support (ES/EN/DE), dark theme, responsive
- **Configuration Management**: Persistent settings with chemistry presets (LFP/NMC/LTO)
- **Inverter Protocols**: BYD HVS (verified), Pylontech Force H2 HV (29-bit CAN), Pylon LV, SMA/Ingeteam (0x35x, verified)
- **Dual CAN Bus**: TWAI 250 kbps (BMS/sensor) + MCP2515 500 kbps (inverter)
- **SoC Calculation**: Coulomb counting with configurable efficiency, SoC cap at 99% until full confirmed
- **SoH Auto-Learning**: Automatic capacity learning from full charge/discharge cycles
- **MQTT Telemetry**: TLS/plain, Home Assistant auto-discovery, remote commands (arm/disarm/config/OTA)
- **OTA Updates**: HTTPS firmware update via MQTT command with MD5 verification
- **Night Mode**: Configurable CCL/DCL reduction during off-peak hours (tariff optimization)
- **Event Log**: 200 persistent events in LittleFS, survives reboots, CSV export
- **History Charts**: 720-point server buffer (1 hour at 5s), range selector (5/15/30/60 min)
- **Balance Health Indicator**: Color badge (Excellent/Good/Fair/Poor/Critical) with weak cell warning
- **Cloud Integration**: Grafana/InfluxDB via Telegraf MQTT bridge (documented stack)

### Planned Features
- Additional inverter protocols (Sofar, SolaX, Sungrow, Growatt, FoxESS)
- Automated testing framework
- Code modularization (split main.cpp)

---

## Hardware: Lilygo T-2CAN

| Component | Specification | Notes |
|-----------|--------------|-------|
| **MCU** | ESP32-S3 (16MB Flash, 8MB PSRAM) | Lilygo T-2CAN board |
| **CAN Bus 1** | TWAI (built-in), 250 kbps | BMS modules + current sensor |
| **CAN Bus 2** | MCP2515 (SPI), 500 kbps, 16 MHz crystal | Inverter communication |
| **BMS Modules** | JK BMS (1-10 units) | CAN-enabled models |
| **Current Sensor** | AHBC-CANB | Bidirectional, CAN output |
| **Contactors/Relays** | 3x high-current relays | Precharge, Charge, Discharge |
| **Serial** | USB-C (CDC) | No external USB-UART needed |

### Pin Configuration (T-2CAN)
```
CAN Bus 1 (TWAI - BMS/Sensor, 250 kbps):
  TX: GPIO 7
  RX: GPIO 6

CAN Bus 2 (MCP2515 - Inverter, 500 kbps, 16 MHz crystal):
  CS:   GPIO 10  (SPI Chip Select)
  SCK:  GPIO 12  (SPI Clock)
  MOSI: GPIO 11  (SPI Master Out)
  MISO: GPIO 13  (SPI Master In)
  RST:  GPIO 9   (Hardware Reset)

Contactors:
  PRECHARGE: GPIO 14
  CHARGE:    GPIO 15
  DISCHARGE: GPIO 16
  MAIN:      GPIO 0 (reserved, not used)
```

### Key Differences from T-CAN485 Version

| Aspect | T-CAN485 (ESP32) | T-2CAN (ESP32-S3) |
|--------|-------------------|---------------------|
| MCU | ESP32 | ESP32-S3 (16MB flash, 8MB PSRAM) |
| TWAI pins | TX=27, RX=26 | TX=7, RX=6 |
| Transceiver control | ME2107_EN, SPEED_MODE | Not needed (always enabled) |
| MCP2515 SPI bus | HSPI | Default SPI |
| MCP2515 crystal | 8 MHz | 16 MHz |
| MCP2515 RX method | INT pin polling | `checkReceive()` SPI polling |
| MCP2515 RST | Not wired | GPIO 9 (hardware reset) |
| Contactor GPIOs | 25, 32, 33 | 14, 15, 16 |
| Serial | UART (CH340/CP2102) | USB CDC (native USB-C) |

---

## Installation

### Prerequisites
- **PlatformIO** (recommended)
- **USB-C cable** for programming
- **Libraries** (auto-installed by PlatformIO):
  - WiFi, WebServer, Preferences, SPI (built-in)
  - ESP32-S3 CAN (TWAI driver, built-in)
  - LittleFS (built-in)
  - PubSubClient (MQTT)
  - ArduinoJson v7
  - autowp-mcp2515

### Quick Start

1. **Clone the repository:**
   ```bash
   git clone https://github.com/ramdoor/JK_BMS_T2CAN.git
   cd JK_BMS_T2CAN
   ```

2. **Configure WiFi credentials** in `src/main.cpp` (lines 26-27):
   ```cpp
   const char* ssid = "YourSSID";
   const char* password = "YourPassword";
   ```

3. **Build and upload:**
   ```bash
   pio run -e t2can --target upload
   ```

4. **Access web interface:**
   - Open serial monitor (`pio device monitor`)
   - Find IP address in output
   - Navigate to `http://<ESP32_IP>` in browser
   - Login: `admin` / `Renovables!`

---

## Build Environments

| Environment | Purpose | Key Flags |
|-------------|---------|-----------|
| `t2can` | Production build | `-O2`, `PRODUCTION_BUILD`, `BOARD_T2CAN` |
| `t2can_debug` | Debug with logging | `-O0 -g3`, `CORE_DEBUG_LEVEL=5` |
| `t2can_ota` | OTA-capable release | `OTA_ENABLED` |

```bash
# Build production firmware
pio run -e t2can

# Build debug firmware
pio run -e t2can_debug

# Upload to T-2CAN board
pio run -e t2can --target upload

# Monitor serial output (USB CDC)
pio device monitor
```

---

## Documentation

- [CLAUDE.md](CLAUDE.md) - Architecture and development guide
- [Functional Specification Document (FSD)](docs/FSD.md) - Complete functional requirements
- [Hardware Setup Guide](docs/HARDWARE.md) - Wiring and installation
- [Web UI Guide](docs/WEB_UI_GUIDE.md) - Interface documentation (EN/ES/DE)
- [Cloud Integration](docs/CLOUD_INTEGRATION.md) - MQTT, InfluxDB, Grafana, Home Assistant
- [Troubleshooting](docs/TROUBLESHOOTING.md) - Common issues and solutions
- [Quick Start](QUICKSTART.md) - Get running in 15 minutes

---

## Inverter Protocols

| Protocol | Status | CAN IDs |
|----------|--------|---------|
| SMA/Ingeteam/Victron (0x35x) | Verified | 0x351-0x35E |
| BYD HVS | Verified (Ingeteam 6K) | 0x110, 0x150, 0x1D0, 0x210, 0x190 |
| Pylontech Force H2 HV | Implemented | 0x4210-0x4290 (29-bit) |
| Pylon LV | Implemented | 0x35x (SMA-compatible) |
| Sofar, SolaX, Sungrow, Growatt, FoxESS | Planned | - |

Protocol implementations inspired by [dalathegreat/Battery-Emulator](https://github.com/dalathegreat/Battery-Emulator) (GPL-3.0).

---

## Contributing

Contributions welcome! Please fork, create a feature branch, and submit a Pull Request.

---

## License

GNU General Public License v3.0 - see [LICENSE](LICENSE) file.

---

## Support

- **GitHub Issues**: [Create an issue](https://github.com/ramdoor/JK_BMS_T2CAN/issues)
- **Original project**: [JK_BMS_Master](https://github.com/ramdoor/JK_BMS_Master)

---

**Safety Notice**: This system controls high-voltage battery equipment. Always follow proper safety procedures. The authors are not responsible for any damage or injury resulting from the use of this software.
