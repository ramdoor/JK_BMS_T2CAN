# Development Roadmap
# JK BMS Master Controller

Detailed planning for current and future development phases.

---

## Phase 1: Foundation ✅ (COMPLETED - 95%)

**Goal:** Establish core BMS functionality with basic safety and monitoring.

### Completed Features
- [x] CAN bus communication (250 kbps)
- [x] JK BMS module aggregation (up to 10 modules)
- [x] AHBC-CANB current sensor integration
- [x] Contactor control with precharge sequencing
- [x] Protection logic (OV, UV, delta, temp, timeout)
- [x] Directional permission system (charge/discharge)
- [x] Web UI (multi-language: ES/EN/DE)
- [x] Configuration persistence (NVS)
- [x] Basic SoC calculation (coulomb counting)
- [x] Basic SoH auto-learning algorithm
- [x] BYD HVS inverter protocol (verified working with Ingeteam 6K)
- [x] Pylon HV/LV inverter protocol (untested)
- [x] SMA/Ingeteam 0x35x protocol (verified working with Ingeteam 6K)
- [x] Dual CAN bus: TWAI 250 kbps (BMS) + MCP2515 500 kbps (Inverter)
- [x] MCP2515 SPI integration with loopback self-test
- [x] CAN2 status monitoring in web UI and API
- [x] Expected cell count validation (critical fault + arm blocking)
- [x] NTP time synchronization with real timestamps in event log

### Remaining Work
- [ ] Field-test SoC/SoH accuracy (User testing in progress)
- [x] Validate BYD HVS protocol with real hardware (verified with Ingeteam 6K)
- [ ] Validate Pylon protocol with real hardware

**Status:** Ready for Phase 2

---

## Phase 2: Inverter Protocol Expansion 🚧 (NEXT - Q1 2026)

**Goal:** Test existing protocols and implement additional inverter support.

### 2.1 Protocol Validation (Week 1-2)
- [x] Test SMA/Ingeteam 0x35x with Ingeteam 6K inverter (WORKING)
- [ ] Test BYD HVS with actual BYD inverter
  - [ ] Verify CAN frame formats
  - [ ] Validate current/voltage limits enforcement
  - [ ] Test startup sequence
  - [ ] Confirm alarm transmission
- [ ] Test Pylon protocol with compatible inverter
  - [ ] Verify request/response cycle
  - [ ] Test multi-bank support (if available)
  - [ ] Validate telemetry accuracy

### 2.2 Sofar Protocol Implementation (Week 3)
- [ ] Research Sofar CAN protocol
  - [ ] Document CAN IDs and frame formats
  - [ ] Identify startup/shutdown sequences
- [ ] Implement protocol handler
  - [ ] Create `sofar_send_XXX()` functions
  - [ ] Add timing logic (update intervals)
  - [ ] Integrate with main inverter task
- [ ] Testing
  - [ ] Bench test with CAN sniffer
  - [ ] Field test with Sofar inverter
  - [ ] Validate safety limits

### 2.3 SolaX Protocol Implementation (Week 4)
- [ ] Research SolaX Triple Power protocol
- [ ] Implement handler functions
- [ ] Test with SolaX inverter
- [ ] Document configuration requirements

### 2.4 Sungrow Protocol Implementation (Week 5)
- [ ] Research Sungrow battery protocol (SBR/SBT series)
- [ ] Implement handler
- [ ] Testing and validation

### 2.5 Growatt Protocol Implementation (Week 6)
- [ ] Research Growatt HV/LV protocols
- [ ] Implement dual-protocol support
- [ ] Testing with both HV and LV inverters

### 2.6 Additional Protocols (Week 7-8)
- [ ] FoxESS (H1/H3 series)
- [ ] SMA Tripower (Sunny Island)
- [ ] Deye/Sunsynk (if demand exists)

### Deliverables
- Fully tested BYD + Pylon protocols
- 4+ new inverter protocols implemented
- Protocol selection guide (which inverter → which protocol)
- Updated web UI with protocol-specific settings

