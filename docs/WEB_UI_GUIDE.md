# Web UI User Guide / Guía de Usuario / Benutzerhandbuch

---

# English

## Overview

The JK BMS Master web interface provides real-time monitoring and configuration of your battery management system. Access it via any web browser on your local network.

## Accessing the Interface

1. Connect to the same WiFi network as the ESP32
2. Open a browser and navigate to the ESP32's IP address (e.g., `http://192.168.1.111`)
3. Enter credentials when prompted:
   - **Admin**: `admin` / `Renovables!` (full access)
   - **User**: `user` / `1234` (read-only)

## Status Page

The main status page displays real-time battery information:

### Alert Banner
- **Green (OK)**: No active faults
- **Red (TRIPPED)**: Critical fault detected - system has tripped
- **Yellow (WARNING)**: Non-critical fault detected

### KPI Cards

| Card | Information |
|------|-------------|
| **State** | Armed status, contactor state, active BMS IDs, charge/discharge permissions, CAN2 (Inverter) status |
| **Contactors** | Status of precharge, charge, and discharge contactors with GPIO pins |
| **Pack Summary** | Total voltage, min/max cell voltages, delta, average temperature, active cell count |
| **Energy** | State of Charge (SoC), State of Health (SoH), cycle count, current flow |

### Cell Voltage Grid

Displays individual cell voltages in a color-coded grid:
- **Green**: Voltage >= 3.45V (healthy)
- **Yellow**: Voltage 3.20V - 3.45V (caution)
- **Red**: Voltage < 3.20V (low)

Hover over a cell to see: BMS ID, local cell number, and global cell number.

### Real-Time Charts

Three rolling charts with server-side history (720 points, ~1 hour at 5-second intervals):
- **Pack Voltage (V)**: Blue line
- **Current (A)**: Green line
- **State of Charge (%)**: Amber line

Use the **range selector** (5/15/30/60 min) to choose the displayed time window. Click **"Load History"** to load historical data stored on the device.

### Action Buttons

| Button | Function |
|--------|----------|
| **Arm** | Enable contactor control - starts precharge sequence |
| **Disarm** | Disable contactor control - opens all contactors |
| **Reset Trip** | Clear fault state and return to normal operation |

## Configuration Page

Access via the **Settings** button (admin only).

### Quick Settings
- **Language**: Switch between Spanish (ES), English (EN), and German (DE)
- **Chemistry**: Apply preset limits for LFP or NMC cells

### Limits Configuration

| Parameter | Description |
|-----------|-------------|
| Cell OV | Cell overvoltage threshold (V) |
| Cell UV | Cell undervoltage threshold (V) |
| Delta Max | Maximum allowed cell voltage difference (V) |
| Temp Min/Max | Operating temperature range (°C) |
| Pack OV/UV | Pack-level voltage limits (V) |
| Comm Timeout | BMS communication timeout (ms) |
| Precharge Time | Precharge duration (ms) |
| Expected Cells | Expected total cell count, 0=disabled |

### Energy Configuration

| Parameter | Description |
|-----------|-------------|
| Capacity (Ah) | Nominal battery capacity |
| SoH (Ah) | State of Health capacity |
| Initial SoC | Starting SoC percentage |
| Current Polarity | Current sensor polarity (+1/-1) |
| Efficiency | Coulomb counting efficiency (0.90-1.00) |

### Auto-Learn Configuration

| Parameter | Description |
|-----------|-------------|
| Full Voltage | Cell voltage considered "full" |
| Empty Voltage | Cell voltage considered "empty" |
| Tail Current | Current threshold for full detection |
| Hold Time | Stable time required for detection |

#### How SoH Auto-Learning Works

The system automatically learns the real battery capacity (SoH Ah) by measuring a complete full-to-empty discharge cycle:

