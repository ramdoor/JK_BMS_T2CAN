# Quick Start Guide
# JK BMS T-2CAN Controller (Lilygo T-2CAN / ESP32-S3)

Get up and running in 15 minutes!

---

## Prerequisites

- **Hardware:**
  - Lilygo T-2CAN board (ESP32-S3)
  - USB-C cable (data-capable)

- **Software:**
  - PlatformIO (recommended)
  - No additional USB drivers needed (ESP32-S3 native USB CDC)

---

## Step 1: Clone or Download

```bash
git clone https://github.com/ramdoor/JK_BMS_T2CAN.git
cd JK_BMS_T2CAN
```

Or download ZIP from GitHub and extract.

---

## Step 2: Configure WiFi

Edit `src/main.cpp` (lines 26-27):

```cpp
const char* ssid = "YourWiFiName";
const char* password = "YourWiFiPassword";
```

**Optional:** Change admin password (lines 30-31):

```cpp
static const char* USER_ADMIN = "admin";
static const char* PASS_ADMIN = "YourSecurePassword";
```

---

## Step 3: Build and Upload

### PlatformIO (Recommended)

```bash
# Install PlatformIO if not installed
pip install platformio

# Build and upload (T-2CAN board via USB-C)
pio run -e t2can --target upload
```

---

## Step 4: Connect to Web UI

1. **Open Serial Monitor** (115200 baud, USB CDC):
   ```bash
   pio device monitor
   ```
2. **Find IP address** in output:
   ```
   WiFi OK. IP: 192.168.1.XXX
   ```
3. **Open browser** and navigate to: `http://192.168.1.XXX`
4. **Login:**
   - Username: `admin`
   - Password: `Renovables!` (or your custom password)

---

## Step 5: Initial Configuration

1. **Select Language** (ES/EN/DE)
2. **Choose Chemistry:**
   - Click "Aplicar LFP" for LiFePO4 batteries
   - Click "Aplicar NMC" for NMC batteries
3. **Set Capacity:**
   - Enter your battery capacity in Ah
4. **Set Expected Cells** (optional):
   - Enter total cell count (e.g., 48 for 2x24-cell modules)
   - Leave as 0 to disable cell count validation
5. **Click "Guardar" (Save)**

---

## Step 6: Verify Operation

1. **Check Status Page:**
   - Should show "OPEN" state
   - Module IDs should appear if BMS connected
   - Cell voltages should display

2. **Test Arming (with caution):**
   - Click "Armar" (Arm)
   - Listen for contactor clicks
   - State should change to PRECHARGING -> CLOSED

3. **Disarm:**
   - Click "Desarmar" (Disarm)
   - Contactors should open

---

## Troubleshooting

**Can't connect to WiFi?**
- Check SSID and password
- Ensure 2.4GHz network (ESP32-S3 doesn't support 5GHz)
- Move board closer to router

**Serial monitor not working?**
- Ensure USB-C cable supports data (not charge-only)
- The T-2CAN uses USB CDC (native), no external drivers needed
- If using Windows, check Device Manager for COM port

**No BMS modules detected?**
- Check CAN wiring (CAN-H, CAN-L)
- Verify 120 ohm termination resistors at both bus ends
- Ensure BMS modules are powered

**MCP2515 loopback FAIL?**
- Check SPI connections (built-in on T-2CAN, should work)
- Verify firmware is built with `t2can` environment (not tcan485)

**Contactors don't click?**
- Check relay board power supply
- Verify GPIO connections (14=PRE, 15=CHG, 16=DSG)
- Check `CONTACTOR_ACTIVE_LOW` setting if inverted

For more help, see [TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md)

---

## Next Steps

- **Configure Limits:** Adjust voltage/temperature limits for your battery
- **Set Up Inverter:** Select inverter protocol in settings
- **Enable Learning:** Perform full charge/discharge for SoH calibration
- **Configure MQTT:** Set broker endpoint for remote monitoring and Home Assistant integration
- **Night Mode:** Configure CCL/DCL reduction during off-peak hours

---

## Safety Reminder

**IMPORTANT:**
- Test with low voltage (12V) before connecting high voltage batteries
- Always use properly rated contactors and fuses
- Follow wiring diagram exactly
- Keep emergency disconnect accessible

---

## Documentation

- [README.md](README.md) - Full project overview
- [CLAUDE.md](CLAUDE.md) - Architecture and development guide
- [FSD.md](docs/FSD.md) - Detailed functional specification
- [HARDWARE.md](docs/HARDWARE.md) - Complete wiring guide for T-2CAN
- [TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) - Common issues

---

**Need Help?**
- GitHub Issues: Report bugs at [JK_BMS_T2CAN](https://github.com/ramdoor/JK_BMS_T2CAN/issues)
- Original project: [JK_BMS_Master](https://github.com/ramdoor/JK_BMS_Master)
