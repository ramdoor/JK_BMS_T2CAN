# Functional Specification Document (FSD)
# JK BMS Master Controller

**Version:** 1.1
**Date:** March 24, 2026
**Status:** In Development  
**Platform:** ESP32 (T-CAN485)

---

## 1. Executive Summary

The JK BMS Master Controller is a comprehensive Battery Management System designed to manage multiple JK BMS modules, control contactors, monitor current/voltage, calculate State of Charge (SoC) and State of Health (SoH), and communicate with various inverter brands using standardized CAN protocols.

### Key Features
- Multi-module JK BMS aggregation (up to 10 modules)
- Intelligent contactor control with precharge sequencing
- Real-time SoC/SoH calculation with auto-learning
- Multi-protocol inverter support (BYD HVS, Pylon HV/LV, and more)
- Web-based configuration interface (multi-language)
- AHBC-CANB current sensor integration
- Safety fault detection and trip mechanisms

---

## 2. System Architecture

### 2.1 Hardware Components

| Component | Model/Type | Purpose | Status |
|-----------|-----------|---------|--------|
| **MCU** | ESP32 (T-CAN485) | Main controller | ✅ Working |
| **CAN1 Transceiver** | T-CAN485 built-in (TWAI) | CAN bus 1: BMS + sensor (250 kbps) | ✅ Working |
| **CAN2 Controller** | MCP2515 (SPI, 8 MHz) | CAN bus 2: Inverter (500 kbps) | ✅ Working |
| **Current Sensor** | AHBC-CANB | Bidirectional current measurement | ✅ Working |
| **BMS Modules** | JK BMS (up to 10) | Cell monitoring | ✅ Working |
| **Contactors** | 4x relay outputs | Precharge, Charge, Discharge control | ✅ Working |

### 2.2 Pin Configuration (T-CAN485)

```
CAN Bus 1 (TWAI - BMS/Sensor, 250 kbps):
  CAN_TX: GPIO 27
  CAN_RX: GPIO 26
  ME2107_EN: GPIO 16 (CAN transceiver enable)
  CAN_SPEED_MODE: GPIO 23 (LOW = 250 kbps)

CAN Bus 2 (MCP2515 - Inverter, 500 kbps):
  MCP2515_CS:   GPIO 5  (SPI Chip Select)
  MCP2515_SCK:  GPIO 18 (SPI Clock, HSPI)
  MCP2515_MOSI: GPIO 12 (SPI Master Out, HSPI)
  MCP2515_MISO: GPIO 35 (SPI Master In, input-only)
  MCP2515_INT:  GPIO 34 (Interrupt, input-only)

Contactors:
- PRECHARGE: GPIO 25
- CHARGE: GPIO 32
- DISCHARGE: GPIO 33
- MAIN: GPIO 0 (currently unused)
```

### 2.3 Software Architecture

```
┌─────────────────────────────────────────┐
│         Web Interface (HTTP)            │
│  - Status Dashboard (multi-language)    │
│  - Configuration Panel                  │
│  - Real-time Cell Monitoring            │
└─────────────────┬───────────────────────┘
                  │
┌─────────────────▼───────────────────────┐
│      Main Controller (ESP32)            │
│  ┌─────────────────────────────────┐    │
│  │  State Machine                   │    │
│  │  - OPEN / PRECHARGING           │    │
│  │  - CLOSED / TRIPPED              │    │
│  └─────────────────────────────────┘    │
│  ┌─────────────────────────────────┐    │
│  │  Protection Logic                │    │
│  │  - Over/Under Voltage            │    │
│  │  - Temperature                   │    │
│  │  - Delta Voltage                 │    │
│  │  - Communication Timeout         │    │
│  └─────────────────────────────────┘    │
│  ┌─────────────────────────────────┐    │
│  │  Energy Management               │    │
│  │  - SoC Calculation (Coulomb)     │    │
│  │  - SoH Auto-learning             │    │
│  │  - Cycle Counting                │    │
│  └─────────────────────────────────┘    │
└──────┬──────────────────────┬───────────┘
       │                      │
       ▼                      ▼
┌─────────────┐      ┌──────────────────┐
│  CAN Bus 1  │      │  CAN Bus 2       │
│  TWAI       │      │  MCP2515 (SPI)   │
│  (250 kbps) │      │  (500 kbps)      │
│             │      │                  │
│  - JK BMS   │      │  - Ingeteam ✅    │
│  - AHBC     │      │  - BYD HVS       │
└─────────────┘      │  - Pylon HV/LV   │
                     │  - Sofar (TODO)  │
                     │  - SolaX (TODO)  │
                     └──────────────────┘
```