1. **FULL detection**: When the maximum cell voltage reaches the "Full Voltage" threshold AND current drops below "Tail Current" AND this condition holds stable for "Hold Time" (default 2 minutes), the system calibrates SoC to 100%, confirms full state, and begins counting discharged Ah.
2. **Discharge counting**: While the learning phase is active, every discharge sample is accumulated into `learnDischargeAh` (shown as "Learn dAh" on the status page).
3. **EMPTY detection**: When the minimum cell voltage drops to "Empty Voltage" AND current is below "Tail Current" for "Hold Time", the system calibrates SoC to 0%.
4. **Capacity update**: The accumulated discharge Ah is clamped to 10%–150% of the nominal capacity and saved as the new SoH (Ah). This value persists across reboots.

**SoH percentage** is calculated as: `(SoH Ah / Nominal Capacity Ah) × 100%`. A value below 100% indicates degraded capacity.

**SoC cap**: Until `applyFullCalibration()` confirms a real FULL event, SoC sent to the inverter is capped at 99% to prevent false 100% readings.

The learning state (`IDLE` or `RUNNING`) and the current `Learn dAh` counter are visible on the status page under the Energy section. Both values survive reboots via NVS persistence.

### Tapering & Ratchet

| Parameter | Description |
|-----------|-------------|
| Taper Hold (ms) | Time to hold at CCL/DCL=0 before allowing recovery (default 15000ms) |

CCL/DCL are gradually reduced as cell voltages approach OV/UV limits. The ratchet ensures values drop instantly but only recover slowly, preventing oscillation at end of charge/discharge.

### Night Mode

| Parameter | Description |
|-----------|-------------|
| Enable | Toggle night mode on/off |
| Start Hour | Hour when night mode begins (default 23) |
| End Hour | Hour when night mode ends (default 7) |
| CCL % | Charge current limit during night as percentage (default 50%) |
| DCL % | Discharge current limit during night as percentage (default 100%) |

### MQTT Settings

| Parameter | Description |
|-----------|-------------|
| Endpoint | MQTT broker hostname or IP |
| Port | MQTT port (1883 plain, 8883 TLS) |
| No TLS | Disable TLS for local/insecure connections |
| User | MQTT username (optional) |
| Password | MQTT password (shown as **** if set) |

Connection status is shown on the Status page. MQTT settings are persisted in NVS.

### Inverter Protocol

Configure CAN communication with compatible inverters. All inverter protocols use the second CAN bus (MCP2515 at 500 kbps). Supported: SMA/Ingeteam (0x35x, verified working), BYD HVS, Pylon HV/LV.

The State card shows "CAN2 (Inverter): OK/FAIL" to indicate MCP2515 status.

### Balance Health Indicator

The status page shows a color-coded badge next to the delta voltage:
- **Excellent** (green): Delta < 10mV
- **Good** (blue): Delta 10-30mV
- **Fair** (yellow): Delta 30-50mV
- **Poor** (orange): Delta 50-80mV
- **Critical** (red): Delta > 80mV

A weak cell warning banner appears when a single cell deviates significantly. Balance time estimate is shown when balancing is active.

### Event Log

The status page includes a persistent event log (200 entries in LittleFS). Events include contactor changes, faults, state transitions, and learning milestones. Logs survive reboots. Use "Clear log" to reset.

## API Endpoints

| Endpoint | Description |
|----------|-------------|
| `GET /api/status` | Real-time battery data (JSON) |
| `GET /api/history` | Historical data buffer (720 points, JSON) |
| `GET /api/events` | Persistent event log (JSON) |
| `GET /api/rev` | Current revision number |

### MCP2515 fields in /api/status
| Field | Description |
|-------|-------------|
| `mcp2515` | 1 = OK, 0 = FAIL |
| `mcp2515tx` | Frames transmitted count |
| `mcp2515rx` | Frames received count |
| `mcp2515err` | Error count |

---

# Español

## Descripción General

La interfaz web de JK BMS Master proporciona monitoreo en tiempo real y configuración de su sistema de gestión de baterías. Acceda desde cualquier navegador en su red local.

