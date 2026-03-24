# Hardware Setup Guide
# JK BMS T-2CAN Controller (Lilygo T-2CAN / ESP32-S3)

This guide covers the physical hardware setup and wiring for the JK BMS T-2CAN Controller system.

---

## Table of Contents
1. [Bill of Materials](#bill-of-materials)
2. [Lilygo T-2CAN Board](#lilygo-t-2can-board)
3. [Pin Configuration](#pin-configuration)
4. [CAN Bus Wiring](#can-bus-wiring)
5. [Contactor Connections](#contactor-connections)
6. [Power Supply](#power-supply)
7. [Differences from T-CAN485](#differences-from-t-can485)
8. [Safety Considerations](#safety-considerations)
9. [Testing & Verification](#testing--verification)

---

## 1. Bill of Materials

### Core Components

| Item | Specification | Qty | Notes |
|------|--------------|-----|-------|
| **Lilygo T-2CAN** | ESP32-S3, 16MB Flash, 8MB PSRAM | 1 | Dual CAN bus board |
| **JK BMS Modules** | JK-BMS with CAN | 1-10 | Model with CAN bus support |
| **Current Sensor** | AHBC-CANB | 1 | Bidirectional hall-effect sensor |
| **Contactors/Relays** | High-current DC relays | 3 | Precharge, Charge, Discharge |
| **Relay Module** | 4-channel relay board | 1 | For GPIO contactor control |
| **Power Supply** | 5V or 12V DC, 1A min | 1 | For board and relays |
| **CAN Terminators** | 120 ohm resistors | 2+2 | One pair per CAN bus |
| **USB-C Cable** | Data-capable | 1 | For programming and serial |
| **Wire** | 18-20 AWG (CAN, GPIO) | - | Twisted pair for CAN |
| **Wire** | Battery cable sized for load | - | For contactors |
| **Fuses** | Appropriate for battery current | 2+ | Critical safety component |

---

## 2. Lilygo T-2CAN Board

The Lilygo T-2CAN features:
- **ESP32-S3** with 16MB Flash and 8MB PSRAM
- **Built-in TWAI** CAN transceiver (always enabled, no EN/SPEED pins needed)
- **MCP2515** CAN controller with **16 MHz crystal** via SPI
- **USB-C** connector for power, programming, and serial (CDC)
- **No external USB-UART chip** needed (native USB on ESP32-S3)

---

## 3. Pin Configuration

### Complete Pin Map

| GPIO | Function | Connection | Notes |
|------|----------|------------|-------|
| 7 | CAN1 TX (TWAI) | Internal transceiver | BMS bus, 250 kbps |
| 6 | CAN1 RX (TWAI) | Internal transceiver | BMS bus, 250 kbps |
| 10 | MCP2515 CS | SPI Chip Select | Inverter bus |
| 12 | MCP2515 SCK | SPI Clock | Inverter bus |
| 11 | MCP2515 MOSI | SPI Master Out | Inverter bus |
| 13 | MCP2515 MISO | SPI Master In | Inverter bus |
| 9 | MCP2515 RST | Hardware Reset | Active LOW pulse on boot |
| 14 | Precharge Contactor | Relay IN1 | Active HIGH (configurable) |
| 15 | Charge Contactor | Relay IN2 | Active HIGH (configurable) |
| 16 | Discharge Contactor | Relay IN3 | Active HIGH (configurable) |
| 0 | Main Contactor | Reserved | Currently unused |

### Wiring Diagram

```
+------------------------------------------------------------------+
|                      Lilygo T-2CAN Board                          |
+------------------------------------------------------------------+
|                                                                    |
|  [USB-C] ---- Power + Serial (CDC)                                |
|                                                                    |
|  GPIO 7  (TWAI TX) ----+                                          |
|  GPIO 6  (TWAI RX) ----+--- CAN Bus 1 (BMS, 250 kbps)            |
|                         |    [120 ohm] at each end                 |
|                         +--- JK BMS Module 1                       |
|                         +--- JK BMS Module 2                       |
|                         +--- ...                                   |
|                         +--- AHBC-CANB Current Sensor              |
|                                                                    |
|  GPIO 10 (CS)   ---+                                              |
|  GPIO 12 (SCK)  ---+                                              |
|  GPIO 11 (MOSI) ---+--- MCP2515 (on-board)                        |
|  GPIO 13 (MISO) ---+    CAN Bus 2 (Inverter, 500 kbps)            |
|  GPIO 9  (RST)  ---+    [120 ohm] at each end                     |
|                          +--- Inverter (Ingeteam/SMA/Victron/BYD)  |
|                                                                    |
|  GPIO 14 ------- Relay Module IN1 --- Precharge Contactor          |
|  GPIO 15 ------- Relay Module IN2 --- Charge Contactor             |
|  GPIO 16 ------- Relay Module IN3 --- Discharge Contactor          |
|                                                                    |
|  GND ----------- Relay Module GND --- CAN GND                     |
|  5V/3.3V ------- Relay Module VCC                                  |
|                                                                    |
+------------------------------------------------------------------+
```

### MCP2515 Details (On-Board)

The T-2CAN board has the MCP2515 built-in with a **16 MHz crystal**. Key differences from external MCP2515 modules:

- **Crystal**: 16 MHz (firmware uses `MCP_16MHZ`)
- **RST pin**: GPIO 9 (firmware pulses LOW on boot for clean reset)
- **No INT pin**: RX uses `checkReceive()` SPI polling instead of interrupt
- **SPI bus**: Default ESP32-S3 SPI (not HSPI/VSPI)

---

## 4. CAN Bus Wiring

### CAN Bus 1 (TWAI, 250 kbps) - BMS + Current Sensor

```
[120 ohm] --- CAN-H --+--+--+--+-- [120 ohm]
                       |  |  |  |
[120 ohm] --- CAN-L --+--+--+--+-- [120 ohm]
                       |  |  |  |
                      JK  JK JK AHBC
                      #1  #2 #3 Sensor
```

### CAN Bus 2 (MCP2515, 500 kbps) - Inverter

```
[120 ohm] --- CAN-H --+-- [120 ohm]
                       |
[120 ohm] --- CAN-L --+-- [120 ohm]
                       |
                   Inverter
              (Ingeteam/SMA/BYD)
```

### Wiring Notes
- Use **twisted pair** cable for CAN buses
- **120 ohm termination** at each end of each bus
- Keep stubs (branch connections) as short as possible
- Maximum bus length: ~40m at 250 kbps

---

## 5. Contactor Connections

### GPIO to Relay Mapping

| GPIO | Relay | Function | Default Level |
|------|-------|----------|---------------|
| 14 | IN1 | Precharge Contactor | HIGH |
| 15 | IN2 | Charge Contactor | HIGH |
| 16 | IN3 | Discharge Contactor | HIGH |

If your relay board is **active-LOW**, set `CONTACTOR_ACTIVE_LOW = 1` in `pin_config.h`.

### Contactor Sequence (Automated)

**Arming:** Precharge ON -> Wait 2s -> Charge+Discharge ON -> Precharge OFF

**Disarming:** Charge OFF -> Discharge OFF -> Precharge OFF

---

## 6. Power Supply

The T-2CAN board is powered via **USB-C** (5V). For the relay module, you may need a separate 5V or 12V supply depending on the relay coil voltage.

---

## 7. Differences from T-CAN485

| Aspect | T-CAN485 (ESP32) | T-2CAN (ESP32-S3) |
|--------|-------------------|---------------------|
| MCU | ESP32 | ESP32-S3 (16MB flash, 8MB PSRAM) |
| TWAI pins | TX=27, RX=26 | TX=7, RX=6 |
| Transceiver | ME2107 (EN=16, SPEED=23) | Built-in (always enabled) |
| MCP2515 SPI | HSPI (CS=5, SCK=18, MOSI=12, MISO=35) | Default SPI (CS=10, SCK=12, MOSI=11, MISO=13) |
| MCP2515 crystal | 8 MHz | 16 MHz |
| MCP2515 RX | INT pin (GPIO34) | `checkReceive()` polling |
| MCP2515 RST | Not wired | GPIO 9 |
| Contactors | PRE=25, CHG=32, DSG=33 | PRE=14, CHG=15, DSG=16 |
| Serial | UART (USB-to-Serial chip) | USB CDC (native) |
| Power input | 12V barrel/screw terminal | USB-C (5V) |
| Flash size | 4MB typical | 16MB |

---

## 8. Safety Considerations

### Critical Safety Rules

1. **High Voltage/Current**: LiFePO4 packs can be 48-51.2V nominal. Always use insulated tools, safety glasses, and gloves.
2. **Fusing**: Install fuses/breakers between battery and contactors. Size for maximum inverter current + 20% margin.
3. **Contactor Ratings**: Use DC-rated contactors (AC contactors may fail on DC). Ensure voltage and current ratings exceed your system.
4. **Precharge**: ALWAYS use precharge to prevent welding contactors. Verify precharge resistor wattage.
5. **Emergency Disconnect**: Install a manual emergency stop or disconnect.

### Testing Checklist

- [ ] All wiring double-checked against pin map
- [ ] Fuses installed and rated correctly
- [ ] CAN bus terminated at both ends (both buses)
- [ ] USB-C connection stable
- [ ] Firmware uploaded and serial output normal
- [ ] Web UI accessible
- [ ] Protection limits configured correctly
- [ ] Manual emergency stop tested

---

## 9. Testing & Verification

### Initial Power-Up

1. **Connect USB-C** to T-2CAN board
2. **Open serial monitor** (`pio device monitor`, 115200 baud):
   ```
   Iniciando JK BMS + Contactores...
   Conectando a WiFi...
   WiFi OK. IP: 192.168.1.XXX
   Bus CAN1 (BMS) activo - 250 kbps
   MCP2515 loopback OK
   Bus CAN2 (Inverter) activo - 500 kbps via MCP2515
   Servidor web listo
   ```
3. **Access web UI** at displayed IP address

### CAN Bus Testing

1. Connect JK BMS modules to CAN bus 1
2. Check web UI for active module IDs and cell voltages
3. Connect inverter to CAN bus 2
4. Select inverter protocol in configuration page

---

## Additional Resources

- **Lilygo T-2CAN**: [GitHub Repository](https://github.com/Xinyuan-LilyGO/T-2CAN)
- **Original T-CAN485 version**: [JK_BMS_Master](https://github.com/ramdoor/JK_BMS_Master)
- **JK BMS Manual**: Refer to manufacturer docs for CAN protocol
- **AHBC-CANB Manual**: Sensor configuration and CAN format

---

**DISCLAIMER**: Working with high-voltage battery systems is dangerous. This guide is for informational purposes only. The author assumes no liability for injury, death, or property damage resulting from the use of this information. Always consult a qualified electrician or engineer.