---

## 3. Functional Requirements

### 3.1 BMS Module Management

**FR-BMS-001**: The system SHALL support up to 10 JK BMS modules simultaneously.

**FR-BMS-002**: The system SHALL aggregate data from all active modules:
- Individual cell voltages (up to 24 cells per module)
- Pack voltage
- Temperature
- Module status

**FR-BMS-003**: The system SHALL detect module communication timeout (configurable, default 3000ms) and raise a fault.

**FR-BMS-004**: The system SHALL calculate pack-level aggregated values:
- Total pack voltage
- Minimum cell voltage (across all modules)
- Maximum cell voltage (across all modules)
- Delta voltage (max - min)
- Average temperature

**Status**: ✅ **Implemented and Working**

---

### 3.2 Current Measurement

**FR-CURR-001**: The system SHALL read current measurements from AHBC-CANB sensor via CAN.

**FR-CURR-002**: Current sensor CAN IDs:
- ID 0x3C2: Current value (format TBD)
- ID 0x3C3: Additional data (format TBD)

**FR-CURR-003**: The system SHALL support configurable current polarity (+1 or -1) for charge/discharge direction.

**FR-CURR-004**: The system SHALL detect current sensor timeout (default 3000ms).

**Status**: ✅ **Implemented and Working**

---

### 3.3 Protection Logic

#### 3.3.1 Voltage Protection

**FR-PROT-001**: The system SHALL implement configurable cell overvoltage protection with hysteresis.
- Default OV threshold: 3.65V (LFP)
- Hysteresis: 0.020V (compile-time constant)

**FR-PROT-002**: The system SHALL implement configurable cell undervoltage protection with hysteresis.
- Default UV threshold: 2.80V (LFP)
- Hysteresis: 0.020V (compile-time constant)

**FR-PROT-003**: The system SHALL support optional pack-level overvoltage/undervoltage limits (0 = disabled).

**FR-PROT-004**: The system SHALL implement delta voltage protection (max cell - min cell).
- Default: 0.05V

#### 3.3.2 Temperature Protection

**FR-PROT-005**: The system SHALL implement configurable temperature limits:
- Minimum: 0°C (default)
- Maximum: 55°C (default)

#### 3.3.3 Directional Permission Logic

**FR-PROT-006**: The system SHALL implement directional permissions:
- **allowCharge**: TRUE if no overvoltage or critical faults
- **allowDischarge**: TRUE if no undervoltage or critical faults

**FR-PROT-007**: Permissions SHALL use hysteresis to prevent oscillation:
- Once a fault trips a direction, conditions must improve by the hysteresis margin before re-enabling

**FR-PROT-008**: The system SHALL distinguish between:
- **Critical Fault**: Requires manual reset (TRIPPED state)
- **Directional Fault**: Automatic recovery when conditions improve

#### 3.3.4 Cell Count Validation

**FR-PROT-009**: The system SHALL support optional expected cell count validation.
- Default: 0 (disabled)
- When configured (`expectedCells > 0`): validates total active cell count matches expected value

**FR-PROT-010**: Cell count mismatch SHALL trigger:
- Critical fault (opens contactors, enters TRIPPED state)
- Blocks arming (`handleActions()` rejects ARM request with error message)
- Event log entry with anti-spam (max once per 30s)

#### 3.3.5 NTP Time Synchronization

**FR-TIME-001**: The system SHALL synchronize time via NTP after WiFi connection.
- Timezone: CET-1CEST (Central European Time with DST)
- Servers: pool.ntp.org, time.nist.gov
- Event log timestamps use epoch time when NTP is available, uptime seconds as fallback

**Status**: ✅ **Implemented and Working**

---

### 3.4 Contactor Control

**FR-CONT-001**: The system SHALL control 4 contactors:
- Precharge contactor
- Main contactor (currently unused in logic)
- Charge contactor
- Discharge contactor

**FR-CONT-002**: Contactor switching sequence:

**ARMING (OPEN → PRECHARGING → CLOSED):**
```
1. Close PRECHARGE contactor
2. Wait precharge time (default 2000ms)
3. Close CHARGE + DISCHARGE contactors
4. Open PRECHARGE contactor
5. State = CLOSED
```

