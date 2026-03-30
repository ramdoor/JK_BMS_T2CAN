# TODO List - JK BMS Master Controller

Organized task list for ongoing development.

---

## 🔥 High Priority (Immediate)

### Phase 1 Completion
- [ ] **Test SoC accuracy**
  - [ ] Full charge/discharge cycle
  - [ ] Compare calculated SoC with cell voltages
  - [ ] Verify coulombic efficiency calibration
  - [ ] Document any drift over 24h+ operation
  
- [ ] **Test SoH auto-learning**
  - [ ] Trigger full/empty detection events
  - [ ] Verify capacity update logic
  - [ ] Test edge cases (partial cycles, interruptions)
  
- [x] **Validate SMA/Ingeteam 0x35x protocol** ✅
  - [x] Dual CAN bus via MCP2515 (500 kbps)
  - [x] Verified with Ingeteam 6K inverter
  - [x] CAN2 status in web UI and API

- [x] **Validate BYD HVS protocol** ✅
  - [x] Verified working with Ingeteam 6K inverter
  - [x] Current limits enforcement confirmed
  - [x] Startup sequence working

- [ ] **Validate Pylon protocol**
  - [ ] Test with Pylon-compatible inverter
  - [ ] Verify request/response timing
  - [ ] Check telemetry accuracy

---

## ⚙️ Phase 2: Inverter Protocol Expansion

### New Protocol Implementations
- [ ] **Sofar Protocol**
  - [ ] Research CAN IDs and frame formats
  - [ ] Implement transmission logic
  - [ ] Test with Sofar inverter
  - [ ] Document configuration
  
- [ ] **SolaX Protocol**
  - [ ] Research Triple Power CAN protocol
  - [ ] Implement handler functions
  - [ ] Field testing
  
- [ ] **Sungrow Protocol**
  - [ ] Research SBR/SBT series protocol
  - [ ] Implementation
  - [ ] Testing
  
- [ ] **Growatt Protocol**
  - [ ] Research HV/LV variants
  - [ ] Dual protocol support
  - [ ] Testing
  
- [ ] **FoxESS Protocol**
  - [ ] Research H1/H3 series
  - [ ] Implementation
  
- [x] **SMA/Ingeteam/Victron (0x35x)** ✅
  - [x] Implemented and verified with Ingeteam 6K
  - [x] Dual CAN bus architecture (MCP2515 500 kbps)

- [ ] **SMA Tripower**
  - [ ] Research Sunny Island protocol (may already work with 0x35x)
  - [ ] Verify compatibility

### Protocol Testing
- [ ] Create CAN message validation suite
- [ ] Document each protocol's quirks
- [ ] Create protocol selection guide (which inverter → protocol)

---

## 🌐 Phase 3: Advanced Features

### MQTT Integration ✅
- [x] **MQTT Client Setup**
  - [x] Add PubSubClient library
  - [x] Configurable broker settings (via `data/mqtt_config.json`)
  - [x] TLS/SSL support (certs in LittleFS)
  - [x] Last Will and Testament (LWT) — `bms/{device_id}/online`
  - [x] Exponential backoff reconnect (5s-60s)
  - [x] Zero impact when not configured

- [x] **Topic Structure**
  - [x] Design topic hierarchy (`bms/{id}/telemetry`, `bms/{id}/event`, `bms/{id}/cmd/*`)
  - [x] Implement publishers for all telemetry (every 5s + on-event)
  - [x] Add command subscribers (cmd/config, cmd/action, cmd/ota)
  - [x] Home Assistant MQTT auto-discovery (sensors + armed switch)

- [x] **Web UI Integration** ✅
  - [x] MQTT settings page (endpoint, port, TLS, user/pass)
  - [x] Connection status indicator
  - [x] Device Name field (synced to cloud)
  - [ ] Test/reconnect buttons

### Cloud Portal ✅
- [x] Backend API (Node.js + Express + TypeScript)
- [x] Frontend Dashboard (React + Vite, matches local BMS UI)
- [x] Role hierarchy: superadmin → installer → monitor
- [x] Device lifecycle: auto-provision → claim → assign
- [x] Real-time telemetry via WebSocket
- [x] InfluxDB time-series storage with configurable resolution
- [x] MQTT bridge (auto-provision, fault alerts, name sync)
- [x] Nginx SPA routing with `/api/` prefix

