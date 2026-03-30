# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

This is a PlatformIO ESP32 project. Use these commands from the project root:

```bash
# Build production firmware
pio run -e esp32_production

# Build debug firmware (verbose logging, no optimization)
pio run -e esp32_debug

# Build OTA-enabled firmware
pio run -e esp32_ota

# Build for T-CAN485 board specifically
pio run -e tcan485

# Upload to connected ESP32
pio run -e esp32_production --target upload

# Monitor serial output (115200 baud)
pio device monitor

# Static analysis / linting
pio check --skip-packages

# Run tests (test framework not yet implemented)
pio test -e esp32_test
```

## Architecture Overview

### System Purpose
ESP32-based Battery Management System controller that aggregates multiple JK BMS modules via CAN bus, controls contactors, calculates SoC/SoH, and communicates with inverters using various protocols (BYD HVS, Pylon, SMA/Ingeteam 0x35x). Uses dual CAN architecture: TWAI (250 kbps) for BMS/sensor, MCP2515 (500 kbps) for inverter.

### Core Components (all in `src/main.cpp`)

**Data Structures:**
- `Module` - Holds per-BMS-module data (up to 10 modules, 24 cells each)
- `Limits` - Protection thresholds (voltage, temperature, delta, timeouts, expected cell count)
- `EnergyCfg` - SoC/SoH configuration and auto-learning parameters

**State Machine (`SysState`):**
- `ST_OPEN` → `ST_PRECHARGING` → `ST_CLOSED` → `ST_TRIPPED`
- Transitions controlled by `controlTick()` based on armed state and faults

**Key Functions:**
- `canTask()` - FreeRTOS task on core 1; polls JK modules, reads AHBC current sensor, handles CAN RX/TX
- `controlTick()` - Main control loop: evaluates faults, computes permissions, runs learning, manages contactors
- `integrateCurrent()` - Coulomb counting for SoC calculation
- `learningTick()` - Auto-learns battery capacity from full-to-empty discharge cycles
- `computePermissions()` - Sets `allowCharge`/`allowDischarge` with hysteresis
- `inverter_tx_tick()` / `inverter_rx()` - Protocol handlers for BYD HVS, Pylon, and SMA/Ingeteam inverters (via MCP2515)
- `canSendMcp()` - Sends CAN frames via MCP2515 (second bus, inverter)

**Web Interface:**
- Status page (`/`) - Real-time monitoring with AJAX polling, balance health badge, charts with range selector
- Config page (`/config`) - Admin-only settings (limits, energy, tapering, night mode, MQTT, inverter)
- API endpoints (`/api/status`, `/api/rev`, `/api/history`, `/api/events`) - JSON data for live updates
- Event log - 200 persistent entries in LittleFS with CSV export

### Hardware Configuration

Pin definitions in `include/pin_config.h` (defaults in build flags for tcan485):
- CAN1 (TWAI): TX=27, RX=26, EN=16, SPEED_MODE=23 (T-CAN485 board, 250 kbps)
- CAN2 (MCP2515 SPI): CS=5, SCK=18, MOSI=12, MISO=35, INT=34 (500 kbps)
- Contactors: PRECHARGE=25, CHARGE=32, DISCHARGE=33 (MAIN=0 unused)

### Dual CAN Architecture

**CAN Bus 1 - TWAI (ESP32 built-in) - 250 kbps:**
- JK BMS modules (IDs 1-10): Polled every 400ms, respond with cell voltages, pack voltage, temperature
- AHBC-CANB current sensor (IDs 0x3C2, 0x3C3): Bidirectional current measurement

**CAN Bus 2 - MCP2515 (SPI) - 500 kbps:**
- Inverter protocols: SMA/Ingeteam (0x351-0x35E), BYD HVS (0x110/0x150/0x1D0/0x190/0x210), Pylon (0x4200/0x4210-0x4290)
- All inverter TX uses `canSendMcp()`, RX via interrupt on GPIO 34
- MCP2515 status (OK/FAIL), TX/RX/error counters visible in web UI and `/api/status`

### Protection Logic

Faults are evaluated in `evaluateFaults()`:
- Cell OV/UV with configurable hysteresis (default 20mV)
- Pack OV/UV (optional, 0=disabled)
- Delta voltage (max cell - min cell)
- Temperature range
- Communication timeout
- Expected cell count mismatch (optional, 0=disabled) — critical fault, also blocks arming

Directional permissions (`allowCharge`/`allowDischarge`) use hysteresis to prevent oscillation.

### Tapering and Ratchet

CCL/DCL are gradually reduced near OV/UV thresholds (tapering). The ratchet logic ensures CCL/DCL can drop instantly but only rises slowly (configurable %/s + hold time at zero). Three levels: taper start → soft stop (CCL/DCL=0) → hard fault.

### Soft Disconnect

Before opening contactors on fault, the system sends CCL/DCL=0 to the inverter for 5 seconds (`SOFT_DISCONNECT_TIMEOUT_MS`), allowing the inverter to ramp down. Cancels automatically if the fault condition clears.

### Night Mode

Configurable CCL/DCL reduction during specified hours (e.g., 23:00-07:00) for tariff optimization. Parameters: `nightStartHour`, `nightEndHour`, `nightCclPct`, `nightDclPct`. Persisted in NVS.

### Device Name

User-configurable device name (`deviceName`, max 32 chars). Set via `/config` page (MQTT section). Stored in NVS (`devName`). Displayed in the `<h1>` header of the status page (replaces default "JK BMS + Control"). Included in MQTT telemetry as `"name"` field, auto-synced to cloud portal.