**DISARMING (CLOSED → OPEN):**
```
1. Open CHARGE contactor
2. Open DISCHARGE contactor
3. Ensure PRECHARGE is open
4. State = OPEN
```

**FR-CONT-003**: The system SHALL enforce minimum toggle time between contactor state changes (default 1500ms).

**FR-CONT-004**: Contactors SHALL open immediately on critical fault (TRIPPED state).

**FR-CONT-005**: Directional faults SHALL open only the affected contactor:
- OV fault → open CHARGE contactor only
- UV fault → open DISCHARGE contactor only

**Status**: ✅ **Implemented and Working**

---

### 3.5 State of Charge (SoC) Calculation

**FR-SOC-001**: The system SHALL calculate SoC using coulomb counting (current integration).

**FR-SOC-002**: SoC calculation parameters:
- Nominal capacity (Ah) - configurable
- Coulombic efficiency - configurable (default 0.995)
- Current polarity - configurable (+1 or -1)
- Initial SoC (%) - configurable for first boot

**FR-SOC-003**: The system SHALL persist SoC state to non-volatile memory (Preferences) at regular intervals.

**FR-SOC-004**: SoC range: 0% to 100%

**FR-SOC-005**: The system SHALL track:
- Current SoC (%)
- Coulomb balance (Ah remaining)
- Throughput (absolute Ah processed)
- Equivalent full cycles (EFC)

**Status**: 🔄 **Implemented - Under Testing**

---

### 3.6 State of Health (SoH) Auto-Learning

**FR-SOH-001**: The system SHALL implement automatic capacity learning to adjust SoH.

**FR-SOH-002**: Learning algorithm (`learningTick()` + `integrateCurrent()`):
1. **FULL detection**: `packMaxCellV >= fullCellV` AND `|current| < tailCurrentA` AND `allowCharge == true`, stable for `holdMs`. Triggers `applyFullCalibration()` which sets SoC=100%, `fullConfirmed=true`, and `coulombBalance_Ah = sohAh`.
2. **Begin learning phase**: `learnPhase` transitions from `LEARN_IDLE` to `LEARN_FROM_FULL`, `learnDischargeAh` resets to 0.
3. **Discharge counting**: While in `LEARN_FROM_FULL`, `integrateCurrent()` accumulates only negative current (discharge) into `learnDischargeAh`: `learnDischargeAh += |I_A| × dt_h` (where `dt_h` = elapsed time in hours).
4. **EMPTY detection**: `packMinCellV <= emptyCellV` AND `|current| < tailCurrentA` AND `allowDischarge == true`, stable for `holdMs`. Triggers `applyEmptyCalibration()` which sets SoC=0%.
5. **Capacity update**: `learnDischargeAh` is clamped to `[capacityAh × 0.10, capacityAh × 1.50]` and stored as `ecfg.sohAh`. Persisted to NVS via `saveConfig()`. Phase returns to `LEARN_IDLE`.
6. **SoH display**: Calculated as `(sohAh / capacityAh) × 100%` and transmitted to inverter protocols.
7. **SoC cap**: Until `fullConfirmed` is true, SoC sent to inverter is capped at 99% to prevent false full readings.

**FR-SOH-002a**: The learning state (`learnPhase`) and accumulated discharge counter (`learnDischargeAh`) are persisted to NVS keys `lph` and `ldAh` respectively, so learning survives reboots.

**FR-SOH-003**: Learning parameters (configurable):
- Full cell voltage (LFP: 3.55V, NMC: 4.15V)
- Empty cell voltage (LFP: 2.95V, NMC: 3.10V)
- Tail current threshold (2A default)
- Hold time (120 seconds default)

**FR-SOH-004**: SoH SHALL be editable manually via web interface.

**FR-SOH-005**: The system SHALL persist SoH to non-volatile memory.

**Status**: 🔄 **Implemented - Under Testing**

---

### 3.7 Inverter Communication Protocols

**FR-INV-001**: The system SHALL support multiple inverter protocols selectable via web UI.

**FR-INV-002**: Supported protocols:
- **OFF**: No inverter communication
- **SMA/Ingeteam (0x35x)**: SMA Sunny Island / Victron / Ingeteam compatible (verified working with Ingeteam 6K)
- **BYD HVS**: BYD Battery-Box HVS protocol
- **Pylon HV**: Pylon LFP high voltage protocol
- **Pylon LV**: Pylon LFP low voltage protocol