### InfluxDB Integration
- [x] Cloud portal uses InfluxDB 2.7 for telemetry storage
- [ ] Direct ESP32 HTTP client for line protocol (optional)
- [ ] Create sample Grafana dashboards

### Alerting
- [ ] SMTP client for email
- [ ] Configurable alert rules
- [ ] Rate limiting (anti-spam)
- [ ] SMS integration (Twilio API)
- [ ] Alert acknowledgment system

### Remote Logging
- [x] Persistent event log (200 entries in LittleFS) — Contactor/fault/state tracking, survives reboots
- [x] CSV download from web UI
- [ ] Structured logging framework
- [ ] SD card logging (optional hardware)
- [ ] Remote log download API
- [ ] Auto-upload on critical fault

### Data Visualization
- [x] Web UI charts (Chart.js) — Real-time rolling with range selector
  - [x] Pack voltage over time
  - [x] Current profile
  - [x] SoC over time
- [x] Dual history buffers: 5s (1h) + 2min (24h), 720 points each
- [x] Range selector: 5min / 15min / 30min / 1h / 6h / 12h / 24h
- [x] Cloud portal charts (Recharts) with same range options
- [ ] On-device persistent data storage (LittleFS)
- [ ] CSV export API (historical data)
- [ ] Temperature trends chart
- [ ] Cell voltage history chart

---

## 🧪 Phase 4: Testing & Validation

### Automated Testing
- [ ] **Unit Test Framework**
  - [ ] Set up Unity or Google Test
  - [ ] Mock CAN bus
  - [ ] Test protection logic
  - [ ] Test SoC/SoH algorithms
  - [ ] Test state machine
  
- [ ] **Integration Tests**
  - [ ] Full cycle simulation
  - [ ] Fault injection
  - [ ] Stress testing
  
- [ ] **CI Integration**
  - [ ] Run tests on every commit
  - [ ] Coverage reporting
  - [ ] Automated regression detection

### Long-Term Testing
- [ ] 1000+ hour runtime test
- [ ] Memory leak detection
- [ ] Performance profiling
- [ ] Temperature cycling tests

### Safety Certification
- [ ] IEC 62619 compliance review
- [ ] FMEA (Failure Mode and Effects Analysis)
- [ ] FTA (Fault Tree Analysis)
- [ ] Safety documentation package
- [ ] Independent audit (if budget allows)

---

## 🛠️ Phase 5: Professional Tooling

### PlatformIO Migration
- [x] Create platformio.ini
- [x] Migrate code to src/ structure (code is in `src/main.cpp`)
- [ ] Modularize code (separate files for BMS, contactors, etc.)
- [ ] Test all build environments

### CI/CD
- [x] GitHub Actions workflow created
- [ ] Enable automated builds
- [ ] Firmware artifact generation
- [ ] Release automation

### OTA Updates
- [x] HTTPS OTA via MQTT command (`cmd/ota`) — URL + MD5 verification
- [x] Safety check: refuses update if contactors are closed
- [ ] Rollback on failure (dual partition scheme)
- [ ] Web UI firmware upload page
- [ ] Version display in UI
- [ ] ArduinoOTA (local network OTA, in addition to MQTT)

### Build Scripts
- [ ] build.sh (compile + version increment)
- [ ] flash.sh (OTA to device)
- [ ] flash_all.sh (multi-device)
- [ ] Docker build container
- [ ] Pre-commit hooks (formatting)

### Version Management
- [x] Version increment script created
- [ ] Semantic versioning enforcement
- [ ] Git tag automation
- [ ] Changelog generation

### Code Quality
- [ ] Clang-format configuration
- [ ] Clang-tidy integration
- [ ] Pre-commit formatting
- [ ] Code review checklist

---

## 📚 Phase 6: Documentation

### User Manual
- [ ] **Multi-language PDFs**
  - [ ] Spanish
  - [ ] English
  - [ ] German
  
- [ ] **Content**
  - [ ] Installation guide (with photos)
  - [ ] Configuration walkthrough
  - [ ] Operation guide
  - [ ] Maintenance procedures
  - [ ] Troubleshooting (expand current doc)

### API Documentation
- [ ] REST API reference
- [ ] MQTT topic schema
- [ ] CAN protocol reference
- [ ] Integration examples (Home Assistant, Node-RED)
- [ ] Postman collection