**Timeline:** 8 weeks  
**Dependencies:** Access to test inverters (may require partnerships)

---

## Phase 3: Advanced Features ✅🚧 (Q1-Q2 2026)

**Goal:** Add professional monitoring, logging, and remote management.

### 3.1 MQTT Telemetry ✅ (COMPLETED - Feb 2026)
- [x] MQTT client library integration (PubSubClient + WiFiClientSecure)
- [x] Configurable broker settings (via `data/mqtt_config.json`)
- [x] Topic structure design
  ```
  bms/{device_id}/telemetry   — Pack data every 5s
  bms/{device_id}/event       — Faults/state changes on-event
  bms/{device_id}/cmd/config  — Remote config updates
  bms/{device_id}/cmd/action  — Remote arm/disarm/reset
  bms/{device_id}/cmd/ota     — Remote firmware update
  bms/{device_id}/online      — LWT status
  ```
- [x] Publish intervals (5s telemetry, on-event for faults/state changes)
- [x] TLS/SSL support (certs in LittleFS: `mqtt_ca.pem`, `mqtt_cert.pem`, `mqtt_key.pem`)
- [x] Last Will and Testament (LWT) for disconnect detection
- [x] Home Assistant MQTT auto-discovery (sensors + armed switch)
- [x] Exponential backoff reconnect (5s-60s)
- [x] Zero impact when not configured (disabled by default)
- [x] Web UI integration (MQTT settings page — endpoint, port, TLS, user/pass)

### 3.2 InfluxDB Integration (Week 3)
- [ ] HTTP client for InfluxDB line protocol
- [ ] Configurable database/bucket settings
- [ ] Efficient batching (reduce write frequency)
- [ ] Automatic retention policy handling
- [ ] Sample Grafana dashboards
  - [ ] Real-time monitoring dashboard
  - [ ] Historical SoC/SoH trends
  - [ ] Cell voltage distribution
  - [ ] Fault history