**FR-INV-002a**: All inverter communication uses a dedicated second CAN bus via MCP2515 at 500 kbps, physically separate from the BMS/sensor bus (TWAI at 250 kbps).

**FR-INV-003**: Planned future protocols (placeholders in code):
- Sofar
- SolaX
- Sungrow
- Growatt HV/LV
- FoxESS
- SMA Tripower

**FR-INV-004**: The system SHALL transmit to inverter:
- Pack voltage (0.1V resolution)
- Current (0.1A resolution)
- SoC (0.01% resolution)
- SoH (0.01% resolution)
- Max charge current (0.1A resolution)
- Max discharge current (0.1A resolution)
- Max charge voltage (0.1V resolution)
- Min discharge voltage (0.1V resolution)

**FR-INV-005**: Directional limits SHALL be applied:
- If !allowCharge → maxChargeA = 0
- If !allowDischarge → maxDischargeA = 0

#### 3.7.1 BYD HVS Protocol

**FR-INV-BYD-001**: Transmission schedule:
- 0x110 (limits): Every 2 seconds
- 0x150 (SoC/SoH/capacity): Every 10 seconds
- 0x1D0 (voltage/current/temp): Every 10 seconds
- 0x210 (temp min/max): Every 10 seconds
- 0x190 (alarms): Every 60 seconds

**FR-INV-BYD-002**: The system SHALL send initial CAN frames on startup detection.

**Status**: ✅ **Implemented and Working** (verified with Ingeteam 6K, uses MCP2515 bus)

#### 3.7.2 SMA/Ingeteam/Victron Protocol (0x35x)

**FR-INV-SMA-001**: Transmission at 1 Hz:
- 0x351 (charge/discharge voltage and current limits): Every 1 second
- 0x355 (SoC/SoH): Every 1 second
- 0x356 (voltage/current/temperature): Every 1 second
- 0x359 (alarms and warnings): Every 1 second
- 0x35C (charge/discharge enable flags): Every 1 second
- 0x35E (manufacturer name "INGETEAM"): Every 1 second

**FR-INV-SMA-002**: The system SHALL set charge/discharge flags in 0x35C based on `allowCharge`/`allowDischarge`.

**FR-INV-SMA-003**: Alarm mapping in 0x359:
- Bit 0: General alarm (critical fault)
- Bit 1: High voltage (OV)
- Bit 2: Low voltage (UV)
- Bit 3: High temperature
- Bit 4: Low temperature

**Status**: ✅ **Implemented and Working** (verified with Ingeteam 6K inverter)

#### 3.7.3 Pylon Protocol

**FR-INV-PYL-001**: The system SHALL respond to Pylon request frames:
- Request 0x02: Send battery info (0x7310, 0x7320)
- Request 0x00: Send telemetry data (0x4210-0x4290)

**FR-INV-PYL-002**: Bank index support: Currently hardcoded to bank 0.

**Status**: ⚠️ **Implemented - Not Tested**

---

### 3.8 Web User Interface

**FR-UI-001**: The system SHALL provide a web interface accessible via WiFi.

**FR-UI-002**: Multi-language support:
- Spanish (ES)
- English (EN)
- German (DE)

**FR-UI-003**: Status page SHALL display:
- System state (OPEN / PRECHARGING / CLOSED / TRIPPED)
- Armed status
- Active module IDs
- Pack summary (voltage, min/max cells, delta, temp)
- Individual cell voltages (grouped by module)
- SoC, SoH, cycles, current
- Contactor states
- Fault indicators
- Allow charge/discharge flags

**FR-UI-004**: Configuration page SHALL allow editing:
- Chemistry presets (LFP / NMC / Custom)
- Voltage limits (cell OV/UV, pack OV/UV, delta max)
- Temperature limits (min/max)
- Communication timeout
- Precharge time
- Energy parameters (capacity, SoH, initial SoC, polarity, efficiency)
- Auto-learning parameters (full/empty voltages, tail current, hold time)
- Inverter protocol selection
- Inverter current/voltage limits

**FR-UI-005**: Actions:
- Arm / Disarm
- Reset trip
- Reset energy counters
- Save configuration
- Apply chemistry presets

**FR-UI-006**: Authentication:
- Admin user (full access)
- Regular user (view + basic actions)