## Acceso a la Interfaz

1. Conéctese a la misma red WiFi que el ESP32
2. Abra un navegador y vaya a la dirección IP del ESP32 (ej: `http://192.168.1.111`)
3. Ingrese las credenciales cuando se solicite:
   - **Admin**: `admin` / `Renovables!` (acceso completo)
   - **Usuario**: `user` / `1234` (solo lectura)

## Página de Estado

La página principal muestra información de la batería en tiempo real:

### Banner de Alerta
- **Verde (OK)**: Sin fallos activos
- **Rojo (TRIPPED)**: Fallo crítico detectado - sistema disparado
- **Amarillo (WARNING)**: Fallo no crítico detectado

### Tarjetas KPI

| Tarjeta | Información |
|---------|-------------|
| **Estado** | Estado de armado, contactores, IDs de BMS activos, permisos de carga/descarga, estado CAN2 (Inversor) |
| **Contactores** | Estado de contactores de precarga, carga y descarga con pines GPIO |
| **Resumen Batería** | Voltaje total, voltajes de celda mín/máx, delta, temperatura promedio, celdas activas |
| **Energía** | Estado de Carga (SoC), Estado de Salud (SoH), ciclos, corriente |

### Cuadrícula de Voltajes de Celda

Muestra voltajes individuales de celda con código de colores:
- **Verde**: Voltaje >= 3.45V (saludable)
- **Amarillo**: Voltaje 3.20V - 3.45V (precaución)
- **Rojo**: Voltaje < 3.20V (bajo)

Pase el cursor sobre una celda para ver: ID de BMS, número de celda local y número de celda global.

### Gráficos en Tiempo Real

Tres gráficos con historial del servidor (720 puntos, ~1 hora a intervalos de 5 segundos):
- **Voltaje del Pack (V)**: Línea azul
- **Corriente (A)**: Línea verde
- **Estado de Carga (%)**: Línea ámbar

Use el **selector de rango** (5/15/30/60 min) para elegir la ventana de tiempo. Haga clic en **"Load History"** para cargar datos históricos del dispositivo.

### Botones de Acción

| Botón | Función |
|-------|---------|
| **Armar** | Habilitar control de contactores - inicia secuencia de precarga |
| **Desarmar** | Deshabilitar control de contactores - abre todos los contactores |
| **Reset Trip** | Limpiar estado de fallo y volver a operación normal |

## Página de Configuración

Acceda mediante el botón **Configuración** (solo admin).

### Configuración Rápida
- **Idioma**: Cambiar entre Español (ES), Inglés (EN) y Alemán (DE)
- **Química**: Aplicar límites preestablecidos para celdas LFP o NMC

### Configuración de Límites

| Parámetro | Descripción |
|-----------|-------------|
| Cell OV | Umbral de sobrevoltaje de celda (V) |
| Cell UV | Umbral de subvoltaje de celda (V) |
| Delta Max | Diferencia máxima permitida entre celdas (V) |
| Temp Min/Max | Rango de temperatura de operación (°C) |
| Pack OV/UV | Límites de voltaje a nivel de pack (V) |
| Comm Timeout | Timeout de comunicación con BMS (ms) |
| Precharge Time | Duración de precarga (ms) |
| Celdas esperadas | Número de celdas esperado, 0=deshabilitado |

### Configuración de Energía

| Parámetro | Descripción |
|-----------|-------------|
| Capacidad (Ah) | Capacidad nominal de la batería |
| SoH (Ah) | Capacidad de Estado de Salud |
| SoC Inicial | Porcentaje de SoC inicial |
| Polaridad Corriente | Polaridad del sensor de corriente (+1/-1) |
| Eficiencia | Eficiencia de conteo Coulomb (0.90-1.00) |

### Configuración de Auto-Aprendizaje