### Developer Guide
- [ ] Architecture overview
- [ ] Code structure explanation
- [ ] Tutorial: Adding new inverter protocol
- [ ] Tutorial: Custom protection logic
- [ ] Contributing guidelines (expand CONTRIBUTING.md)
- [ ] Testing guide

### Video Tutorials
- [ ] Hardware assembly
- [ ] Initial setup
- [ ] Web UI tour
- [ ] Firmware update
- [ ] Troubleshooting
- [ ] Advanced features (MQTT, auto-learning)

### Community
- [ ] FAQ page
- [ ] GitHub Wiki
- [ ] Example configurations
- [ ] Discussion forum guidelines

---

## 🚀 Future Enhancements (Backlog)

### Advanced BMS
- [ ] Active balancing support
- [ ] Multi-chemistry packs
- [ ] Predictive SoH (ML-based)
- [ ] Calendar aging models

### Connectivity
- [ ] Bluetooth Low Energy (BLE)
- [ ] LoRaWAN for remote sites
- [ ] Modbus RTU/TCP gateway
- [ ] OAuth2 authentication for API

### Safety
- [ ] External watchdog redundancy
- [ ] Configuration backup encryption
- [ ] Tamper detection alerts
- [ ] Black box logging

### UI/UX
- [x] Mobile-responsive web UI
- [x] Dark mode (dark theme with CSS variables)
- [x] Multi-language support (ES/EN/DE)
- [ ] Custom alert sounds
- [ ] Additional languages (FR, IT, PT, CN)

### Hardware
- [ ] Touchscreen LCD support
- [ ] E-ink status display
- [ ] Relay board auto-detection
- [ ] Hot-swap module support

---

## 🐛 Known Issues (Bugs to Fix)

### Current
- None reported yet

### To Investigate
- [ ] Long-term WiFi stability (monitor for disconnects)
- [ ] NVS wear leveling (how often to save state?)
- [ ] CAN bus error handling (bus-off recovery tested?)

---

## 🔧 Code Refactoring (Technical Debt)

### Immediate
- [ ] Split main.cpp into modular files:
  - [ ] bms_manager.cpp/h
  - [ ] contactor_control.cpp/h
  - [ ] protection.cpp/h
  - [ ] energy_management.cpp/h
  - [ ] inverter_protocols.cpp/h
  - [ ] web_server.cpp/h

### Nice to Have
- [ ] Use C++ classes instead of global state
- [ ] Implement RAII for CAN resources
- [ ] Add error handling with Result<T, E> pattern
- [ ] Replace magic numbers with named constants
- [ ] Improve variable naming consistency

---

## 📝 Documentation Improvements

### README
- [ ] Add screenshots of web UI
- [ ] Add wiring diagram images
- [ ] Video demo link (when available)
- [ ] Comparison table (vs. other BMS solutions)

### FSD
- [x] Initial version complete
- [ ] Add CAN message format appendix
- [ ] Document learning algorithm in detail
- [ ] Add state transition diagrams

### HARDWARE
- [x] Initial version complete
- [ ] Add high-res wiring diagrams
- [ ] Include part sourcing links
- [ ] Add safety certification references

### TROUBLESHOOTING
- [x] Initial version complete
- [ ] Add more common issues as they're discovered
- [ ] Include photos of correct vs. incorrect wiring
- [ ] Add decision tree flowchart

---

## 🎯 Milestones

### v1.0.0 (Target: Q2 2026)
- All Phase 1 tasks complete ✅
- SMA/Ingeteam + BYD HVS protocols verified ✅
- MQTT + OTA + Event log ✅
- Pylon protocol field validation pending
- Production-ready firmware

### v1.1.0 (Target: Q3 2026)
- Additional inverter protocols (Sofar, SolaX, Sungrow)
- Code modularization (split main.cpp)
- Automated testing framework

### v2.0.0 (Target: Q4 2026)
- Full CI/CD pipeline
- Multi-language documentation (PDFs)
- Cloud portal frontend

---

## 💡 Ideas / Wishlist

- [x] Mobile app (React Native + Expo SDK 55) — Fase 1 completa, repo `jkbms-app`
- [ ] Voice alerts (TTS over speaker)
- [ ] Integration with home automation (Alexa, Google Home)
- [ ] Blockchain-based battery passport (lifetime tracking)
- [ ] AI-powered anomaly detection
- [ ] Peer-to-peer energy trading integration

---

**Last Updated:** March 30, 2026
**Next Review:** Weekly during active development