**FR-UI-007**: The system SHALL use AJAX polling for real-time updates without page refresh.

**Status**: ✅ **Implemented and Working** (minor UI improvements planned)

---

### 3.9 Configuration Persistence

**FR-CFG-001**: The system SHALL persist the following to NVS (Preferences):
- Chemistry selection
- All limit values (voltage, temp, delta, timeout, precharge time, expected cell count)
- Energy configuration (capacity, SoH, initial SoC, polarity, efficiency)
- Auto-learning parameters
- Language preference
- Inverter protocol and user limits

**FR-CFG-002**: The system SHALL persist energy state:
- Current SoC
- Coulomb balance
- Throughput
- Cycle count

**FR-CFG-003**: Configuration SHALL be loaded on boot.

**Status**: ✅ **Implemented and Working**

---

## 4. Non-Functional Requirements

### 4.1 Performance

**NFR-PERF-001**: The system SHALL process CAN messages with <100ms latency.

**NFR-PERF-002**: Web UI updates SHALL occur at ≥1 Hz (configurable via AJAX polling interval).

**NFR-PERF-003**: The system SHALL support WiFi reconnection on connection loss.

### 4.2 Safety

**NFR-SAFE-001**: All critical faults SHALL result in immediate contactor opening.

**NFR-SAFE-002**: The system SHALL NOT re-enable charge/discharge on fault without sufficient hysteresis margin.

**NFR-SAFE-003**: Contactor toggling SHALL respect minimum timing constraints to prevent relay damage.

### 4.3 Reliability

**NFR-REL-001**: The system SHALL operate continuously without watchdog resets.

**NFR-REL-002**: Configuration SHALL survive power loss (stored in NVS).

**NFR-REL-003**: The system SHALL handle CAN bus errors gracefully (e.g., bus-off recovery).

### 4.4 Maintainability

**NFR-MAINT-001**: Code SHALL follow consistent naming conventions.

**NFR-MAINT-002**: Critical sections SHALL be documented with comments.

**NFR-MAINT-003**: The system SHALL provide serial debug output.

---

## 5. Development Roadmap

### Phase 1: Foundation ✅ (COMPLETED)
- ✅ Core BMS communication
- ✅ Contactor control with soft disconnect
- ✅ Current sensor integration
- ✅ Protection logic with tapering and ratchet
- ✅ Web UI (dark theme, responsive, multi-language)
- ✅ SoC/SoH algorithms (coulomb counting + auto-learning)
- ✅ Balance health indicator and time estimator
- ✅ Event log (200 entries, LittleFS persistent)
- ✅ History charts (720 points, 1 hour, range selector)
- ✅ Night mode (CCL/DCL reduction by schedule)
- ✅ Chemistry presets (LFP/NMC/LTO)
- 🔄 SoH auto-learning field validation (in progress)

### Phase 2: Inverter Protocol Expansion (IN PROGRESS)
- ✅ SMA/Ingeteam 0x35x (verified with Ingeteam 6K)
- ✅ BYD HVS (verified with Ingeteam 6K)
- ✅ Pylontech Force H2 HV (29-bit extended CAN)
- ✅ Pylon LV (via 0x35x with PYLON manufacturer)
- ⚠️ Test Pylon protocol with real hardware
- ❌ Implement Sofar protocol
- ❌ Implement SolaX protocol
- ❌ Implement Sungrow protocol
- ❌ Implement Growatt protocol
- ❌ Implement FoxESS protocol

### Phase 3: Advanced Features ✅ (CORE COMPLETED)
- ✅ MQTT telemetry (TLS, HA discovery, remote commands)
- ✅ MQTT web config UI
- ✅ OTA firmware updates (HTTPS via MQTT)
- ✅ Remote logging (persistent event log)
- ✅ Historical data visualization (charts + history buffer)
- ✅ Cloud integration guide (Grafana/InfluxDB/Telegraf)
- ❌ Email/SMS alerts
- ❌ On-device persistent long-term data (LittleFS)

### Phase 4: Testing & Validation (PENDING)
- ❌ Automated unit tests
- ❌ Integration tests
- ❌ Long-term stability testing
- ❌ Safety certification preparation