| Parámetro | Descripción |
|-----------|-------------|
| Voltaje Lleno | Voltaje de celda considerado "lleno" |
| Voltaje Vacío | Voltaje de celda considerado "vacío" |
| Corriente de Cola | Umbral de corriente para detección de lleno |
| Tiempo de Retención | Tiempo estable requerido para detección |

#### Cómo funciona el auto-aprendizaje de SoH

El sistema aprende automáticamente la capacidad real de la batería (SoH Ah) midiendo un ciclo completo de descarga de lleno a vacío:

1. **Detección de LLENO**: Cuando el voltaje máximo de celda alcanza el umbral "Voltaje Lleno" Y la corriente cae por debajo de "Corriente de Cola" Y esta condición se mantiene estable durante "Tiempo de Retención" (por defecto 2 minutos), el sistema calibra el SoC a 100%, confirma estado lleno, y comienza a contar los Ah descargados.
2. **Conteo de descarga**: Mientras la fase de aprendizaje está activa, cada muestra de descarga se acumula en `learnDischargeAh` (mostrado como "Learn dAh" en la página de estado).
3. **Detección de VACÍO**: Cuando el voltaje mínimo de celda baja hasta "Voltaje Vacío" Y la corriente está por debajo de "Corriente de Cola" durante "Tiempo de Retención", el sistema calibra el SoC a 0%.
4. **Actualización de capacidad**: Los Ah de descarga acumulados se limitan al rango 10%–150% de la capacidad nominal y se guardan como el nuevo SoH (Ah). Este valor persiste entre reinicios.

**El porcentaje de SoH** se calcula como: `(SoH Ah / Capacidad Nominal Ah) × 100%`. Un valor inferior al 100% indica capacidad degradada.

El estado de aprendizaje (`IDLE` o `RUNNING`) y el contador actual de `Learn dAh` son visibles en la página de estado bajo la sección de Energía. Ambos valores sobreviven reinicios gracias a la persistencia en NVS.

### Protocolo de Inversor

Configure la comunicación CAN con inversores compatibles. Todos los protocolos de inversor usan el segundo bus CAN (MCP2515 a 500 kbps). Soportados: SMA/Ingeteam (0x35x, verificado en producción), BYD HVS, Pylon HV/LV.

La tarjeta de Estado muestra "CAN2 (Inverter): OK/FAIL" para indicar el estado del MCP2515.

## Endpoints de API

| Endpoint | Descripción |
|----------|-------------|
| `GET /api/status` | Datos de batería en tiempo real (JSON) |
| `GET /api/history` | Buffer de datos históricos (720 puntos, JSON) |
| `GET /api/events` | Log de eventos persistente (JSON) |
| `GET /api/rev` | Número de revisión actual |

---

# Deutsch

## Übersicht

Die JK BMS Master Web-Oberfläche bietet Echtzeit-Überwachung und Konfiguration Ihres Batterie-Management-Systems. Zugriff über jeden Webbrowser in Ihrem lokalen Netzwerk.

## Zugriff auf die Oberfläche

1. Verbinden Sie sich mit dem gleichen WiFi-Netzwerk wie der ESP32
2. Öffnen Sie einen Browser und navigieren Sie zur IP-Adresse des ESP32 (z.B. `http://192.168.1.111`)
3. Geben Sie die Anmeldedaten ein:
   - **Admin**: `admin` / `Renovables!` (Vollzugriff)
   - **Benutzer**: `user` / `1234` (nur Lesen)

## Statusseite

Die Hauptstatusseite zeigt Echtzeit-Batterieinformationen:

### Alarm-Banner
- **Grün (OK)**: Keine aktiven Fehler
- **Rot (TRIPPED)**: Kritischer Fehler erkannt - System ausgelöst
- **Gelb (WARNING)**: Nicht-kritischer Fehler erkannt

### KPI-Karten