### 3.3 Email/SMS Alerting (Week 4)
- [ ] SMTP client for email alerts
- [ ] Configurable alert triggers (critical faults, SoC thresholds)
- [ ] Rate limiting (don't spam on persistent faults)
- [ ] SMS via Twilio/Nexmo API (optional)
- [ ] Alert acknowledgment system (silence until next occurrence)

### 3.4 Remote Logging and Diagnostics (Week 5-6)
- [x] Persistent event log in LittleFS (200 events) — Tracks contactors, faults, state transitions, survives reboots
- [x] CSV download from web UI
- [ ] Structured logging to SD card (optional hardware)
- [ ] Log levels (ERROR, WARN, INFO, DEBUG)
- [ ] Remote log download via API
- [ ] Automatic log upload on fault (to cloud storage)
- [ ] Log rotation and compression

### 3.5 Historical Data Visualization (Week 7-8)
- [x] Web UI charts (Chart.js) — Real-time rolling 60s for voltage, current, SoC
- [x] Server-side history buffer (720 points, ~1 hour at 5s intervals) with `/api/history` endpoint and range selector (5/15/30/60 min)
- [x] Load History button to populate charts with server-buffered data
- [ ] On-device persistent data storage (LittleFS) for long-term history
- [ ] CSV export functionality (historical data)
- [ ] Temperature trends chart
- [ ] Cell voltage history chart
- [ ] Data export API (JSON/CSV)

### Deliverables
- [x] MQTT-enabled firmware with TLS and HA auto-discovery
- [x] Real-time charts and history buffer
- [x] Event logging with CSV export
- [ ] Pre-configured Grafana dashboards
- [ ] Email alert system (optional activation)
- [ ] Full remote diagnostics capability
- [ ] Long-term data export tools

**Timeline:** 8 weeks (MQTT and charts completed Feb 2026, remaining items pending)
**Dependencies:** Cloud infrastructure (MQTT broker, InfluxDB instance)

---

## Phase 4: Testing & Validation 🧪 (Q3 2026)

**Goal:** Ensure reliability, safety, and code quality through comprehensive testing.

### 4.1 Automated Unit Tests (Week 1-3)
- [ ] Set up test framework (Unity/Google Test)
- [ ] Test coverage goals: >70%
- [ ] Tests for:
  - [ ] Protection logic (OV, UV, delta, temp)
  - [ ] SoC calculation accuracy
  - [ ] SoH learning algorithm
  - [ ] Contactor state machine
  - [ ] CAN message parsing
  - [ ] Configuration save/load
- [ ] Mock CAN bus for offline testing
- [ ] CI integration (run tests on every commit)

### 4.2 Integration Tests (Week 4-5)
- [ ] Full charge/discharge cycle simulation
- [ ] Multi-module fault injection
- [ ] CAN bus failure scenarios (timeout, corruption)
- [ ] Rapid state transitions (arm/disarm/trip)
- [ ] Inverter protocol compliance tests
- [ ] Stress testing (high message rates, rapid limit changes)

### 4.3 Long-Term Stability Testing (Week 6-12)
- [ ] 1000+ hour runtime test
- [ ] Memory leak detection (heap fragmentation analysis)
- [ ] Watchdog reset monitoring
- [ ] Performance profiling (CPU usage, task delays)
- [ ] Temperature cycling (0°C to 55°C)
- [ ] Network instability handling (WiFi dropouts)

### 4.4 Safety Certification Prep (Week 13-16)
- [ ] IEC 62619 compliance review (battery systems)
- [ ] UL 1741 considerations (inverter integration)
- [ ] Fault tree analysis (FTA)
- [ ] Failure mode and effects analysis (FMEA)
- [ ] Safety documentation package
- [ ] Independent safety audit (if budget allows)

### Deliverables
- Comprehensive test suite (unit + integration)
- Test report (coverage, pass rate)
- Long-term stability report
- Safety analysis documentation
- Certification readiness assessment

**Timeline:** 16 weeks  
**Dependencies:** Test hardware (multiple BMS modules, test inverters)

---

## Phase 5: Professional Tooling 🛠️ (Q4 2026)

**Goal:** Streamline development workflow and enable production deployment.

### 5.1 PlatformIO Migration ✅ (Week 1)
- [x] Create `platformio.ini`
- [x] Define build environments (production, debug, OTA)
- [x] Migrate existing code to `src/` structure (code in `src/main.cpp`)
- [ ] Test compilation across all environments
- [ ] Update documentation

### 5.2 CI/CD Pipeline ✅ (Week 2)
- [x] GitHub Actions workflow
- [ ] Automatic build on push/PR
- [ ] Firmware artifact generation
- [ ] Automated testing (when tests ready)
- [ ] Release tagging and GitHub Releases

### 5.3 OTA Firmware Updates (Week 3-4)
- [x] MQTT-triggered HTTPS OTA (`cmd/ota` with URL + MD5)
- [x] Safety: refuses update while contactors are closed
- [x] ACK with progress/result via MQTT
- [ ] ArduinoOTA for local network updates
- [ ] Secure OTA (password protection)
- [ ] Rollback on failed update (dual partition scheme)
- [ ] Web UI: firmware upload page
- [ ] Version display in UI

### 5.4 Automated Build/Flash Scripts (Week 5)
- [ ] Bash script: `build.sh` (compile + version increment)
- [ ] Bash script: `flash.sh <IP>` (OTA to specific device)
- [ ] Batch script: `flash_all.sh` (multi-device deployment)
- [ ] Docker container for reproducible builds
- [ ] Pre-commit hooks (code formatting, linting)

### 5.5 Version Management (Week 6)
- [ ] Semantic versioning (MAJOR.MINOR.PATCH)
- [ ] Auto-increment build number
- [ ] Git tag automation
- [ ] Changelog generation (from commit messages)
- [ ] Version API endpoint (`/api/version`)

### 5.6 Code Quality Tools (Week 7-8)
- [ ] Clang-format configuration
- [ ] Clang-tidy static analysis
- [ ] PlatformIO check integration
- [ ] Pre-commit formatting enforcement
- [ ] Code review checklist (PR template)

### Deliverables
- Fully migrated PlatformIO project
- Automated CI/CD pipeline
- OTA update capability
- Build automation scripts
- Version management system

**Timeline:** 8 weeks  
**Dependencies:** GitHub repository setup

---

## Phase 6: Documentation 📚 (Ongoing)

**Goal:** Provide comprehensive documentation for users and developers.

### 6.1 User Manual (Month 1-2)
- [ ] Multi-language support (ES/EN/DE PDFs)
- [ ] Installation guide (step-by-step with photos)
- [ ] Configuration guide (web UI walkthrough)
- [ ] Operation guide (arming, monitoring, fault handling)
- [ ] Maintenance guide (firmware updates, calibration)
- [ ] Troubleshooting section (expanded from current doc)

### 6.2 API Documentation (Month 3)
- [ ] REST API reference (all endpoints)
- [ ] MQTT topic schema
- [ ] CAN protocol reference (BMS, inverters, current sensor)
- [ ] Integration examples (Home Assistant, OpenHAB, Node-RED)
- [ ] Postman collection (API testing)

### 6.3 Developer Guide (Month 4)
- [ ] Architecture overview (block diagrams)
- [ ] Code structure explanation
- [ ] Adding new inverter protocols (tutorial)
- [ ] Custom protection logic guide
- [ ] Contributing guidelines (coding standards)
- [ ] Testing guide (running unit/integration tests)

### 6.4 Video Tutorials (Month 5-6)
- [ ] Hardware assembly and wiring
- [ ] Initial setup and configuration
- [ ] Web UI tour
- [ ] Firmware update procedure
- [ ] Troubleshooting common issues
- [ ] Advanced features (MQTT, auto-learning)

### 6.5 Community Resources (Ongoing)
- [ ] FAQ (frequently asked questions)
- [ ] GitHub Wiki setup
- [ ] Discussion forum guidelines
- [ ] Example configurations (various battery setups)
- [ ] User-contributed inverter configs

### Deliverables
- Complete user manual (3 languages)
- API documentation site
- Developer onboarding guide
- Video tutorial series (6+ videos)
- Active community support

**Timeline:** 6 months (parallel with other phases)  
**Dependencies:** Technical writing resources, video production

---

## Future Enhancements (Backlog)

Ideas for post-v1.0 development, including insights from market analysis of Daly, JK, Batrium, and Orion BMS systems.

### Safety & Diagnostics (High Priority)
- [ ] **Welded contactor detection** — Measure voltage after contactor before closing; voltage present = welded fault (Orion BMS feature)
- [ ] **Cell internal resistance estimation** — Calculate IR per cell from ΔV during current pulses for better SoH accuracy
- [ ] **Predictive alerts** — Detect anomalous trends (rising delta, weak cell) before they become faults
- [ ] **Remaining Useful Life (RUL) estimation** — Predict cycles remaining based on SoH degradation pattern
- [ ] **Black box logging** — Non-volatile fault history for post-incident analysis

### Advanced BMS Features
- [ ] Active cell balancing support (if hardware available)
- [ ] Multi-chemistry support (within same pack)
- [ ] Predictive maintenance (ML-based SoH prediction)
- [ ] Calendar aging models
- [ ] **SoC via Kalman Filter** — More accurate than pure Coulomb counting (combines OCV + current integration)
- [ ] **Weak cell detection** — Identify cells that drop faster under load
- [ ] **Parallel pack protection** — Prevent inrush current when connecting packs in parallel (Daly M-series feature)
- [ ] **Battery profiles/presets** — Quick setup for LFP, Li-ion, LTO with default voltage thresholds
- [ ] **Cycle history logging** — Store complete charge/discharge cycles (Ah in/out, duration, Vmax/Vmin) in LittleFS
- [x] **Balance time estimator** — Show estimated time to balance based on delta and balance current ✅

### Connectivity
- [ ] Bluetooth Low Energy (BLE) app integration
- [ ] LoRaWAN for remote installations
- [ ] Modbus RTU/TCP gateway (industrial integration)
- [ ] RESTful API with OAuth2 authentication
- [ ] **GPS + Geofencing** — Alert if system leaves defined zone (anti-theft, Daly/JK feature)
- [ ] **Telegram/Push notifications** — Critical alerts via MQTT → Node-RED → Telegram

### Safety Enhancements
- [ ] Redundant protection (external watchdog)
- [ ] Encrypted configuration backup
- [ ] Tamper detection (unauthorized access alerts)
- [ ] **Hibernation mode** — Ultra-low power mode for long-term storage (Batrium feature)

### User Experience
- [x] Mobile-responsive web UI (responsive grid layout, mobile/tablet/desktop breakpoints)
- [x] Dark mode (dark theme with CSS variables, slate palette)
- [x] Localization (ES/EN/DE implemented)
- [ ] Custom alert sounds/visuals
- [ ] Additional languages (FR, IT, PT, CN)
- [x] **Night/quiet mode** — Reduce CCL/DCL during configurable hours (electricity tariff optimization) ✅
- [ ] **Daly Cloud-style IoT platform** — Multi-device management, remote batch upgrades

### Hardware Expansion
- [ ] Touchscreen LCD support (local display)
- [ ] E-ink status display (low power)
- [ ] Relay board auto-detection
- [ ] Hot-swappable module support

---

## Success Metrics

**Phase 2:**
- 80% of popular inverters supported
- Zero protocol-related faults in field testing

**Phase 3:**
- MQTT uptime >99.5%
- Alert latency <5 seconds
- Grafana dashboards used by 50% of users

**Phase 4:**
- Test coverage >70%
- Zero critical bugs in 1000-hour test
- Safety audit pass

**Phase 5:**
- OTA success rate >95%
- Build time <2 minutes
- CI/CD pipeline green on all branches

**Phase 6:**
- User manual completion rate >90% (users reading it)
- Average support ticket resolution <24 hours
- Community contributions (PRs) >10/quarter

---

## Resource Requirements

### Hardware
- Multiple test inverters (BYD, Pylon, Sofar, etc.) - ~$15,000
- Battery simulator or test pack - ~$5,000
- CAN analyzers and sniffers - ~$1,000
- Multiple ESP32 boards for testing - ~$500

### Software/Services
- MQTT broker (cloud or self-hosted) - $0-50/month
- InfluxDB Cloud - $0-100/month
- GitHub Actions minutes (if over free tier) - $0-50/month
- Technical writing tools (Grammarly, etc.) - $30/month

### Human Resources
- Firmware developer (part-time or full-time)
- Technical writer (contract or part-time)
- Video producer (for tutorials, contract)
- Beta testers (community volunteers)

---

## Risk Management

| Risk | Impact | Mitigation |
|------|--------|-----------|
| Inverter protocol incompatibility | High | Thorough testing, community feedback |
| Hardware failure in testing | Medium | Backup devices, insurance |
| MQTT broker downtime | Medium | Fallback to local logging, redundant brokers |
| Security vulnerabilities | High | Code audits, regular updates, bounty program |
| Lack of community adoption | Medium | Marketing, documentation, support |
| Regulatory changes (safety) | High | Monitor standards, flexible design |

---

**Last Updated:** March 24, 2026
**Next Review:** Q2 2026

---

## Market Research References

Ideas in "Future Enhancements" section were informed by analysis of:
- [Daly BMS](https://www.dalybms.com/) — M-series parallel protection, IoT cloud, GPS
- [JK BMS](https://www.jkbms.com/) — Active balancing, delta ≤5mV target, Bluetooth app
- [Batrium](https://www.batrium.com/) — WatchMon distributed architecture, 250-cell monitoring, 10-year logging
- [Orion BMS](https://www.orionbms.com/) — Welded contactor detection, dual CAN, SOC/SOH algorithms