### Phase 5: Professional Tooling (PARTIAL)
- ✅ PlatformIO project structure
- ✅ GitHub Actions workflow
- ✅ OTA firmware updates
- ✅ Version management (build numbers)
- ❌ Code modularization (split main.cpp)
- ❌ Automated build/flash scripts
- ❌ CI/CD pipeline (automated builds)

### Phase 6: Documentation (PARTIAL)
- ✅ FSD, hardware guide, troubleshooting, web UI guide
- ✅ Cloud integration guide
- ❌ User manual (multi-language PDFs)
- ❌ Video tutorials

---

## 6. Known Issues & Limitations

### Current Limitations
1. **Main contactor unused**: Logic uses CHG+DSG, main contactor GPIO defined but not used
2. **Single bank support**: Pylon protocol hardcoded to bank 0
3. **Monolithic codebase**: All logic in `src/main.cpp`, modularization pending
4. **No automated tests**: Manual testing only
5. **Pylon HV untested**: Pylontech Force H2 protocol not validated with real hardware

### Known Bugs
- None reported (will be updated during testing)

---

## 7. Testing Strategy

### 7.1 Unit Testing (TODO)
- Individual protection functions
- SoC calculation accuracy
- SoH learning algorithm
- Contactor sequencing logic

### 7.2 Integration Testing (TODO)
- Full charge/discharge cycles
- Multiple module scenarios
- Fault injection testing
- Inverter protocol compliance

### 7.3 Field Testing (ONGOING)
- Real-world battery operations
- SoC/SoH tracking accuracy
- Long-term stability

---

## 8. References

### Inspired By / Credits
- **dalathegreat/Battery-Emulator** (GPL-3.0): Inverter protocol implementations
- **JK BMS CAN Protocol**: (Reverse-engineered community documentation)
- **AHBC-CANB Manual**: Current sensor specification

### Standards
- CAN 2.0B (ISO 11898-1)
- IEEE 754 (Floating point)

---

## 9. Appendices

### Appendix A: CAN Message Formats

#### CAN Bus 1 (TWAI, 250 kbps) - BMS/Sensor

**JK BMS Messages:**
- **Format**: (To be documented based on reverse-engineering)
- **Baud Rate**: 250 kbps
- **Frame Type**: Standard (11-bit ID)

**AHBC-CANB Current Sensor:**
- **ID 0x3C2**: Current measurement
- **ID 0x3C3**: Additional data
- **Baud Rate**: 250 kbps
- **Format**: (To be documented)

#### CAN Bus 2 (MCP2515, 500 kbps) - Inverter

#### SMA/Ingeteam/Victron Protocol (0x35x)
- **0x351**: Charge/discharge voltage and current limits (every 1s)
- **0x355**: SoC/SoH percentage (every 1s)
- **0x356**: Pack voltage/current/temperature (every 1s)
- **0x359**: Alarms and warnings (every 1s)
- **0x35C**: Charge/discharge enable flags (every 1s)
- **0x35E**: Manufacturer name ASCII (every 1s)

#### BYD HVS Protocol
- **0x110**: Charge/discharge limits (every 2s)
- **0x150**: SoC/SoH/capacity (every 10s)
- **0x1D0**: Voltage/current/temp (every 10s)
- **0x210**: Temp min/max (every 10s)
- **0x190**: Alarms (every 60s)

#### Pylon Protocol
- **Request 0x02**: Battery info request
  - Response: 0x7310, 0x7320
- **Request 0x00**: Telemetry request
  - Response: 0x4210-0x4290 (9 frames)

### Appendix B: Chemistry Presets

#### LFP (Lithium Iron Phosphate)
```
Cell OV: 3.65V
Cell UV: 2.80V
Delta Max: 0.05V
Full Cell V: 3.55V
Empty Cell V: 2.95V
```

#### NMC (Nickel Manganese Cobalt)
```
Cell OV: 4.20V
Cell UV: 3.00V
Delta Max: 0.05V
Full Cell V: 4.15V
Empty Cell V: 3.10V
```

### Appendix C: Glossary

- **BMS**: Battery Management System
- **SoC**: State of Charge (%)
- **SoH**: State of Health (%)
- **EFC**: Equivalent Full Cycles
- **OV**: Overvoltage
- **UV**: Undervoltage
- **NVS**: Non-Volatile Storage
- **OTA**: Over-The-Air (firmware update)
- **AHBC**: Current sensor model
- **JK**: BMS manufacturer

---

**Document End**

*This document is a living specification and will be updated as the project evolves.*