| Karte | Information |
|-------|-------------|
| **Status** | Scharfschaltung, Schützstatus, aktive BMS-IDs, Lade-/Entladefreigaben, CAN2 (Wechselrichter) Status |
| **Schütze** | Status von Vorlade-, Lade- und Entladeschützen mit GPIO-Pins |
| **Pack-Übersicht** | Gesamtspannung, Min/Max Zellspannungen, Delta, Durchschnittstemperatur, aktive Zellen |
| **Energie** | Ladezustand (SoC), Gesundheitszustand (SoH), Zyklen, Stromfluss |

### Zellspannungs-Raster

Zeigt einzelne Zellspannungen in einem farbcodierten Raster:
- **Grün**: Spannung >= 3,45V (gesund)
- **Gelb**: Spannung 3,20V - 3,45V (Vorsicht)
- **Rot**: Spannung < 3,20V (niedrig)

Bewegen Sie die Maus über eine Zelle um zu sehen: BMS-ID, lokale Zellnummer und globale Zellnummer.

### Echtzeit-Diagramme

Drei Diagramme mit serverseitigem Verlauf (720 Punkte, ~1 Stunde bei 5-Sekunden-Intervallen):
- **Pack-Spannung (V)**: Blaue Linie
- **Strom (A)**: Grüne Linie
- **Ladezustand (%)**: Bernsteinfarbene Linie

Verwenden Sie den **Bereichswähler** (5/15/30/60 Min) für das Zeitfenster. Klicken Sie auf **"Load History"** um historische Daten vom Gerät zu laden.

### Aktionsschaltflächen

| Schaltfläche | Funktion |
|--------------|----------|
| **Arm** | Schützsteuerung aktivieren - startet Vorladesequenz |
| **Disarm** | Schützsteuerung deaktivieren - öffnet alle Schütze |
| **Reset Trip** | Fehlerstatus löschen und zum Normalbetrieb zurückkehren |

## Konfigurationsseite

Zugriff über die Schaltfläche **Einstellungen** (nur Admin).

### Schnelleinstellungen
- **Sprache**: Wechseln zwischen Spanisch (ES), Englisch (EN) und Deutsch (DE)
- **Chemie**: Voreingestellte Grenzwerte für LFP- oder NMC-Zellen anwenden

### Grenzwert-Konfiguration

| Parameter | Beschreibung |
|-----------|--------------|
| Cell OV | Zell-Überspannungsschwelle (V) |
| Cell UV | Zell-Unterspannungsschwelle (V) |
| Delta Max | Maximal zulässige Zellspannungsdifferenz (V) |
| Temp Min/Max | Betriebstemperaturbereich (°C) |
| Pack OV/UV | Pack-Spannungsgrenzen (V) |
| Comm Timeout | BMS-Kommunikations-Timeout (ms) |
| Precharge Time | Vorladezeit (ms) |
| Erwartete Zellen | Erwartete Zellanzahl, 0=deaktiviert |

### Energie-Konfiguration

| Parameter | Beschreibung |
|-----------|--------------|
| Kapazität (Ah) | Nennkapazität der Batterie |
| SoH (Ah) | Gesundheitszustand-Kapazität |
| Initial SoC | Start-SoC-Prozentsatz |
| Strompolarität | Stromsensor-Polarität (+1/-1) |
| Effizienz | Coulomb-Zähler-Effizienz (0,90-1,00) |

### Auto-Lern-Konfiguration

| Parameter | Beschreibung |
|-----------|--------------|
| Voll-Spannung | Zellspannung die als "voll" gilt |
| Leer-Spannung | Zellspannung die als "leer" gilt |
| Reststrom | Stromschwelle für Voll-Erkennung |
| Haltezeit | Erforderliche stabile Zeit für Erkennung |

#### Wie das SoH-Auto-Lernen funktioniert

Das System lernt automatisch die tatsächliche Batteriekapazität (SoH Ah), indem es einen vollständigen Entladezyklus von voll bis leer misst:

1. **VOLL-Erkennung**: Wenn die maximale Zellspannung den Schwellenwert "Voll-Spannung" erreicht UND der Strom unter "Reststrom" fällt UND diese Bedingung für "Haltezeit" (Standard 2 Minuten) stabil bleibt, kalibriert das System den SoC auf 100%, bestätigt den Vollstatus, und beginnt die entladenen Ah zu zählen.
2. **Entladezählung**: Während die Lernphase aktiv ist, wird jede Entladeprobe in `learnDischargeAh` akkumuliert (als "Learn dAh" auf der Statusseite angezeigt).
3. **LEER-Erkennung**: Wenn die minimale Zellspannung auf "Leer-Spannung" sinkt UND der Strom für "Haltezeit" unter "Reststrom" liegt, kalibriert das System den SoC auf 0%.
4. **Kapazitätsaktualisierung**: Die akkumulierten Entlade-Ah werden auf 10%–150% der Nennkapazität begrenzt und als neuer SoH (Ah) gespeichert. Dieser Wert bleibt über Neustarts erhalten.

**Der SoH-Prozentsatz** wird berechnet als: `(SoH Ah / Nennkapazität Ah) × 100%`. Ein Wert unter 100% zeigt eine verminderte Kapazität an.

Der Lernstatus (`IDLE` oder `RUNNING`) und der aktuelle `Learn dAh`-Zähler sind auf der Statusseite im Energiebereich sichtbar. Beide Werte überleben Neustarts dank NVS-Persistenz.

### Wechselrichter-Protokoll

Konfigurieren Sie die CAN-Kommunikation mit kompatiblen Wechselrichtern. Alle Wechselrichter-Protokolle verwenden den zweiten CAN-Bus (MCP2515 bei 500 kbps). Unterstützt: SMA/Ingeteam (0x35x, in Produktion verifiziert), BYD HVS, Pylon HV/LV.

Die Statuskarte zeigt "CAN2 (Inverter): OK/FAIL" für den MCP2515-Status.

## API-Endpunkte

| Endpunkt | Beschreibung |
|----------|--------------|
| `GET /api/status` | Echtzeit-Batteriedaten (JSON) |
| `GET /api/history` | Historischer Datenpuffer (720 Punkte, JSON) |
| `GET /api/events` | Persistentes Ereignisprotokoll (JSON) |
| `GET /api/rev` | Aktuelle Revisionsnummer |

---

## API Response Examples / Ejemplos de Respuesta API / API-Antwortbeispiele

### /api/status
```json
{
  "rev": 123,
  "armed": 0,
  "state": "OPEN",
  "ids": "1,2",
  "packV": 181.03,
  "minCell": 3.760,
  "maxCell": 3.783,
  "delta": 0.023,
  "tavg": 17.0,
  "cellsActive": 48,
  "expectedCells": 48,
  "soc": 76.0,
  "currentA": 0.000,
  "mcp2515": 1,
  "mcp2515tx": 1234,
  "mcp2515rx": 56,
  "mcp2515err": 0,
  "taperChg": 1.00,
  "taperDis": 1.00,
  "balTimeH": 2.50,
  "nightActive": 0,
  "learnPhase": "IDLE",
  "learnDAh": 0.00,
  "fullConfirmed": 0,
  "pylonTx": "CVL=876 DVL=672 CCL=100 DCL=100 T=17",
  "cells": [
    {"g": 1, "id": 1, "l": 1, "v": 3.762},
    {"g": 2, "id": 1, "l": 2, "v": 3.762}
  ]
}
```

### /api/history
```json
{
  "points": [
    {"ts": 5, "v": 181.04, "a": 0.00, "soc": 76.0, "min": 3.761, "max": 3.783, "t": 17.0},
    {"ts": 10, "v": 181.04, "a": 0.00, "soc": 76.0, "min": 3.761, "max": 3.783, "t": 17.0}
  ],
  "count": 720
}
```

---

*Documentation version: 1.2 | Last updated: 2026-03-24*
