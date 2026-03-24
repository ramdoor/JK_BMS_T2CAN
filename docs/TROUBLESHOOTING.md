# Troubleshooting Guide
# JK BMS Master Controller

Quick reference for diagnosing and fixing common issues.

---

## Table of Contents
1. [System Won't Boot](#system-wont-boot)
2. [WiFi Connection Issues](#wifi-connection-issues)
3. [CAN Bus Problems](#can-bus-problems)
4. [Contactor Issues](#contactor-issues)
5. [Current Sensor Problems](#current-sensor-problems)
6. [Protection Faults](#protection-faults)
7. [Web UI Issues](#web-ui-issues)
8. [SoC/SoH Inaccuracies](#socsoh-inaccuracies)

---

## System Won't Boot

### Symptoms
- No serial output
- ESP32 LED not blinking
- No WiFi access point

### Checks
1. **Power Supply**
   - [ ] Measure voltage at ESP32 VIN (should be 12V or 5V depending on board)
   - [ ] Check polarity (reversed power can damage board)
   - [ ] Verify power supply current capacity (>500mA)

2. **USB Connection** (for programming)
   - [ ] Try different USB cable (some are charge-only)
   - [ ] Test different USB port
   - [ ] Install CH340/CP2102 drivers if needed

3. **Firmware**
   - [ ] Re-flash firmware via USB
   - [ ] Check for compile errors in Arduino IDE
   - [ ] Verify correct board selected (ESP32 Dev Module)

### Serial Output Expected
```
⏳ Iniciando JK BMS + Contactores + SoC/SoH learning ...
🌐 Conectando a WiFi...
✅ WiFi OK. IP: 192.168.1.XXX
✅ Bus CAN1 (BMS) activo - 250 kbps
✅ MCP2515 loopback OK
✅ Bus CAN2 (Inverter) activo - 500 kbps via MCP2515
🌍 Servidor web listo
```

---

## WiFi Connection Issues

### Symptoms
- ESP32 boots but no IP address shown
- Can't access web UI
- WiFi connection timeout

### Checks
1. **Credentials**
   - [ ] SSID and password correct in code (lines 22-23)
   - [ ] Check for typos, case-sensitive
   - [ ] Verify WiFi network is 2.4GHz (ESP32 doesn't support 5GHz)

2. **Network**
   - [ ] Router is powered on and working
   - [ ] ESP32 within WiFi range
   - [ ] No MAC address filtering blocking ESP32

3. **Code**
   ```cpp
   const char* ssid = "YourSSID";        // Line 22
   const char* password = "YourPassword"; // Line 23
   ```

4. **Fallback**
   - ESP32 will retry connection every 20 seconds
   - Check serial monitor for "⚠️ WiFi timeout, reintentando..."

### Fix
- Update WiFi credentials and re-flash
- Move ESP32 closer to router for testing
- Try connecting ESP32 to phone hotspot to isolate issue

---

## CAN Bus Problems

### Symptoms
- No JK BMS modules detected (activeIdsCsv empty)
- "Timeout comm" fault
- No current sensor readings
- Inverter not responding

### Checks

#### 1. **Termination**
- [ ] 120Ω resistor at EACH end of bus (measure ~60Ω across CAN-H to CAN-L with power off)
- [ ] Not over-terminated (too many resistors = low resistance = bus failure)

#### 2. **Wiring**
- [ ] CAN-H and CAN-L not swapped
- [ ] Twisted pair used (not separate wires)
- [ ] Shield grounded at ONE point only (if using shielded cable)
- [ ] Total bus length <40m @ 250 kbps
- [ ] All stubs (branches) <30cm

#### 3. **Baud Rate**
- [ ] BMS/sensor bus: All devices set to 250 kbps (TWAI bus, GPIO 23 LOW)
- [ ] Inverter bus: MCP2515 set to 500 kbps (automatic in firmware)

#### 4. **Power**
- [ ] All CAN devices powered (JK BMS, AHBC, inverter)
- [ ] Common ground between devices

#### 5. **ME2107 Transceiver**
- [ ] GPIO 16 (ME2107_EN) driven HIGH
- [ ] Measure 2.5V on CAN-H and CAN-L (recessive state)

### Testing

**Loopback Test** (no other devices):
```cpp
// In setup(), after twai_start():
twai_message_t tx_msg = {
  .identifier = 0x123,
  .data_length_code = 8,
  .data = {1,2,3,4,5,6,7,8}
};
if (twai_transmit(&tx_msg, pdMS_TO_TICKS(1000)) == ESP_OK) {
  Serial.println("✅ CAN TX OK");
}

twai_message_t rx_msg;
if (twai_receive(&rx_msg, pdMS_TO_TICKS(1000)) == ESP_OK) {
  Serial.println("✅ CAN RX OK (loopback)");
}
```

If loopback fails, transceiver or ESP32 CAN peripheral issue.

---

## Contactor Issues

### Symptoms
- Contactors don't click when arming
- Wrong sequence
- Contactors chatter (rapid on/off)
- Stuck closed or stuck open

### Checks

#### 1. **Relay Module**
- [ ] VCC and GND connected to ESP32 power
- [ ] Voltage at relay coil matches rating (5V or 12V)
- [ ] Measure GPIO output: should toggle 0V → 3.3V

#### 2. **Polarity**
- [ ] If relays activate when they shouldn't, set `CONTACTOR_ACTIVE_LOW = 1`
- [ ] Test with multimeter: measure GPIO when "armed" vs "disarmed"

#### 3. **High-Current Contactors**
- [ ] Coil voltage matches control signal (12V or 24V)
- [ ] Check for manual override buttons (some contactors have them)
- [ ] Verify contactor auxiliary contacts (if used for feedback)

#### 4. **Timing**
- [ ] `CONTACTOR_MIN_TOGGLE_MS` prevents rapid toggling (default 1500ms)
- [ ] Precharge time sufficient (default 2000ms)

### Sequence Expected

```
Arm command → 
  [0ms]    Precharge ON
  [2000ms] Charge ON, Discharge ON
  [3000ms] Precharge OFF
  → State = CLOSED
```

**Serial Output:**
```
⚡ Activar PRECHARGE...
⏳ Esperando precarga...
⚡ Cerrar CHARGE
⚡ Cerrar DISCHARGE
💤 Desactivar PRECHARGE
✅ Sistema CERRADO
```

---

## Current Sensor Problems

### Symptoms
- Current always shows 0.0A
- Current reading timeout
- Current polarity inverted

### Checks

#### 1. **CAN Connection**
- [ ] AHBC-CANB connected to same CAN bus
- [ ] CAN IDs configured (0x3C2, 0x3C3 by default)
- [ ] Sensor powered (12V or 24V depending on model)

#### 2. **Wiring**
- [ ] Current flows through sensor shunt/hall element
- [ ] Direction: Battery → Sensor → Inverter (or vice versa)

#### 3. **Polarity**
- [ ] If discharge shows positive, set `currentPolarity = -1`
- [ ] Check via web UI: Current should be negative when discharging

#### 4. **Calibration**
- [ ] Some sensors require zero offset calibration (no current flowing)
- [ ] Refer to AHBC-CANB manual for configuration

### Testing
- Apply known load (e.g., 10A)
- Check current reading in web UI
- If inverted, toggle polarity setting

---

## Protection Faults

### Cell Overvoltage (OV)

**Fault Message:** "Cell OV: X.XXV > 3.65V"

**Causes:**
- Overcharging
- Incorrect cell OV limit for chemistry
- Imbalanced cells (one cell much higher)

**Fix:**
1. Stop charging immediately
2. Check individual cell voltages (web UI)
3. If using NMC, increase OV limit (e.g., 4.20V)
4. Balance cells if delta >50mV
5. Reset trip and re-arm when safe

### Cell Undervoltage (UV)

**Fault Message:** "Cell UV: X.XXV < 2.80V"

**Causes:**
- Over-discharging
- Incorrect cell UV limit
- Dead cell

**Fix:**
1. Stop discharging
2. Charge battery slowly
3. If using NMC, decrease UV limit (e.g., 3.00V)
4. If one cell much lower, check for internal short or damage

### Delta Voltage

**Fault Message:** "Delta >50mV"

**Causes:**
- Cell imbalance
- Passive balancing insufficient
- Weak cell

**Fix:**
1. Enable BMS balancing (if available)
2. Top-balance pack (charge to 100%, wait 4+ hours)
3. If persistent, identify weak cell and replace

### Temperature

**Fault Message:** "Temp out of range"

**Causes:**
- High current (heating)
- Poor cooling
- Incorrect temperature sensor

**Fix:**
1. Reduce current (lower inverter limits)
2. Improve airflow
3. Check BMS temperature sensor calibration

### Communication Timeout

**Fault Message:** "Timeout comm"

**Causes:**
- CAN bus failure
- BMS module powered off
- Loose connection

**Fix:**
1. Check CAN wiring
2. Verify all modules powered
3. Increase timeout if needed (default 3000ms)

### Cell Count Mismatch

**Fault Message:** "CELLS:XX/YY"

**Causes:**
- Battery module disconnected or not responding
- Module added/removed without updating configuration
- CAN communication partial failure

**Fix:**
1. Verify all BMS modules are powered and communicating
2. Check CAN wiring to all modules
3. If configuration changed intentionally, update "Expected Cells" in config (Settings page)
4. Set Expected Cells to 0 to disable validation

---

## Web UI Issues

### Can't Access UI

**Symptoms:** Browser shows "Can't reach this page"

**Checks:**
- [ ] Correct IP address (check serial monitor)
- [ ] Same network as ESP32
- [ ] Ping ESP32: `ping 192.168.1.XXX`
- [ ] Web server started (serial: "🌍 Servidor web listo")

### UI Not Updating

**Symptoms:** Cell voltages/SoC frozen

**Checks:**
- [ ] AJAX polling enabled (check browser console for errors)
- [ ] ESP32 not crashed (check serial output)
- [ ] Refresh page (Ctrl+F5)

### Authentication Loop

**Symptoms:** Keeps asking for login

**Checks:**
- [ ] Correct username/password
- [ ] Browser cookies enabled
- [ ] Try different browser

---

## SoC/SoH Inaccuracies

### SoC Drifts Over Time

**Causes:**
- Coulombic efficiency incorrect
- Current sensor offset
- No auto-learning events

**Fix:**
1. Perform full charge/discharge cycle for learning
2. Adjust efficiency (0.98-0.999 typical for LiFePO4)
3. Reset energy counters after full charge

### SoH Not Updating

**Causes:**
- Auto-learning thresholds not met
- Insufficient hold time at full/empty
- Learning cycle incomplete (requires full-to-empty in one session)
- Critical fault active (learning pauses during `criticalFault`)

**How the learning works:**
The system must observe a complete cycle: FULL → discharge → EMPTY. FULL is detected when the **maximum** cell voltage reaches `fullCellV` (not minimum — this allows learning even with unbalanced packs). It measures the total Ah discharged (`learnDischargeAh`, shown as "Learn dAh" on the status page) and uses that as the new SoH capacity.

**Fix:**
1. Check `fullCellV` and `emptyCellV` match your chemistry (LFP: 3.55V / 2.95V)
2. Ensure current drops below `tailCurrentA` (default 2A) for at least `holdMs` (default 120s) at both full and empty states
3. Monitor learning phase on the status page: should show "RUNNING" after a FULL event, and "Learn dAh" should increase during discharge
4. Monitor serial output for learning events: `learn=RUN dAh=XX.XX`
5. Verify no critical faults are active (learning is skipped during faults)
6. The learned capacity is clamped to 10%–150% of nominal capacity; if your battery is outside this range, adjust the nominal capacity first
7. Note: SoC sent to inverter is capped at 99% until a real FULL event is confirmed

### SoC Jumps After Reset

**Causes:**
- SoC not persisted to NVS
- Volatile memory cleared

**Fix:**
1. Verify `saveEnergyState()` called periodically
2. Check NVS write success (serial output)
3. Don't rely on initial SoC estimate—perform learning

---

## MCP2515 / Inverter CAN Bus Issues

### Symptoms
- "MCP2515 FAIL" on boot serial output
- "MCP2515 loopback FAIL" message
- Inverter not recognizing battery
- CAN2 error counter rising in web UI

### Checks

#### 1. **SPI Wiring**
- [ ] CS → GPIO 5
- [ ] SCK → GPIO 18
- [ ] MOSI (SI) → GPIO 12
- [ ] MISO (SO) → GPIO 35 (input-only pin, correct for MISO)
- [ ] INT → GPIO 34 (input-only pin, correct for interrupt)
- [ ] VCC → 5V (not 3.3V)
- [ ] GND → common ground with ESP32

#### 2. **MCP2515 Module**
- [ ] Crystal is **8 MHz** (firmware expects `MCP_8MHZ`)
- [ ] Module has proper CAN transceiver (TJA1050 or similar)
- [ ] Module powered (LED on if present)

#### 3. **Inverter Bus**
- [ ] 120Ω termination at both ends of bus
- [ ] CAN-H and CAN-L not swapped
- [ ] Twisted pair cable used
- [ ] Bus speed matches inverter (500 kbps)

#### 4. **Web UI Diagnostics**
- Check the State card for "CAN2 (Inverter): OK/FAIL"
- Check `/api/status` for:
  - `mcp2515`: 1 = OK, 0 = FAIL
  - `mcp2515tx`: TX frame count (should increase every second when inverter protocol active)
  - `mcp2515rx`: RX frame count (increases if inverter sends data)
  - `mcp2515err`: Error count (should stay at 0)

### Serial Debug Output
On boot you should see:
```
✅ MCP2515 loopback OK
✅ Bus CAN2 (Inverter) activo - 500 kbps via MCP2515
```

If you see:
```
⚠️ MCP2515 loopback FAIL - check SPI wiring
❌ MCP2515 init FAIL
```
→ The SPI communication is not working. Recheck all 5 SPI wires.

---

## MQTT Connection Issues

### Symptoms
- MQTT status shows "disconnected" in web UI
- No data in Home Assistant or Grafana
- Serial monitor shows MQTT errors

### Checks

#### 1. **Broker Reachability**
- [ ] Ping the broker IP from another device
- [ ] Verify port is correct (1883 plain, 8883 TLS)
- [ ] Check firewall rules on broker host

#### 2. **Credentials**
- [ ] Verify user/password in `/config` MQTT section
- [ ] Test with `mosquitto_sub -h <broker> -u <user> -P <pass> -t 'bms/#' -v`
- [ ] If using TLS, verify certificates in LittleFS (`mqtt_ca.pem`)

#### 3. **Configuration**
- [ ] Check "No TLS" is enabled for local/insecure connections
- [ ] Ensure endpoint is hostname or IP (no `mqtt://` prefix)
- [ ] Save settings and reboot if changes were made

#### 4. **Serial Debug**
Look for these messages:
```
MQTT connected to broker:1883
MQTT publish: bms/jkbms001/telemetry
```

If you see repeated reconnect attempts with exponential backoff, the broker is rejecting the connection.

### MQTT has zero impact when not configured — if endpoint is empty, MQTT is completely disabled.

---

## OTA Update Issues

### Symptoms
- OTA command via MQTT has no effect
- Update fails or ESP32 crashes after update

### Checks

#### 1. **Safety Check**
- OTA is **refused if contactors are closed** (armed state). Disarm first.

#### 2. **URL and MD5**
- OTA command format: `{"url":"https://...firmware.bin","md5":"abc123..."}`
- URL must be HTTPS and reachable from ESP32's network
- MD5 must match the firmware binary exactly

#### 3. **Partition Size**
- Firmware binary must fit in the OTA partition
- Check serial monitor for size errors

---

## Advanced Debugging

### Enable Verbose Logging

Add to `setup()`:
```cpp
Serial.setDebugOutput(true);
esp_log_level_set("*", ESP_LOG_VERBOSE);
```

### CAN Sniffer

Monitor all CAN traffic:
```cpp
void canTask(void* param) {
  while(1) {
    twai_message_t msg;
    if (twai_receive(&msg, pdMS_TO_TICKS(1000)) == ESP_OK) {
      Serial.printf("RX ID:0x%03X [%d] ", msg.identifier, msg.data_length_code);
      for (int i=0; i<msg.data_length_code; i++) {
        Serial.printf("%02X ", msg.data[i]);
      }
      Serial.println();
    }
  }
}
```

### Watchdog Disable (for debugging only)

```cpp
disableCore0WDT();
disableCore1WDT();
```

**Warning:** Only for debugging! Re-enable before production use.

---

## Getting Help

If you can't solve the issue:

1. **Check Serial Output**
   - Connect USB and open serial monitor at 115200 baud
   - Copy full output (from boot to fault)

2. **Gather Info**
   - Firmware version
   - Hardware (T-CAN485, relay board model, etc.)
   - Battery configuration (voltage, chemistry, cell count)
   - Exact fault message

3. **Report Issue**
   - GitHub: [Create Issue](https://github.com/ramdoor/JK_BMS_Master/issues)
   - Include serial log, wiring diagram, configuration

4. **Community Support**
   - GitHub Discussions
   - JK BMS forums
   - ESP32 forums

---

**Safety Reminder:** If troubleshooting reveals unexpected behavior with contactors or protection logic, **DO NOT connect to live battery** until issue is resolved. Always test with low-voltage bench setup first.