### 24-Hour History Buffer

Two ring buffers for chart data:
- **5s buffer:** 720 points (~1 hour), used for ranges <= 1h
- **2min buffer:** 720 points (~24 hours), used for ranges > 1h
- Range selector: 5min / 15min / 30min / 1h / 6h / 12h / 24h
- Auto-selects buffer and downsamples to max 180 points

### MQTT Integration

PubSubClient with optional TLS. Topics: `bms/{id}/telemetry` (5s), `bms/{id}/event` (on-event), `bms/{id}/cmd/*` (config/action/ota). Home Assistant auto-discovery. Settings configurable from `/config` page and persisted in NVS. Zero impact when not configured.

### Cloud Connection

Default MQTT settings (T2CAN) point to cloud server `91.98.122.117:1883` with user `bmsdevice`. One-time NVS migration clears old local settings on first boot. Telemetry includes `name` field for auto-sync of device name to cloud portal.

### OTA Updates

HTTPS OTA triggered via MQTT `cmd/ota` topic with URL + MD5. Refuses update if contactors are closed. Progress reported via MQTT ACK.

### SoH and Capacity Auto-Learning

The system auto-learns the real battery capacity (`sohAh`) by measuring a complete full-to-empty discharge cycle. This is managed by `learningTick()` and `integrateCurrent()`.

**Configuration (`EnergyCfg`):**
- `capacityAh` (200 Ah) — Nominal capacity, used as reference for SoC and SoH percentage
- `sohAh` (200 Ah) — Learned/editable actual capacity; updated automatically by the learning algorithm
- `fullCellV` (3.55 V) — Maximum cell voltage threshold to consider the pack fully charged (LFP)
- `emptyCellV` (2.95 V) — Maximum cell voltage to consider the pack fully discharged (LFP)
- `tailCurrentA` (2.0 A) — Current must be below this threshold for full/empty detection
- `holdMs` (120000 ms) — Full/empty condition must remain stable for this duration

**Learning state machine (`LearnPhase`):**
1. `LEARN_IDLE` — Waiting for a FULL event
2. `LEARN_FROM_FULL` — Counting discharge Ah until an EMPTY event

**Algorithm flow:**
1. **FULL detection:** When `packMaxCellV >= fullCellV`, absolute current is below `tailCurrentA`, and charge is allowed, a timer starts. After `holdMs` of stable conditions, `applyFullCalibration()` resets SoC to 100%, sets `fullConfirmed = true`, `learnPhase` transitions to `LEARN_FROM_FULL`, and `learnDischargeAh` resets to 0.
2. **Discharge counting:** While in `LEARN_FROM_FULL`, `integrateCurrent()` accumulates negative current (discharge) into `learnDischargeAh`: `learnDischargeAh += |I_A| × dt_h` (only when `I_A < 0`).
3. **EMPTY detection:** When `packMinCellV <= emptyCellV`, current is below tail, and discharge is allowed, another timer starts. After `holdMs` stable, `applyEmptyCalibration()` resets SoC to 0%.
4. **Capacity update:** The accumulated `learnDischargeAh` is clamped to 10%–150% of `capacityAh` and stored as `ecfg.sohAh`. The result is persisted to NVS via `saveConfig()`.

**SoH percentage:** Displayed as `(sohAh / capacityAh) × 100%`. A value below 100% indicates degraded capacity; above 100% means the nominal value was conservative.

**Persistence:** `learnPhase` and `learnDischargeAh` are saved to NVS (`lph`, `ldAh`), so learning survives reboots.

### Persistence

Uses ESP32 Preferences (NVS) for:
- Configuration (`jkcfg` namespace): limits, energy config, language, inverter settings, expected cell count (`expCells`), night mode, MQTT settings, tapering, device name (`devName`)
- Energy state: SoC, coulomb balance, throughput, cycle count, learning phase

Uses LittleFS for:
- Event log (`/event_log.jsonl`) - 200 entries, JSONL format
- MQTT certificates (`mqtt_ca.pem`, `mqtt_cert.pem`, `mqtt_key.pem`)
- MQTT fallback config (`data/mqtt_config.json`)

### NTP Time Sync

On boot (after WiFi), the system calls `configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", ...)` for NTP synchronization. Event log timestamps use real time (epoch) when available, falling back to `millis()/1000` (uptime) before NTP sync.

## Build Environments

| Environment | Purpose | Key Flags |
|-------------|---------|-----------|
| `esp32_production` | Release build | `-O2`, `PRODUCTION_BUILD` |
| `esp32_debug` | Debug with logging | `-O0 -g3`, `CORE_DEBUG_LEVEL=5` |
| `esp32_ota` | OTA-capable release | `OTA_ENABLED` |
| `esp32_test` | Unit testing | `UNIT_TEST`, ArduinoUnit |
| `tcan485` | T-CAN485 board specific | Board-specific pins, MCP2515, `autowp-mcp2515` lib |
| `native` | Host simulation | GoogleTest, C++17 |

## Important Constants

Compile-time constants (can be overridden in `platformio.ini`):
- `CONTACTOR_MIN_TOGGLE_MS` (1500ms) - Minimum time between contactor state changes
- `CELL_OV_HYST` / `CELL_UV_HYST` (0.020V) - Voltage hysteresis for permissions

## WiFi/Auth

Credentials are hardcoded in `main.cpp`:
- WiFi: Lines 22-23
- Auth: Lines 26-29 (admin/user accounts)

## Third-Party Credits

Inverter protocol implementations inspired by [dalathegreat/Battery-Emulator](https://github.com/dalathegreat/Battery-Emulator) (GPL-3.0).
