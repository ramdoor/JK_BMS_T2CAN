\
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "driver/twai.h"
#ifdef MCP2515_CS
#include <SPI.h>
#include <mcp2515.h>
#endif
#include "pin_config.h"
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <HTTPUpdate.h>
#include "version.h"

// ===== Contactor timing =====
#ifndef CONTACTOR_MIN_TOGGLE_MS
#define CONTACTOR_MIN_TOGGLE_MS 1500UL
#endif


// ===================== WiFi =====================
const char* ssid = "Terrepower";
const char* password = "2c9Yr27hqjRoHc";

// ===================== Users / Auth =====================
static const char* USER_ADMIN = "admin";
static const char* PASS_ADMIN = "Renovables!";
static const char* USER_USER  = "user";
static const char* PASS_USER  = "1234";

// ===================== MQTT Cloud =====================
#ifndef MQTT_ENDPOINT
#define MQTT_ENDPOINT ""  // AWS IoT endpoint, set in build flags or LittleFS config
#endif
#ifndef MQTT_PORT
#define MQTT_PORT 8883
#endif
#define MQTT_TELEMETRY_INTERVAL_MS 5000
#define MQTT_RECONNECT_MIN_MS      5000
#define MQTT_RECONNECT_MAX_MS     60000

static WiFiClientSecure mqttTlsClient;
static WiFiClient mqttPlainClient;
static PubSubClient mqttClient;
static char mqttDeviceId[13]; // MAC hex (12 chars + null)
static char mqttTopicTelemetry[48];
static char mqttTopicFaults[48];
static char mqttTopicStatus[48];
static char mqttTopicAck[48];
static char mqttTopicOnline[48];
static char mqttTopicCmdPrefix[48];
static uint32_t mqttLastTelemetry = 0;
static uint32_t mqttLastReconnectAttempt = 0;
static uint32_t mqttReconnectInterval = MQTT_RECONNECT_MIN_MS;
static bool mqttEnabled = false;
static bool mqttInsecure = false;
static uint16_t mqttPort = MQTT_PORT;
static String mqttEndpoint = MQTT_ENDPOINT;
static String mqttUser = "";
static String mqttPass = "";
static String lastFaultText = "";
static String lastStateStr = "";

// ===================== AHBC-CANB current sensor =====================
// CAN IDs per manual (variant listed too)
static const uint16_t AHBC_ID1 = 0x3C2;
static const uint16_t AHBC_ID2 = 0x3C3;

// ===================== JK =====================
#define MAX_MODULES  10
#define MAX_CELLS_PER_MODULE 24

struct Module {
  bool active = false;
  float packV = 0.0f;
  float tempC = 0.0f;
  uint8_t cellCount = 0;
  float cells[MAX_CELLS_PER_MODULE] = {0};
  uint32_t lastSeenMs = 0;
};

enum Chemistry : uint8_t { CHEM_LFP = 0, CHEM_NMC = 1, CHEM_LTO = 2, CHEM_CUSTOM = 3 };
enum SysState  : uint8_t { ST_OPEN = 0, ST_PRECHARGING = 1, ST_CLOSED = 2, ST_TRIPPED = 3 };
enum Lang      : uint8_t { LANG_ES = 0, LANG_EN = 1, LANG_DE = 2 };

// Inverter protocol selection (inspired by dalathegreat/Battery-Emulator, GPL-3.0)
enum InverterProto : uint8_t {
  INV_OFF = 0,
  INV_BYD_HVS = 1,
  INV_PYLON_HV = 2,
  INV_PYLON_LV = 3,
  INV_SMA_CAN = 4,       // SMA/Victron/Ingeteam compatible (0x35x IDs)
  // Placeholders for future integration:
  INV_SOFAR = 10,
  INV_SOLAX = 11,
  INV_SUNGROW = 12,
  INV_GROWATT_HV = 13,
  INV_GROWATT_LV = 14,
  INV_FOXESS = 15,
  INV_SMA_TRIPOWER = 16
};

// ===================== MCP2515 (second CAN bus for inverter) =====================
#ifdef MCP2515_CS
static MCP2515 mcp2515(MCP2515_CS);
#endif
static bool mcp2515_ok = false;
static uint32_t mcp2515_txCount = 0;
static uint32_t mcp2515_rxCount = 0;
static uint32_t mcp2515_errCount = 0;
static uint32_t mcp2515_lastRxId = 0;  // debug: last received CAN ID (raw, with flags)
static uint8_t mcp2515_lastRxData[8] = {0};  // debug: last received data bytes
static uint8_t mcp2515_lastRxDlc = 0;
// Pylon HV state
static bool pylonUseStdIds = false;  // true when Ingeteam sends standard 0x420 instead of extended 0x4200
// Debug: Pylon HV last-sent values for web UI
static uint16_t pylonDbg_CVL = 0, pylonDbg_DVL = 0, pylonDbg_CCL = 0, pylonDbg_DCL = 0;
static int16_t pylonDbg_temp = 0;
// Debug: track all distinct CAN IDs seen on inverter bus
static uint32_t mcp2515_seenIds[16] = {0};
static uint8_t mcp2515_seenCount = 0;
static void mcp2515_trackId(uint32_t id) {
  for(uint8_t i=0; i<mcp2515_seenCount; i++) if(mcp2515_seenIds[i]==id) return;
  if(mcp2515_seenCount<16) mcp2515_seenIds[mcp2515_seenCount++]=id;
}

static InverterProto invProto = INV_OFF;
static uint32_t invLast2s=0, invLast10s=0, invLast60s=0;
static bool invStarted=false, invInitialSent=false;
static uint16_t invUserMaxChgA = 100;   // A
static uint16_t invUserMaxDisA = 100;   // A
static uint16_t invUserMaxChgV_dV = 0;  // 0 = auto
static uint16_t invUserMinDisV_dV = 0;  // 0 = auto


struct Limits {
  float cellOv = 3.65f;
  float cellUv = 2.80f;
  float deltaMax = 0.05f;
  float tempMin = 0.0f;
  float tempMax = 55.0f;
  float packOv = 0.0f;      // 0 disables
  float packUv = 0.0f;      // 0 disables
  float deltaRecov = 0.040f;   // delta must drop below this to clear fault
  float cellOvRecov = 0.020f;  // hysteresis V below cellOv to re-allow charge
  float cellUvRecov = 0.020f;  // hysteresis V above cellUv to re-allow discharge
  float packOvRecov = 0.0f;    // pack V must drop below this to clear (0=disabled)
  float packUvRecov = 0.0f;    // pack V must rise above this to clear (0=disabled)
  uint32_t commTimeoutMs = 3000;
  uint32_t prechargeMs = 2000;
  uint16_t expectedCells = 0;   // 0 = disabled
  float taperChgStartV = 3.45f;   // start reducing CCL (0=off)
  float taperChgEndV   = 3.55f;   // CCL reaches 0 (soft stop, before cellOv)
  float taperDisStartV = 3.10f;   // start reducing DCL (0=off)
  float taperDisEndV   = 2.90f;   // DCL reaches 0 (soft stop, before cellUv)
  // Ratchet tapering: slow recovery to prevent cycling at end of charge/discharge
  float taperRecoveryPctS = 2.0f; // % per second CCL/DCL can recover (2 = 50s to go 0→100%)
  uint16_t taperHoldMs = 15000;   // ms to wait at 0% before starting recovery
  float dynCvlOffsetV = 0.8f;    // Dynamic CVL: offset above packTotalV when tapering (0=off)
};

struct EnergyCfg {
  float capacityAh = 200.0f;     // nominal capacity for SoC & cycles
  float sohAh = 200.0f;          // learned/editable
  float initSocPct = 50.0f;      // first boot
  int8_t currentPolarity = +1;   // +1 => raw +mA increases SoC; -1 => invert
  float coulombEff = 0.995f;

  // Auto-learning thresholds
  float fullCellV = 3.55f;       // LFP: ~3.50-3.55, NMC: ~4.10-4.15
  float emptyCellV = 2.95f;      // LFP: ~2.90-3.00, NMC: ~3.00-3.10
  float tailCurrentA = 2.0f;     // abs current below this for full/empty validation
  uint32_t holdMs = 120000;      // 2 minutes stable for event
};

static Module modules[MAX_MODULES + 1];
static Limits limits;
static EnergyCfg ecfg;
static Chemistry chem = CHEM_LFP;
static Lang lang = LANG_ES;

static SysState state = ST_OPEN;
static bool armed = false;
static uint32_t prechargeStartMs = 0;

// Contactor states (logical)
static bool cMain=false, cPre=false, cChg=false, cDsg=false;
static uint32_t lastTogPre=0, lastTogChg=0, lastTogDsg=0;


// Aggregated pack
static float packTotalV = 0.0f;
static float packMinCellV = 999.0f;
static float packMaxCellV = 0.0f;
static float packDeltaV = 0.0f;
static float packAvgTempC = 0.0f;
static float packAvgCellV = 0.0f;
static uint16_t packActiveCells = 0;
static uint8_t packMinCellModule = 0, packMinCellIndex = 0;
static uint8_t packMaxCellModule = 0, packMaxCellIndex = 0;
static String activeIdsCsv = "";

// Power
static float power_W = 0.0f;

// Faults
static bool faultComm=false, faultTemp=false, faultOv=false, faultUv=false, faultDelta=false, faultPackOv=false, faultPackUv=false, faultCellCount=false;
static String faultText = "";

// Directional permissions
static bool allowCharge = true;
static bool allowDischarge = true;
static bool criticalFault = false;
static bool directionalFault = false;

// Current / SoC / SoH / cycles
static bool currentValid = false;
static int32_t current_mA = 0;
static uint32_t lastCurrentMs = 0;

static float socPct = 0.0f;          // 0..100
static float coulombBalance_Ah = 0.0f; // remaining Ah (0..capacityAh)
static float throughput_Ah = 0.0f;   // absolute Ah processed
static float cycleCountEq = 0.0f;    // EFC

// User-resettable Ah/kWh counters
static float user_chargeAh = 0.0f;
static float user_dischargeAh = 0.0f;
static float user_chargeWh = 0.0f;
static float user_dischargeWh = 0.0f;

// Auto-learning state
enum LearnPhase : uint8_t { LEARN_IDLE=0, LEARN_FROM_FULL=1 };
static LearnPhase learnPhase = LEARN_IDLE;
static float learnDischargeAh = 0.0f;
static uint32_t fullStableStart = 0;
static uint32_t emptyStableStart = 0;
static bool fullConfirmed = false;  // true after applyFullCalibration(), cleared when SoC < 100%

// Soft disconnect - wait for inverter to reduce current before opening contactors
#define SOFT_DISCONNECT_TIMEOUT_MS 5000
static bool softDisconnectActive = false;
static uint32_t softDisconnectStartMs = 0;
static SysState softDisconnectTargetState = ST_OPEN;
static bool softDisconnectTargetChg = false;
static bool softDisconnectTargetDsg = false;

// Balance time estimator
static float balanceCurrentA = 1.0f;  // JK BMS typical balance current (0.6-2.0A)

// Night mode - reduce CCL/DCL during specified hours
static bool nightModeEnabled = false;
static uint8_t nightStartHour = 23;   // 23:00
static uint8_t nightEndHour = 7;      // 07:00
static uint8_t nightCclPct = 50;      // 50% of normal CCL at night
static uint8_t nightDclPct = 100;     // 100% of normal DCL at night (usually no limit)

WebServer server(80);
volatile uint32_t ui_rev = 0;

Preferences prefs;

// Historical data buffer (5-second samples, ~1 hour of data)
#define HISTORY_SIZE 720
struct HistoryPoint {
  uint32_t ts;      // timestamp (millis/1000)
  float packV;
  float currentA;
  float soc;
  float minCell;
  float maxCell;
  float tempC;
};
static HistoryPoint history[HISTORY_SIZE];
static uint16_t historyHead = 0;
static uint16_t historyCount = 0;
static uint32_t lastHistorySample = 0;

static void recordHistory() {
  history[historyHead].ts = millis() / 1000;
  history[historyHead].packV = packTotalV;
  history[historyHead].currentA = currentValid ? ((float)current_mA / 1000.0f) : 0.0f;
  history[historyHead].soc = socPct;
  history[historyHead].minCell = packMinCellV < 900.0f ? packMinCellV : 0.0f;
  history[historyHead].maxCell = packMaxCellV;
  history[historyHead].tempC = packAvgTempC;
  historyHead = (historyHead + 1) % HISTORY_SIZE;
  if (historyCount < HISTORY_SIZE) historyCount++;
}

// ===================== Event Log (Persistent) =====================
#define EVENT_LOG_SIZE 200
#define EVENT_LOG_FILE "/event_log.jsonl"
enum EventType : uint8_t {
  EVT_STATE=0, EVT_CONTACTOR, EVT_FAULT, EVT_FAULT_CLEAR,
  EVT_ARM, EVT_DISARM, EVT_RESET, EVT_PERM
};
struct EventEntry {
  time_t ts;
  EventType type;
  char msg[60];
};
static EventEntry eventLog[EVENT_LOG_SIZE];
static uint8_t evtHead = 0;
static uint16_t evtCount = 0;
static bool eventLogLoaded = false;

// Save single event to persistent storage (append mode)
static void saveEventToFile(const EventEntry& evt) {
  if (!LittleFS.begin(true)) return;

  File f = LittleFS.open(EVENT_LOG_FILE, "a");
  if (!f) return;

  // JSON line format: {"ts":123456789,"t":0,"m":"message"}
  char line[128];
  snprintf(line, sizeof(line), "{\"ts\":%ld,\"t\":%d,\"m\":\"%s\"}\n",
           (long)evt.ts, (int)evt.type, evt.msg);
  f.print(line);
  f.close();
}

// Rotate log file if too many entries (keep last EVENT_LOG_SIZE)
static void rotateEventLogFile() {
  if (!LittleFS.begin(true)) return;

  File f = LittleFS.open(EVENT_LOG_FILE, "r");
  if (!f) return;

  // Count lines
  uint16_t lineCount = 0;
  while (f.available()) {
    if (f.read() == '\n') lineCount++;
  }
  f.close();

  // If under limit, no rotation needed
  if (lineCount <= EVENT_LOG_SIZE) return;

  // Read all lines, keep only last EVENT_LOG_SIZE
  f = LittleFS.open(EVENT_LOG_FILE, "r");
  if (!f) return;

  uint16_t skipLines = lineCount - EVENT_LOG_SIZE;
  String kept = "";
  uint16_t currentLine = 0;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (currentLine >= skipLines) {
      kept += line + "\n";
    }
    currentLine++;
  }
  f.close();

  // Rewrite file with kept lines
  f = LittleFS.open(EVENT_LOG_FILE, "w");
  if (f) {
    f.print(kept);
    f.close();
  }
}

// Load event log from persistent storage
static void loadEventLog() {
  if (eventLogLoaded) return;
  eventLogLoaded = true;

  if (!LittleFS.begin(true)) {
    Serial.println("⚠️ LittleFS mount failed for event log");
    return;
  }

  File f = LittleFS.open(EVENT_LOG_FILE, "r");
  if (!f) {
    Serial.println("ℹ️ No event log file found, starting fresh");
    return;
  }

  evtHead = 0;
  evtCount = 0;

  while (f.available() && evtCount < EVENT_LOG_SIZE) {
    String line = f.readStringUntil('\n');
    if (line.length() < 10) continue;

    // Parse JSON line: {"ts":123456789,"t":0,"m":"message"}
    JsonDocument doc;
    if (deserializeJson(doc, line) != DeserializationError::Ok) continue;

    eventLog[evtHead].ts = doc["ts"] | 0L;
    eventLog[evtHead].type = (EventType)(doc["t"] | 0);
    strlcpy(eventLog[evtHead].msg, doc["m"] | "", sizeof(eventLog[evtHead].msg));

    evtHead = (evtHead + 1) % EVENT_LOG_SIZE;
    evtCount++;
  }
  f.close();

  Serial.printf("📋 Event log loaded: %d events\n", evtCount);
}

// Clear event log (RAM and file)
static void clearEventLog() {
  evtHead = 0;
  evtCount = 0;
  if (LittleFS.begin(true)) {
    LittleFS.remove(EVENT_LOG_FILE);
  }
  Serial.println("🗑️ Event log cleared");
}

static void logEvent(EventType type, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  time_t now;
  time(&now);
  eventLog[evtHead].ts = (now > 1700000000) ? now : (time_t)(millis() / 1000);
  eventLog[evtHead].type = type;
  vsnprintf(eventLog[evtHead].msg, sizeof(eventLog[evtHead].msg), fmt, ap);
  va_end(ap);

  Serial.printf("[EVT] %s\n", eventLog[evtHead].msg);

  // Save to persistent storage
  saveEventToFile(eventLog[evtHead]);

  evtHead = (evtHead + 1) % EVENT_LOG_SIZE;
  evtCount++;

  // Rotate file periodically (every 50 new events after reaching limit)
  if (evtCount > 0 && evtCount % 50 == 0) {
    rotateEventLogFile();
  }
}

// ===================== i18n =====================
enum StrId : uint8_t {
  S_TITLE=0, S_STATUS, S_SETTINGS, S_ARM, S_DISARM, S_RESET_TRIP,
  S_OK, S_ALERT_TRIP, S_NO_FAULTS, S_WARN,
  S_STATE, S_ARMED, S_IDS, S_PRECHARGE_TIME,
  S_CONTACTORS, S_PRECHARGE, S_MAIN, S_CHARGE, S_DISCHARGE,
  S_PACK_SUMMARY, S_TOTAL_V, S_CELL_MIN, S_CELL_MAX, S_DELTA, S_TEMP_AVG, S_ACTIVE_CELLS,
  S_CELL_VOLTAGES, S_LANGUAGE, S_LANG_ES, S_LANG_EN, S_LANG_DE,
  S_CHEMISTRY, S_APPLY_LFP, S_APPLY_NMC, S_APPLY_LTO, S_LIMITS, S_SAVE,
  S_CELL_OV, S_CELL_UV, S_DELTA_MAX, S_TEMP_MIN, S_TEMP_MAX, S_PACK_OV, S_PACK_UV, S_COMM_TIMEOUT,
  S_PRECHARGE_MS, S_ALLOW_CHG, S_ALLOW_DSG,
  S_ENERGY, S_SOC, S_SOH, S_CYCLES, S_CURRENT, S_CAP_AH, S_SOH_AH, S_INIT_SOC, S_CURR_POL, S_EFF,
  S_LEARN, S_FULL_V, S_EMPTY_V, S_TAIL_A, S_HOLD_MS, S_RESET_ENERGY,
  S_DELTA_RECOV, S_CELL_OV_RECOV, S_CELL_UV_RECOV, S_PACK_OV_RECOV, S_PACK_UV_RECOV,
  S_POWER, S_CELL_AVG,
  S_USER_COUNTERS, S_CHARGE_AH, S_DISCHARGE_AH, S_RESET_COUNTERS,
  S_EVENT_LOG, S_DOWNLOAD_LOG, S_CLEAR_LOG,
  S_EXPECTED_CELLS,
  S_TAPER_CHG_V, S_TAPER_DIS_V,
  S_TAPER_CHG_END_V, S_TAPER_DIS_END_V,
  S_TAPER_RECOVERY, S_TAPER_HOLD, S_DYN_CVL_OFFSET,
  S_BAL_CURRENT, S_BAL_TIME_EST,
  S_NIGHT_MODE, S_NIGHT_START, S_NIGHT_END, S_NIGHT_CCL, S_NIGHT_DCL,
  S_MQTT, S_MQTT_ENDPOINT, S_MQTT_PORT, S_MQTT_INSECURE, S_MQTT_USER, S_MQTT_PASS
};

static const char* TR_ES[] = {
  "JK BMS + Control",
  "Estado","Configuración","Armar","Desarmar","Reset Trip",
  "OK","ALERTA / TRIP","Sin fallos activos","AVISO",
  "Estado","Armado","IDs","Tiempo de precarga",
  "Contactores","Precarga","Principal","Carga","Descarga",
  "Resumen batería","Voltaje total","Celda mín.","Celda máx.","Delta","Temp. media","Celdas activas",
  "Voltajes por celda","Idioma","Español","Inglés","Alemán",
  "Química","Aplicar LFP","Aplicar NMC","Aplicar LTO","Límites","Guardar",
  "Cell OV","Cell UV","Delta max","Temp min","Temp max","Pack OV (0=off)","Pack UV (0=off)","Timeout comm (ms)",
  "Precarga (ms)","Permitir carga","Permitir descarga",
  "Energía","SoC","SoH","Ciclos","Corriente","Capacidad (Ah)","SoH (Ah)","SoC inicial (%)","Polaridad corriente (+1/-1)","Eficiencia carga",
  "Auto-aprendizaje","V celda FULL","V celda EMPTY","Tail current (A)","Hold (ms)","Reset energía",
  "Delta recup. (V)","Cell OV recup. (V)","Cell UV recup. (V)","Pack OV recup. (V)","Pack UV recup. (V)",
  "Potencia","Celda prom.",
  "Contadores","Carga (Ah)","Descarga (Ah)","Reset contadores",
  "Registro de eventos","Descargar log","Limpiar log",
  "Celdas esperadas (0=off)",
  "Taper carga (V, 0=off)", "Taper descarga (V, 0=off)",
  "Taper fin carga (V)", "Taper fin descarga (V)",
  "Recup. taper (%/s)", "Hold taper (ms)", "CVL dinámico offset (V, 0=off)",
  "Corriente balance (A)", "Tiempo balance est.",
  "Modo noche", "Hora inicio", "Hora fin", "CCL noche (%)", "DCL noche (%)",
  "MQTT", "Endpoint (host)", "Puerto", "Sin TLS (inseguro)", "Usuario (vacío=sin auth)", "Contraseña"
};

static const char* TR_EN[] = {
  "JK BMS + Control",
  "Status","Settings","Arm","Disarm","Reset Trip",
  "OK","ALERT / TRIP","No active faults","WARNING",
  "State","Armed","IDs","Precharge time",
  "Contactors","Precharge","Main","Charge","Discharge",
  "Pack summary","Total voltage","Min cell","Max cell","Delta","Avg temp","Active cells",
  "Cell voltages","Language","Spanish","English","German",
  "Chemistry","Apply LFP","Apply NMC","Apply LTO","Limits","Save",
  "Cell OV","Cell UV","Max delta","Temp min","Temp max","Pack OV (0=off)","Pack UV (0=off)","Comm timeout (ms)",
  "Precharge (ms)","Allow charge","Allow discharge",
  "Energy","SoC","SoH","Cycles","Current","Capacity (Ah)","SoH (Ah)","Initial SoC (%)","Current polarity (+1/-1)","Charge efficiency",
  "Auto-learning","Cell V FULL","Cell V EMPTY","Tail current (A)","Hold (ms)","Reset energy",
  "Delta recov. (V)","Cell OV recov. (V)","Cell UV recov. (V)","Pack OV recov. (V)","Pack UV recov. (V)",
  "Power","Avg cell",
  "Counters","Charge (Ah)","Discharge (Ah)","Reset counters",
  "Event log","Download log","Clear log",
  "Expected cells (0=off)",
  "Taper charge (V, 0=off)", "Taper discharge (V, 0=off)",
  "Taper charge end (V)", "Taper discharge end (V)",
  "Taper recovery (%/s)", "Taper hold (ms)", "Dynamic CVL offset (V, 0=off)",
  "Balance current (A)", "Balance time est.",
  "Night mode", "Start hour", "End hour", "Night CCL (%)", "Night DCL (%)",
  "MQTT", "Endpoint (host)", "Port", "No TLS (insecure)", "User (empty=no auth)", "Password"
};

static const char* TR_DE[] = {
  "JK BMS + Steuerung",
  "Status","Einstellungen","Scharf","Entschärfen","Trip zurücksetzen",
  "OK","ALARM / TRIP","Keine aktiven Fehler","WARNUNG",
  "Zustand","Scharf","IDs","Vorladezeit",
  "Schütze","Vorladen","Haupt","Laden","Entladen",
  "Pack-Übersicht","Gesamtspannung","Min Zelle","Max Zelle","Delta","Ø Temperatur","Aktive Zellen",
  "Zellspannungen","Sprache","Spanisch","Englisch","Deutsch",
  "Chemie","LFP anwenden","NMC anwenden","LTO anwenden","Grenzwerte","Speichern",
  "Zell OV","Zell UV","Max Delta","Temp min","Temp max","Pack OV (0=aus)","Pack UV (0=aus)","Comm Timeout (ms)",
  "Vorladen (ms)","Laden erlaubt","Entladen erlaubt",
  "Energie","SoC","SoH","Zyklen","Strom","Kapazität (Ah)","SoH (Ah)","Start-SoC (%)","Strom-Polarität (+1/-1)","Lade-Wirkungsgrad",
  "Auto-Lernen","Zell V FULL","Zell V EMPTY","Tail-Strom (A)","Hold (ms)","Energie zurücksetzen",
  "Delta Erholung (V)","Zell OV Erholung (V)","Zell UV Erholung (V)","Pack OV Erholung (V)","Pack UV Erholung (V)",
  "Leistung","Ø Zelle",
  "Zähler","Laden (Ah)","Entladen (Ah)","Zähler zurücksetzen",
  "Ereignisprotokoll","Log herunterladen","Log löschen",
  "Erwartete Zellen (0=aus)",
  "Taper Laden (V, 0=aus)", "Taper Entladen (V, 0=aus)",
  "Taper Laden Ende (V)", "Taper Entladen Ende (V)",
  "Taper Erholung (%/s)", "Taper Halte (ms)", "Dyn. CVL Offset (V, 0=aus)",
  "Balancestrom (A)", "Balancezeit gesch.",
  "Nachtmodus", "Startzeit", "Endzeit", "Nacht CCL (%)", "Nacht DCL (%)",
  "MQTT", "Endpoint (Host)", "Port", "Kein TLS (unsicher)", "Benutzer (leer=ohne Auth)", "Passwort"
};

static inline const char* T(StrId id) {
  if (lang == LANG_EN) return TR_EN[(uint8_t)id];
  if (lang == LANG_DE) return TR_DE[(uint8_t)id];
  return TR_ES[(uint8_t)id];
}

// ===================== Auth =====================
static bool isAdmin() { return server.authenticate(USER_ADMIN, PASS_ADMIN); }
static bool isUserOrAdmin() {
  if (server.authenticate(USER_ADMIN, PASS_ADMIN)) return true;
  if (server.authenticate(USER_USER, PASS_USER)) return true;
  return false;
}
static void requireAdmin() {
  if (isAdmin()) return;
  server.requestAuthentication(BASIC_AUTH, "JK BMS", "Admin access required");
}
static void requireAnyUser() {
  if (isUserOrAdmin()) return;
  server.requestAuthentication(BASIC_AUTH, "JK BMS", "Authentication required");
}

// ===================== GPIO =====================
static inline void writeOut(uint8_t gpio, bool closed) {
  bool level = closed;
#if CONTACTOR_ACTIVE_LOW
  level = !closed;
#endif
  digitalWrite(gpio, level ? HIGH : LOW);
}
static void setContactors(bool mainC, bool preC, bool chgC, bool dsgC) {
  uint32_t now = millis();
  cMain = false; (void)mainC; // MAIN unused

  if (preC != cPre && (now - lastTogPre) < (uint32_t)CONTACTOR_MIN_TOGGLE_MS) preC = cPre;
  if (chgC != cChg && (now - lastTogChg) < (uint32_t)CONTACTOR_MIN_TOGGLE_MS) chgC = cChg;
  if (dsgC != cDsg && (now - lastTogDsg) < (uint32_t)CONTACTOR_MIN_TOGGLE_MS) dsgC = cDsg;

  if (preC != cPre) { lastTogPre = now; logEvent(EVT_CONTACTOR, "PRECHARGE: %s", preC?"CLOSED":"OPEN"); }
  if (chgC != cChg) { lastTogChg = now; logEvent(EVT_CONTACTOR, "CHARGE: %s", chgC?"CLOSED":"OPEN"); }
  if (dsgC != cDsg) { lastTogDsg = now; logEvent(EVT_CONTACTOR, "DISCHARGE: %s", dsgC?"CLOSED":"OPEN"); }

  cPre = preC; cChg = chgC; cDsg = dsgC;

  writeOut(GPIO_PRECHARGE_CONTACTOR, cPre);
  writeOut(GPIO_CHG_CONTACTOR,       cChg);
  writeOut(GPIO_DSG_CONTACTOR,       cDsg);
}

// ===================== Presets =====================
static void applyPreset(Chemistry c) {
  chem = c;
  if (c == CHEM_LFP) {
    limits.cellOv = 3.65f;
    limits.cellUv = 2.80f;
    limits.deltaMax = 0.05f;
    limits.deltaRecov = 0.040f;
    limits.cellOvRecov = 0.020f;
    limits.cellUvRecov = 0.020f;
    limits.tempMin = 0.0f;
    limits.tempMax = 55.0f;

    limits.taperChgStartV = 3.45f;
    limits.taperChgEndV   = 3.55f;
    limits.taperDisStartV = 3.10f;
    limits.taperDisEndV   = 2.90f;
    limits.taperRecoveryPctS = 2.0f;  // 2%/s = 50s to recover from 0 to 100%
    limits.taperHoldMs = 15000;       // 15s hold at 0% before recovery
    limits.dynCvlOffsetV = 0.8f;

    ecfg.fullCellV = 3.55f;
    ecfg.emptyCellV = 2.95f;
    ecfg.tailCurrentA = 2.0f;
    ecfg.holdMs = 120000;
  } else if (c == CHEM_NMC) {
    limits.cellOv = 4.20f;
    limits.cellUv = 3.00f;
    limits.deltaMax = 0.08f;
    limits.deltaRecov = 0.060f;
    limits.cellOvRecov = 0.030f;
    limits.cellUvRecov = 0.030f;
    limits.tempMin = 0.0f;
    limits.tempMax = 60.0f;

    limits.taperChgStartV = 4.10f;
    limits.taperChgEndV   = 4.18f;
    limits.taperDisStartV = 3.20f;
    limits.taperDisEndV   = 3.05f;
    limits.taperRecoveryPctS = 2.0f;  // 2%/s = 50s to recover from 0 to 100%
    limits.taperHoldMs = 15000;       // 15s hold at 0% before recovery
    limits.dynCvlOffsetV = 0.5f;

    ecfg.fullCellV = 4.12f;
    ecfg.emptyCellV = 3.10f;
    ecfg.tailCurrentA = 2.0f;
    ecfg.holdMs = 120000;
  } else if (c == CHEM_LTO) {
    limits.cellOv = 2.80f;
    limits.cellUv = 1.80f;
    limits.deltaMax = 0.03f;
    limits.deltaRecov = 0.020f;
    limits.cellOvRecov = 0.015f;
    limits.cellUvRecov = 0.015f;
    limits.tempMin = -20.0f;   // LTO works well at low temps
    limits.tempMax = 55.0f;

    limits.taperChgStartV = 2.70f;
    limits.taperChgEndV   = 2.78f;
    limits.taperDisStartV = 2.00f;
    limits.taperDisEndV   = 1.85f;
    limits.taperRecoveryPctS = 2.0f;
    limits.taperHoldMs = 15000;
    limits.dynCvlOffsetV = 0.4f;

    ecfg.fullCellV = 2.75f;
    ecfg.emptyCellV = 1.90f;
    ecfg.tailCurrentA = 2.0f;
    ecfg.holdMs = 120000;
  }
  // CHEM_CUSTOM: don't modify limits, user sets them manually
}

// ===================== Preferences =====================
static void loadConfig() {
  prefs.begin("jkcfg", true);
  chem = (Chemistry)prefs.getUChar("chem", (uint8_t)CHEM_LFP);
  lang = (Lang)prefs.getUChar("lang", (uint8_t)LANG_ES);

  invProto = (InverterProto)prefs.getUChar("invP", (uint8_t)INV_OFF);
  invUserMaxChgA = prefs.getUShort("invChgA", invUserMaxChgA);
  invUserMaxDisA = prefs.getUShort("invDisA", invUserMaxDisA);
  invUserMaxChgV_dV = prefs.getUShort("invChgV", invUserMaxChgV_dV);
  invUserMinDisV_dV = prefs.getUShort("invDisV", invUserMinDisV_dV);

  applyPreset(chem);

  limits.cellOv = prefs.getFloat("cellOv", limits.cellOv);
  limits.cellUv = prefs.getFloat("cellUv", limits.cellUv);
  limits.deltaMax = prefs.getFloat("dMax", limits.deltaMax);
  limits.tempMin = prefs.getFloat("tMin", limits.tempMin);
  limits.tempMax = prefs.getFloat("tMax", limits.tempMax);
  limits.packOv = prefs.getFloat("pOv", limits.packOv);
  limits.packUv = prefs.getFloat("pUv", limits.packUv);
  limits.deltaRecov = prefs.getFloat("dRec", limits.deltaRecov);
  limits.cellOvRecov = prefs.getFloat("cOvR", limits.cellOvRecov);
  limits.cellUvRecov = prefs.getFloat("cUvR", limits.cellUvRecov);
  limits.packOvRecov = prefs.getFloat("pOvR", limits.packOvRecov);
  limits.packUvRecov = prefs.getFloat("pUvR", limits.packUvRecov);
  limits.commTimeoutMs = prefs.getUInt("cto", limits.commTimeoutMs);
  limits.prechargeMs = prefs.getUInt("pcms", limits.prechargeMs);
  limits.expectedCells = prefs.getUShort("expCells", 0);
  limits.taperChgStartV = prefs.getFloat("tpChg", limits.taperChgStartV);
  limits.taperChgEndV   = prefs.getFloat("tpChgE", limits.taperChgEndV);
  limits.taperDisStartV = prefs.getFloat("tpDis", limits.taperDisStartV);
  limits.taperDisEndV   = prefs.getFloat("tpDisE", limits.taperDisEndV);
  limits.taperRecoveryPctS = prefs.getFloat("tpRec", limits.taperRecoveryPctS);
  limits.taperHoldMs = prefs.getUShort("tpHold", limits.taperHoldMs);
  limits.dynCvlOffsetV = prefs.getFloat("dynCvl", limits.dynCvlOffsetV);

  ecfg.capacityAh = prefs.getFloat("capAh", ecfg.capacityAh);
  ecfg.sohAh = prefs.getFloat("sohAh", ecfg.sohAh);
  ecfg.initSocPct = prefs.getFloat("initSoc", ecfg.initSocPct);
  ecfg.currentPolarity = (int8_t)prefs.getChar("cPol", ecfg.currentPolarity);
  ecfg.coulombEff = prefs.getFloat("eff", ecfg.coulombEff);

  ecfg.fullCellV = prefs.getFloat("fV", ecfg.fullCellV);
  ecfg.emptyCellV = prefs.getFloat("eV", ecfg.emptyCellV);
  ecfg.tailCurrentA = prefs.getFloat("tail", ecfg.tailCurrentA);
  ecfg.holdMs = prefs.getUInt("hold", ecfg.holdMs);

  // Balance & night mode settings
  balanceCurrentA = prefs.getFloat("balA", balanceCurrentA);
  nightModeEnabled = prefs.getBool("nightEn", false);
  nightStartHour = prefs.getUChar("nightS", 23);
  nightEndHour = prefs.getUChar("nightE", 7);
  nightCclPct = prefs.getUChar("nightCcl", 50);
  nightDclPct = prefs.getUChar("nightDcl", 100);

  // MQTT settings from NVS
  mqttEndpoint = prefs.getString("mqttEp", "");
  mqttPort = prefs.getUShort("mqttPt", MQTT_PORT);
  mqttInsecure = prefs.getBool("mqttIns", false);
  mqttUser = prefs.getString("mqttUsr", "");
  mqttPass = prefs.getString("mqttPwd", "");

  prefs.end();

  if (ecfg.capacityAh < 1.0f) ecfg.capacityAh = 1.0f;
  if (ecfg.sohAh < 1.0f) ecfg.sohAh = ecfg.capacityAh;
  ecfg.initSocPct = constrain(ecfg.initSocPct, 0.0f, 100.0f);
  if (ecfg.currentPolarity != 1 && ecfg.currentPolarity != -1) ecfg.currentPolarity = 1;
  ecfg.coulombEff = constrain(ecfg.coulombEff, 0.90f, 1.00f);
  ecfg.tailCurrentA = constrain(ecfg.tailCurrentA, 0.1f, 500.0f);
  if (ecfg.holdMs < 5000) ecfg.holdMs = 5000;
}

static void saveConfig() {
  prefs.begin("jkcfg", false);
  prefs.putUChar("chem", (uint8_t)chem);
  prefs.putUChar("lang", (uint8_t)lang);
  prefs.putUChar("invP", (uint8_t)invProto);
  prefs.putUShort("invChgA", invUserMaxChgA);
  prefs.putUShort("invDisA", invUserMaxDisA);
  prefs.putUShort("invChgV", invUserMaxChgV_dV);
  prefs.putUShort("invDisV", invUserMinDisV_dV);

  prefs.putFloat("cellOv", limits.cellOv);
  prefs.putFloat("cellUv", limits.cellUv);
  prefs.putFloat("dMax", limits.deltaMax);
  prefs.putFloat("tMin", limits.tempMin);
  prefs.putFloat("tMax", limits.tempMax);
  prefs.putFloat("pOv", limits.packOv);
  prefs.putFloat("pUv", limits.packUv);
  prefs.putFloat("dRec", limits.deltaRecov);
  prefs.putFloat("cOvR", limits.cellOvRecov);
  prefs.putFloat("cUvR", limits.cellUvRecov);
  prefs.putFloat("pOvR", limits.packOvRecov);
  prefs.putFloat("pUvR", limits.packUvRecov);
  prefs.putUInt("cto", limits.commTimeoutMs);
  prefs.putUInt("pcms", limits.prechargeMs);
  prefs.putUShort("expCells", limits.expectedCells);
  prefs.putFloat("tpChg", limits.taperChgStartV);
  prefs.putFloat("tpChgE", limits.taperChgEndV);
  prefs.putFloat("tpDis", limits.taperDisStartV);
  prefs.putFloat("tpDisE", limits.taperDisEndV);
  prefs.putFloat("tpRec", limits.taperRecoveryPctS);
  prefs.putUShort("tpHold", limits.taperHoldMs);
  prefs.putFloat("dynCvl", limits.dynCvlOffsetV);

  prefs.putFloat("capAh", ecfg.capacityAh);
  prefs.putFloat("sohAh", ecfg.sohAh);
  prefs.putFloat("initSoc", ecfg.initSocPct);
  prefs.putChar("cPol", (char)ecfg.currentPolarity);
  prefs.putFloat("eff", ecfg.coulombEff);

  prefs.putFloat("fV", ecfg.fullCellV);
  prefs.putFloat("eV", ecfg.emptyCellV);
  prefs.putFloat("tail", ecfg.tailCurrentA);
  prefs.putUInt("hold", ecfg.holdMs);

  // Balance & night mode settings
  prefs.putFloat("balA", balanceCurrentA);
  prefs.putBool("nightEn", nightModeEnabled);
  prefs.putUChar("nightS", nightStartHour);
  prefs.putUChar("nightE", nightEndHour);
  prefs.putUChar("nightCcl", nightCclPct);
  prefs.putUChar("nightDcl", nightDclPct);

  // MQTT settings
  prefs.putString("mqttEp", mqttEndpoint);
  prefs.putUShort("mqttPt", mqttPort);
  prefs.putBool("mqttIns", mqttInsecure);
  prefs.putString("mqttUsr", mqttUser);
  prefs.putString("mqttPwd", mqttPass);
  prefs.end();
}

static void saveLangOnly() {
  prefs.begin("jkcfg", false);
  prefs.putUChar("lang", (uint8_t)lang);
  prefs.putUChar("invP", (uint8_t)invProto);
  prefs.putUShort("invChgA", invUserMaxChgA);
  prefs.putUShort("invDisA", invUserMaxDisA);
  prefs.putUShort("invChgV", invUserMaxChgV_dV);
  prefs.putUShort("invDisV", invUserMinDisV_dV);
  prefs.end();
}

// Energy persistence
static void initEnergyFromInitSoc() {
  float cap = max(1.0f, ecfg.sohAh);
  socPct = constrain(ecfg.initSocPct, 0.0f, 100.0f);
  coulombBalance_Ah = (socPct/100.0f) * cap;
}

static void loadEnergyState() {
  prefs.begin("jkcfg", true);
  bool has = prefs.getBool("hasE", false);
  if (has) {
    socPct = prefs.getFloat("soc", 50.0f);
    coulombBalance_Ah = prefs.getFloat("balAh", 0.0f);
    throughput_Ah = prefs.getFloat("thrAh", 0.0f);
    cycleCountEq = prefs.getFloat("cyc", 0.0f);
    learnPhase = (LearnPhase)prefs.getUChar("lph", (uint8_t)LEARN_IDLE);
    learnDischargeAh = prefs.getFloat("ldAh", 0.0f);
  } else {
    initEnergyFromInitSoc();
    throughput_Ah = 0.0f;
    cycleCountEq = 0.0f;
    learnPhase = LEARN_IDLE;
    learnDischargeAh = 0.0f;
  }
  prefs.end();

  float cap = max(1.0f, ecfg.sohAh);
  coulombBalance_Ah = constrain(coulombBalance_Ah, 0.0f, cap);
  socPct = constrain((coulombBalance_Ah / cap) * 100.0f, 0.0f, 100.0f);
}

static void saveEnergyState() {
  prefs.begin("jkcfg", false);
  prefs.putBool("hasE", true);
  prefs.putFloat("soc", socPct);
  prefs.putFloat("balAh", coulombBalance_Ah);
  prefs.putFloat("thrAh", throughput_Ah);
  prefs.putFloat("cyc", cycleCountEq);
  prefs.putUChar("lph", (uint8_t)learnPhase);
  prefs.putFloat("ldAh", learnDischargeAh);
  prefs.end();
}

static void resetEnergyState() {
  initEnergyFromInitSoc();
  throughput_Ah = 0.0f;
  cycleCountEq = 0.0f;
  learnPhase = LEARN_IDLE;
  learnDischargeAh = 0.0f;
  saveEnergyState();
}

// ===================== JK poll =====================
static void requestModule(uint8_t id) {
  twai_message_t tx = {};
  tx.identifier = id;
  tx.extd = 0;
  tx.rtr = 0;
  tx.data_length_code = 1;
  tx.data[0] = 0xFF;
  (void)twai_transmit(&tx, pdMS_TO_TICKS(50));
}

// ===================== AHBC decode =====================
static bool decodeAhbcCurrent(const twai_message_t& rx, int32_t& out_mA) {
  if (rx.data_length_code < 8) return false;

  uint32_t raw =
    (uint32_t(rx.data[0]) << 24) |
    (uint32_t(rx.data[1]) << 16) |
    (uint32_t(rx.data[2]) << 8)  |
    (uint32_t(rx.data[3]) << 0);

  uint8_t err = rx.data[4];
  if (err != 0) return false;

  int32_t signed_mA = (int32_t)(raw - 0x80000000UL);
  out_mA = signed_mA * (int32_t)ecfg.currentPolarity;
  return true;
}

// ===================== SoC / cycles integration =====================
static void integrateCurrent(int32_t mA, uint32_t nowMs) {
  if (!currentValid) {
    currentValid = true;
    current_mA = mA;
    lastCurrentMs = nowMs;
    return;
  }

  uint32_t dtMs = nowMs - lastCurrentMs;
  if (dtMs > 2000) {
    current_mA = mA;
    lastCurrentMs = nowMs;
    return;
  }

  float I_A = (float)mA / 1000.0f;
  float dt_h = (float)dtMs / 3600000.0f;

  float eff = (I_A > 0.0f) ? ecfg.coulombEff : 1.0f;
  float dAh = I_A * dt_h * eff;

  float cap = max(1.0f, ecfg.sohAh);
  coulombBalance_Ah = constrain(coulombBalance_Ah + dAh, 0.0f, cap);
  socPct = constrain((coulombBalance_Ah / cap) * 100.0f, 0.0f, 100.0f);
  if (socPct < 99.9f) fullConfirmed = false;

  throughput_Ah += fabsf(dAh);
  cycleCountEq = throughput_Ah / cap;

  // User resettable counters
  float abs_dAh = fabsf(dAh);
  float abs_dWh = abs_dAh * packTotalV;
  if (I_A > 0.001f) { user_chargeAh += abs_dAh; user_chargeWh += abs_dWh; }
  else if (I_A < -0.001f) { user_dischargeAh += abs_dAh; user_dischargeWh += abs_dWh; }

  // Learning: track discharge Ah only (I_A < 0 means SoC decreases in our convention)
  if (learnPhase == LEARN_FROM_FULL && I_A < 0.0f) {
    learnDischargeAh += (-I_A) * dt_h;
  }

  current_mA = mA;
  lastCurrentMs = nowMs;
  power_W = packTotalV * ((float)mA / 1000.0f);
}

// ===================== Pack aggregation =====================
static void computePackSnapshot() {
  packTotalV = 0.0f;
  packMinCellV = 999.0f;
  packMaxCellV = 0.0f;
  packAvgTempC = 0.0f;
  packActiveCells = 0;
  activeIdsCsv = "";

  float tempSum = 0.0f;
  int tempN = 0;

  for (uint8_t id=1; id<=MAX_MODULES; id++) {
    if (!modules[id].active) continue;

    if (activeIdsCsv.length()) activeIdsCsv += ",";
    activeIdsCsv += String(id);

    packTotalV += modules[id].packV;
    tempSum += modules[id].tempC;
    tempN++;

    for (uint8_t i=0; i<modules[id].cellCount && i<MAX_CELLS_PER_MODULE; i++) {
      float v = modules[id].cells[i];
      if (v <= 0.001f) continue;
      packActiveCells++;
      if (v < packMinCellV) { packMinCellV = v; packMinCellModule = id; packMinCellIndex = i+1; }
      if (v > packMaxCellV) { packMaxCellV = v; packMaxCellModule = id; packMaxCellIndex = i+1; }
    }
  }
  packDeltaV = (packMaxCellV > 0.01f && packMinCellV < 998.0f) ? (packMaxCellV - packMinCellV) : 0.0f;
  packAvgTempC = (tempN>0) ? (tempSum / tempN) : 0.0f;
  packAvgCellV = (packActiveCells > 0) ? (packTotalV / packActiveCells) : 0.0f;
}

// ===================== Faults =====================
static void evaluateFaults() {
  uint32_t now = millis();

  // First: check for communication timeouts and mark modules inactive + clear stale data
  for (uint8_t id=1; id<=MAX_MODULES; id++) {
    if (!modules[id].active) continue;
    if (now - modules[id].lastSeenMs > limits.commTimeoutMs) {
      // Module timed out - mark inactive and clear stale data
      modules[id].active = false;
      modules[id].packV = 0.0f;
      modules[id].tempC = 0.0f;
      modules[id].cellCount = 0;
      for (uint8_t i=0; i<MAX_CELLS_PER_MODULE; i++) {
        modules[id].cells[i] = 0.0f;
      }
      Serial.printf("⚠️ Module %d TIMEOUT - marked inactive\n", id);
    }
  }

  // Now compute pack snapshot with only active modules
  computePackSnapshot();

  // Reset fault flags
  faultComm=faultTemp=faultOv=faultUv=faultDelta=faultPackOv=faultPackUv=false;
  faultText = "";
  criticalFault = false;
  directionalFault = false;

  bool anyActive = false;

  for (uint8_t id=1; id<=MAX_MODULES; id++) {
    if (!modules[id].active) continue;
    anyActive = true;

    // Temperature faults
    if (modules[id].tempC < limits.tempMin || modules[id].tempC > limits.tempMax) faultTemp = true;

    // Cell voltage faults
    for (uint8_t i=0; i<modules[id].cellCount && i<MAX_CELLS_PER_MODULE; i++) {
      float v = modules[id].cells[i];
      if (v <= 0.001f) continue;
      if (v > limits.cellOv) faultOv = true;
      if (v < limits.cellUv) faultUv = true;
    }
  }

  // Pack-level faults with latching hysteresis
  {
    static bool latchPackOv = false;
    static bool latchPackUv = false;
    static bool latchDelta  = false;

    // Pack OV: latch on when above packOv, clear when below packOvRecov
    if (limits.packOv > 0.01f) {
      if (packTotalV > limits.packOv) latchPackOv = true;
      if (limits.packOvRecov > 0.01f && packTotalV < limits.packOvRecov) latchPackOv = false;
      else if (limits.packOvRecov <= 0.01f && packTotalV <= limits.packOv) latchPackOv = false;
    } else { latchPackOv = false; }
    faultPackOv = latchPackOv;

    // Pack UV: latch on when below packUv, clear when above packUvRecov
    if (limits.packUv > 0.01f && anyActive) {
      if (packTotalV < limits.packUv) latchPackUv = true;
      if (limits.packUvRecov > 0.01f && packTotalV > limits.packUvRecov) latchPackUv = false;
      else if (limits.packUvRecov <= 0.01f && packTotalV >= limits.packUv) latchPackUv = false;
    } else { latchPackUv = false; }
    faultPackUv = latchPackUv;

    // Delta: latch on when above deltaMax, clear when below deltaRecov
    if (anyActive) {
      if (packDeltaV > limits.deltaMax) latchDelta = true;
      if (packDeltaV < limits.deltaRecov) latchDelta = false;
    } else { latchDelta = false; }
    faultDelta = latchDelta;
  }

  // Communication fault: armed but no active modules
  if (armed && !anyActive) faultComm = true;

  // Cell count mismatch fault
  faultCellCount = false;
  if (limits.expectedCells > 0 && anyActive && packActiveCells != limits.expectedCells) {
    faultCellCount = true;
  }

  // Build fault text and determine fault severity
  auto add = [&](const char* s){
    if (faultText.length()) faultText += " | ";
    faultText += s;
  };

  // CRITICAL faults - will trip the system and open all contactors
  if (faultComm) { add("COMMS_TIMEOUT"); criticalFault = true; }
  if (faultOv) { add("CELL_OV"); criticalFault = true; }
  if (faultUv) { add("CELL_UV"); criticalFault = true; }
  if (faultPackOv) { add("PACK_OV"); criticalFault = true; }
  if (faultPackUv) { add("PACK_UV"); criticalFault = true; }
  if (faultTemp) { add("TEMP_OUT_OF_RANGE"); criticalFault = true; }
  if (faultCellCount) {
    char cbuf[32];
    snprintf(cbuf, sizeof(cbuf), "CELLS:%d/%d", packActiveCells, limits.expectedCells);
    add(cbuf);
    criticalFault = true;
    static uint32_t lastCellCountLog = 0;
    if (now - lastCellCountLog > 30000) {
      lastCellCountLog = now;
      logEvent(EVT_FAULT, "Cell count mismatch: %d/%d", packActiveCells, limits.expectedCells);
    }
  }

  // DIRECTIONAL faults - affect charge/discharge permissions but don't trip
  if (faultDelta) { add("DELTA_HIGH"); directionalFault = true; }
}

static void computePermissions() {
  const float ov = limits.cellOv;
  const float uv = limits.cellUv;

  static bool allowChargeLatched = true;
  static bool allowDischargeLatched = true;

  // Only allow charge/discharge if we have active modules with valid data
  if (packActiveCells == 0) {
    allowCharge = false;
    allowDischarge = false;
    return;
  }

  if (allowChargeLatched) {
    if (packMaxCellV >= ov) allowChargeLatched = false;
  } else {
    if (packMaxCellV <= (ov - limits.cellOvRecov)) allowChargeLatched = true;
  }

  if (allowDischargeLatched) {
    if (packMinCellV <= uv) allowDischargeLatched = false;
  } else {
    if (packMinCellV >= (uv + limits.cellUvRecov)) allowDischargeLatched = true;
  }

  allowCharge = allowChargeLatched;
  allowDischarge = allowDischargeLatched;

  // Critical fault: stop all operations
  if (criticalFault) { allowCharge = false; allowDischarge = false; }

  // Directional fault (high delta): limit charging to prevent further imbalance
  if (directionalFault) { allowCharge = false; }
}

// ===================== Auto learning =====================
static bool absCurrentBelowTail() {
  if (!currentValid) return false;
  float a = fabsf((float)current_mA / 1000.0f);
  return (a <= ecfg.tailCurrentA);
}

static void applyFullCalibration() {
  // Force SoC=100% and set balance to learned capacity
  float cap = max(1.0f, ecfg.sohAh);
  coulombBalance_Ah = cap;
  socPct = 100.0f;
  fullConfirmed = true;
}

static void applyEmptyCalibration() {
  coulombBalance_Ah = 0.0f;
  socPct = 0.0f;
}

static void learningTick() {
  // Only learn when we have valid current and at least one module active and not in critical trip
  if (!currentValid || packActiveCells == 0) {
    fullStableStart = 0;
    emptyStableStart = 0;
    return;
  }
  if (criticalFault) return;

  uint32_t now = millis();

  bool fullCond  = (packMaxCellV >= ecfg.fullCellV) && absCurrentBelowTail() && allowCharge; // allowCharge means not OV flagged (uses max cell - charging stops when highest cell is full)
  bool emptyCond = (packMinCellV <= ecfg.emptyCellV) && absCurrentBelowTail() && allowDischarge; // allowDischarge means not UV flagged (uses min cell - discharging stops when lowest cell is empty)

  // FULL detection (stable)
  if (fullCond) {
    if (fullStableStart == 0) fullStableStart = now;
    if (now - fullStableStart >= ecfg.holdMs) {
      // FULL event
      applyFullCalibration();
      // Start learning discharge capacity
      learnPhase = LEARN_FROM_FULL;
      learnDischargeAh = 0.0f;
      emptyStableStart = 0;
    }
  } else {
    fullStableStart = 0;
  }

  // EMPTY detection only meaningful if learning started
  if (learnPhase == LEARN_FROM_FULL) {
    if (emptyCond) {
      if (emptyStableStart == 0) emptyStableStart = now;
      if (now - emptyStableStart >= ecfg.holdMs) {
        // EMPTY event: learn capacity from FULL->EMPTY discharge
        applyEmptyCalibration();

        // Clamp learned SoH Ah to reasonable bounds (10%..150% of nominal)
        float minAh = max(1.0f, ecfg.capacityAh * 0.10f);
        float maxAh = ecfg.capacityAh * 1.50f;
        float learned = constrain(learnDischargeAh, minAh, maxAh);

        ecfg.sohAh = learned;
        // Persist to config (so web shows updated SoH)
        saveConfig();

        learnPhase = LEARN_IDLE;
        fullStableStart = 0;
        emptyStableStart = 0;
      }
    } else {
      emptyStableStart = 0;
    }
  }
}

// ===================== Taper with Ratchet Recovery =====================
// CCL/DCL can drop instantly but only rise slowly to prevent cycling
static float taperChgFactor = 1.0f;
static float taperDisFactor = 1.0f;
static float taperChgTarget = 1.0f;  // instant target from voltage
static float taperDisTarget = 1.0f;
static uint32_t taperChgZeroSince = 0;  // when CCL reached 0% (for hold timer)
static uint32_t taperDisZeroSince = 0;

static void computeTaper() {
  static uint32_t lastTaperMs = 0;
  uint32_t now = millis();
  float dtS = (lastTaperMs > 0) ? (now - lastTaperMs) / 1000.0f : 0.0f;
  lastTaperMs = now;

  // --- Charge tapering ---
  // Calculate instant target from voltage
  if (limits.taperChgStartV > 0.01f && packMaxCellV >= limits.taperChgStartV) {
    float range = limits.taperChgEndV - limits.taperChgStartV;
    if (range > 0.001f)
      taperChgTarget = constrain((limits.taperChgEndV - packMaxCellV) / range, 0.0f, 1.0f);
    else
      taperChgTarget = 0.0f;
  } else {
    taperChgTarget = 1.0f;
  }

  // Apply ratchet logic: drop instant, rise slow
  if (taperChgTarget < taperChgFactor) {
    // Dropping - follow instantly (safety)
    taperChgFactor = taperChgTarget;
    if (taperChgFactor < 0.001f) taperChgZeroSince = now;  // mark when we hit 0
  } else if (taperChgTarget > taperChgFactor) {
    // Rising - check hold time first, then rate limit
    bool canRecover = true;
    if (taperChgFactor < 0.001f && limits.taperHoldMs > 0) {
      // We're at 0%, check if hold time has passed
      if (taperChgZeroSince == 0) taperChgZeroSince = now;
      canRecover = (now - taperChgZeroSince) >= limits.taperHoldMs;
    }
    if (canRecover && limits.taperRecoveryPctS > 0.01f) {
      float maxRise = (limits.taperRecoveryPctS / 100.0f) * dtS;
      taperChgFactor = min(taperChgTarget, taperChgFactor + maxRise);
    }
  }
  // Reset zero timer if we're above 0
  if (taperChgFactor > 0.01f) taperChgZeroSince = 0;

  // --- Discharge tapering ---
  if (limits.taperDisStartV > 0.01f && packMinCellV <= limits.taperDisStartV) {
    float range = limits.taperDisStartV - limits.taperDisEndV;
    if (range > 0.001f)
      taperDisTarget = constrain((packMinCellV - limits.taperDisEndV) / range, 0.0f, 1.0f);
    else
      taperDisTarget = 0.0f;
  } else {
    taperDisTarget = 1.0f;
  }

  // Apply ratchet logic for discharge
  if (taperDisTarget < taperDisFactor) {
    taperDisFactor = taperDisTarget;
    if (taperDisFactor < 0.001f) taperDisZeroSince = now;
  } else if (taperDisTarget > taperDisFactor) {
    bool canRecover = true;
    if (taperDisFactor < 0.001f && limits.taperHoldMs > 0) {
      if (taperDisZeroSince == 0) taperDisZeroSince = now;
      canRecover = (now - taperDisZeroSince) >= limits.taperHoldMs;
    }
    if (canRecover && limits.taperRecoveryPctS > 0.01f) {
      float maxRise = (limits.taperRecoveryPctS / 100.0f) * dtS;
      taperDisFactor = min(taperDisTarget, taperDisFactor + maxRise);
    }
  }
  if (taperDisFactor > 0.01f) taperDisZeroSince = 0;
}

// ===================== Night Mode =====================
static float nightModeFactor = 1.0f;  // multiplier for CCL (1.0 = day, nightCclPct/100 = night)
static float nightModeDclFactor = 1.0f;

static bool isNightTime() {
  if (!nightModeEnabled) return false;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 0)) return false;  // NTP not synced
  uint8_t hour = timeinfo.tm_hour;
  if (nightStartHour > nightEndHour) {
    // Crosses midnight: e.g., 23:00 - 07:00
    return (hour >= nightStartHour || hour < nightEndHour);
  } else {
    // Same day: e.g., 01:00 - 05:00
    return (hour >= nightStartHour && hour < nightEndHour);
  }
}

static void computeNightMode() {
  if (isNightTime()) {
    nightModeFactor = nightCclPct / 100.0f;
    nightModeDclFactor = nightDclPct / 100.0f;
  } else {
    nightModeFactor = 1.0f;
    nightModeDclFactor = 1.0f;
  }
}

// ===================== Balance Time Estimator =====================
// Returns estimated hours to balance, or -1 if not applicable
static float estimateBalanceTimeH() {
  if (packActiveCells < 2 || packDeltaV < 0.005f) return 0.0f;
  if (balanceCurrentA < 0.1f) return -1.0f;  // balance current not configured
  // Formula: Each mV of delta requires (delta_mV / balance_mA) hours per cell pair
  // Simplified: total_time ≈ (delta_V * 1000) / (balance_A * 1000) = delta_V / balance_A
  // But balancing happens on multiple cells, so divide by ~2 (top and bottom simultaneously)
  float hours = (packDeltaV / balanceCurrentA) / 2.0f;
  return hours;
}

// ===================== State machine =====================
static void controlTick() {
  static SysState prevState = ST_OPEN;
  static bool prevCritical = false;
  static bool prevAllowChg = true, prevAllowDsg = true;

  evaluateFaults();
  computePermissions();
  computeTaper();
  computeNightMode();

  // learning based on latest snapshot
  learningTick();

  // Log fault transitions
  if (criticalFault && !prevCritical) logEvent(EVT_FAULT, "CRITICAL: %s", faultText.c_str());
  else if (!criticalFault && prevCritical) logEvent(EVT_FAULT_CLEAR, "Critical cleared");
  prevCritical = criticalFault;

  if (allowCharge != prevAllowChg) { logEvent(EVT_PERM, "ALLOW_CHG: %s", allowCharge?"YES":"NO"); prevAllowChg = allowCharge; }
  if (allowDischarge != prevAllowDsg) { logEvent(EVT_PERM, "ALLOW_DSG: %s", allowDischarge?"YES":"NO"); prevAllowDsg = allowDischarge; }

  uint32_t now = millis();

  // Determine target state and contactors
  SysState targetState = state;
  bool targetChg = false, targetDsg = false;

  if (criticalFault) {
    targetState = ST_TRIPPED;
    targetChg = false;
    targetDsg = false;
  } else if (!armed) {
    targetState = ST_OPEN;
    targetChg = false;
    targetDsg = false;
  } else if (state == ST_CLOSED) {
    targetState = ST_CLOSED;
    targetChg = allowCharge;
    targetDsg = allowDischarge;
  }

  // Check if we need to open any contactor that is currently closed
  bool needToOpenChg = (cChg && !targetChg);
  bool needToOpenDsg = (cDsg && !targetDsg);
  bool needToOpenAny = needToOpenChg || needToOpenDsg;

  // Soft disconnect logic: wait before opening contactors to let inverter reduce current
  if (needToOpenAny && !softDisconnectActive && (state == ST_CLOSED || state == ST_PRECHARGING)) {
    // Start soft disconnect
    softDisconnectActive = true;
    softDisconnectStartMs = now;
    softDisconnectTargetState = targetState;
    softDisconnectTargetChg = targetChg;
    softDisconnectTargetDsg = targetDsg;
    logEvent(EVT_STATE, "SOFT_DISCONNECT: waiting 5s for inverter (CCL/DCL=0)");
  }

  if (softDisconnectActive) {
    // Cancel soft disconnect if the condition that triggered it has cleared
    // (e.g. permission restored because cell voltage dropped after CCL=0)
    if (!needToOpenAny && !criticalFault && armed) {
      softDisconnectActive = false;
      logEvent(EVT_STATE, "SOFT_DISCONNECT: cancelled (condition cleared)");
      // Fall through to normal state machine
    } else if (now - softDisconnectStartMs >= SOFT_DISCONNECT_TIMEOUT_MS) {
      // Timeout reached, now open contactors
      logEvent(EVT_STATE, "SOFT_DISCONNECT: complete, opening contactors");
      softDisconnectActive = false;
      state = softDisconnectTargetState;
      if (state == ST_TRIPPED || state == ST_OPEN) {
        setContactors(false, false, false, false);
      } else {
        setContactors(true, false, softDisconnectTargetChg, softDisconnectTargetDsg);
      }
    }
    // While waiting, don't change contactors but update target if situation changes
    if (softDisconnectActive && criticalFault) {
      softDisconnectTargetState = ST_TRIPPED;
      softDisconnectTargetChg = false;
      softDisconnectTargetDsg = false;
    } else if (softDisconnectActive && !armed) {
      softDisconnectTargetState = ST_OPEN;
      softDisconnectTargetChg = false;
      softDisconnectTargetDsg = false;
    }
    // Don't proceed with normal state machine while soft disconnecting
    if (softDisconnectActive) {
      ui_rev++;
      if (state != prevState) {
        const char* sn[] = {"OPEN","PRECHARGING","CLOSED","TRIPPED"};
        logEvent(EVT_STATE, "STATE: %s -> %s", sn[prevState], sn[state]);
        prevState = state;
      }
      return;
    }
  }

  // Normal state machine (no soft disconnect active)
  switch (state) {
    case ST_OPEN:
      if (armed && !criticalFault) {
        prechargeStartMs = now;
        state = ST_PRECHARGING;
        setContactors(false, true, false, false);
      } else {
        setContactors(false, false, false, false);
      }
      break;

    case ST_PRECHARGING:
      if (criticalFault) {
        state = ST_TRIPPED;
        setContactors(false, false, false, false);
      } else if (!armed) {
        state = ST_OPEN;
        setContactors(false, false, false, false);
      } else if (now - prechargeStartMs >= limits.prechargeMs) {
        state = ST_CLOSED;
        setContactors(true, false, allowCharge, allowDischarge);
      }
      break;

    case ST_CLOSED:
      // Opening is handled by soft disconnect above; here we only handle closing or staying
      setContactors(true, false, allowCharge, allowDischarge);
      break;

    case ST_TRIPPED:
    default:
      setContactors(false, false, false, false);
      // Allow recovery from trip if fault clears and still armed
      if (!criticalFault && armed) {
        state = ST_OPEN;
      }
      break;
  }

  if (state != prevState) {
    const char* sn[] = {"OPEN","PRECHARGING","CLOSED","TRIPPED"};
    logEvent(EVT_STATE, "STATE: %s -> %s", sn[prevState], sn[state]);
    prevState = state;
  }

  ui_rev++;
}

// ===================== Web UI =====================
static String langLinks() {
  String s;
  s += "<a class='btn btn-ghost' href=\"/setlang?l=es\">ES</a>";
  s += "<a class='btn btn-ghost' href=\"/setlang?l=en\">EN</a>";
  s += "<a class='btn btn-ghost' href=\"/setlang?l=de\">DE</a>";
  return s;
}

static String htmlHeader(const String& title) {
  String h;
  h += "<!doctype html><html><head><meta charset='utf-8'>";
  h += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  h += "<title>" + title + "</title>";
  h += R"(<style>
:root{
  --bg:#0f172a;--card-bg:#1e293b;--text:#e2e8f0;--text-muted:#94a3b8;
  --border:#334155;--accent:#3b82f6;--success:#22c55e;--warning:#f59e0b;--danger:#ef4444;
}
*{box-sizing:border-box;margin:0;padding:0;}
body{font-family:'Inter',system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--text);padding:16px;line-height:1.5;min-height:100vh;}
a{color:var(--accent);text-decoration:none;}
a:hover{text-decoration:underline;}
.header{display:flex;flex-wrap:wrap;justify-content:space-between;align-items:center;padding-bottom:16px;margin-bottom:20px;border-bottom:1px solid var(--border);gap:12px;}
.header h1{font-size:1.5rem;font-weight:700;}
.nav{display:flex;flex-wrap:wrap;gap:8px;}
.btn{display:inline-block;padding:8px 16px;border-radius:8px;font-weight:500;font-size:14px;cursor:pointer;transition:all .2s;border:none;text-decoration:none !important;}
.btn:hover{filter:brightness(1.15);transform:translateY(-1px);}
.btn-primary{background:var(--accent);color:#fff;}
.btn-success{background:var(--success);color:#fff;}
.btn-danger{background:var(--danger);color:#fff;}
.btn-ghost{background:transparent;color:var(--text-muted);border:1px solid var(--border);}
.btn-ghost:hover{background:var(--card-bg);color:var(--text);}
.grid{display:grid;gap:16px;}
.grid-2{grid-template-columns:1fr;}
.grid-4{grid-template-columns:1fr;}
.card{background:var(--card-bg);border:1px solid var(--border);border-radius:12px;padding:20px;box-shadow:0 4px 6px -1px rgba(0,0,0,.3);}
.card-title{font-size:13px;color:var(--text-muted);text-transform:uppercase;letter-spacing:.05em;margin-bottom:16px;display:flex;align-items:center;gap:8px;}
.card-title span{font-size:18px;}
.kpi{display:flex;flex-direction:column;gap:10px;}
.kpi-row{display:flex;justify-content:space-between;align-items:center;padding:6px 0;border-bottom:1px solid var(--border);}
.kpi-row:last-child{border-bottom:none;}
.kpi-label{color:var(--text-muted);font-size:13px;}
.kpi-value{font-weight:600;font-size:15px;}
.stat-big{font-size:28px;font-weight:700;}
.dot{display:inline-block;width:12px;height:12px;border-radius:50%;margin-right:8px;box-shadow:0 0 8px currentColor;}
.dot-on{background:var(--success);color:var(--success);}
.dot-off{background:var(--danger);color:var(--danger);}
.alert{padding:16px;border-radius:12px;margin-bottom:20px;display:flex;align-items:center;gap:12px;}
.alert-ok{background:rgba(34,197,94,.12);border:1px solid var(--success);}
.alert-danger{background:rgba(239,68,68,.12);border:1px solid var(--danger);}
.alert-warning{background:rgba(245,158,11,.12);border:1px solid var(--warning);}
.alert b{font-weight:600;}
.cell-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(72px,1fr));gap:8px;background:var(--card-bg);border:1px solid var(--border);border-radius:12px;padding:16px;}
.cell{padding:10px 6px;border-radius:8px;text-align:center;transition:all .2s;cursor:default;position:relative;}
.cell:hover{transform:scale(1.05);z-index:10;box-shadow:0 4px 12px rgba(0,0,0,.4);}
.cell-num{font-size:10px;font-weight:600;opacity:.8;margin-bottom:2px;}
.cell-v{font-size:14px;font-weight:700;letter-spacing:-.02em;}
.cell-id{font-size:9px;opacity:.6;margin-top:2px;}
.cell-bars{background:var(--card-bg);border:1px solid var(--border);border-radius:12px;padding:16px;display:flex;align-items:flex-end;gap:3px;overflow-x:auto;height:220px;}
.cell-bar-col{display:flex;flex-direction:column;align-items:center;flex:1;min-width:18px;height:100%;}
.cell-bar-wrap{flex:1;width:100%;background:var(--bg);border-radius:3px 3px 0 0;position:relative;display:flex;align-items:flex-end;}
.cell-bar{width:100%;border-radius:3px 3px 0 0;transition:height .3s ease;}
.cell-bar-val{font-size:9px;font-weight:600;color:var(--text);writing-mode:vertical-rl;text-orientation:mixed;position:absolute;top:4px;left:50%;transform:translateX(-50%);text-shadow:0 1px 2px rgba(0,0,0,.7);}
.cell-bar-num{font-size:9px;font-weight:600;color:var(--text-muted);margin-top:4px;}
.view-toggle{display:inline-flex;background:var(--bg);border-radius:6px;padding:2px;margin-left:auto;}
.view-toggle button{background:transparent;border:none;padding:6px 12px;font-size:12px;color:var(--text-muted);cursor:pointer;border-radius:4px;transition:all .2s;}
.view-toggle button.active{background:var(--accent);color:#fff;}
.section-title{font-size:16px;font-weight:600;margin:24px 0 12px;color:var(--text);display:flex;align-items:center;gap:8px;}
.section-title span{font-size:20px;}
input,select{background:var(--bg);border:1px solid var(--border);border-radius:8px;padding:10px 12px;color:var(--text);font-size:14px;width:100%;margin-bottom:12px;}
input:focus,select:focus{outline:none;border-color:var(--accent);box-shadow:0 0 0 3px rgba(59,130,246,.2);}
label{display:block;color:var(--text-muted);font-size:13px;margin-bottom:4px;}
.form-grid{display:grid;grid-template-columns:1fr 1fr;gap:12px 24px;}
button[type=submit]{background:var(--accent);color:#fff;border:none;padding:12px 24px;border-radius:8px;font-weight:600;cursor:pointer;transition:all .2s;}
button[type=submit]:hover{filter:brightness(1.1);}
@media(min-width:640px){.grid-2{grid-template-columns:repeat(2,1fr);}.grid-4{grid-template-columns:repeat(2,1fr);}}
@media(min-width:1024px){.grid-4{grid-template-columns:repeat(4,1fr);}body{padding:24px;max-width:1400px;margin:0 auto;}}
@media(max-width:639px){.header{flex-direction:column;align-items:flex-start;}.nav{width:100%;justify-content:flex-start;}.form-grid{grid-template-columns:1fr;}}
.chart-container{background:var(--card-bg);border:1px solid var(--border);border-radius:12px;padding:16px;height:220px;position:relative;}
.chart-grid{display:grid;grid-template-columns:1fr;gap:16px;}
@media(min-width:768px){.chart-grid{grid-template-columns:repeat(3,1fr);}}
</style>)";
  h += "<script src='https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js'></script>";
  h += "</head><body>";
  h += "<div class='header'><h1>" + title + "</h1>";
  h += "<div class='nav'>";
  h += "<a class='btn btn-ghost' href=\"/\">" + String(T(S_STATUS)) + "</a>";
  h += "<a class='btn btn-ghost' href=\"/config\">" + String(T(S_SETTINGS)) + "</a>";
  h += "<a class='btn btn-success' href=\"/actions?arm=1\">" + String(T(S_ARM)) + "</a>";
  h += "<a class='btn btn-danger' href=\"/actions?arm=0\">" + String(T(S_DISARM)) + "</a>";
  h += "<a class='btn btn-ghost' href=\"/actions?reset=1\">" + String(T(S_RESET_TRIP)) + "</a>";
  h += "<a class='btn btn-ghost' href=\"/actions?resetUserCounters=1\">" + String(T(S_RESET_COUNTERS)) + "</a>";
  h += "</div></div>";
  return h;
}

static void handleRoot() {
  requireAnyUser();
  if (!isUserOrAdmin()) return;

  float sohPct = (ecfg.capacityAh > 0.1f) ? (ecfg.sohAh / ecfg.capacityAh) * 100.0f : 100.0f;

  String html = htmlHeader(String("🔋 ") + T(S_TITLE));

  // Alert box (dynamically updated by JS)
  {
    const char* alertClass = criticalFault ? "alert alert-danger" : directionalFault ? "alert alert-warning" : "alert alert-ok";
    String alertLabel = criticalFault ? String(T(S_ALERT_TRIP)) : directionalFault ? String(T(S_WARN)) : String(T(S_OK));
    String alertDetail = (criticalFault || directionalFault) ? faultText : String(T(S_NO_FAULTS));
    html += "<div id='alertBox' class='" + String(alertClass) + "'><b id='alertLabel'>" + alertLabel + "</b> &mdash; <span id='fault'>" + alertDetail + "</span></div>";
  }

  html += "<div class='grid grid-4'>";

  // State card
  html += "<div class='card'>";
  html += "<div class='card-title'><span>🧠</span> " + String(T(S_STATE)) + "</div>";
  html += "<div class='kpi'>";
  html += "<div class='kpi-row'><span class='kpi-label'>" + String(T(S_ARMED)) + "</span><span class='kpi-value'>" + String(armed ? "YES" : "NO") + "</span></div>";
  html += "<div class='kpi-row'><span class='kpi-label'>State</span><span class='kpi-value'>" + String(state==ST_OPEN?"OPEN":state==ST_PRECHARGING?"PRECHARGING":state==ST_CLOSED?"CLOSED":"TRIPPED") + "</span></div>";
  html += "<div class='kpi-row'><span class='kpi-label'>" + String(T(S_IDS)) + "</span><span class='kpi-value' id='ids'>" + (activeIdsCsv.length()?activeIdsCsv:String("-")) + "</span></div>";
  float effCclPct = taperChgFactor * nightModeFactor * 100.0f;
  float effDclPct = taperDisFactor * nightModeDclFactor * 100.0f;
  html += "<div class='kpi-row'><span class='kpi-label'>" + String(T(S_ALLOW_CHG)) + "</span><span class='kpi-value'>" + String(allowCharge ? "YES" : "NO") + " <span id='tpChg' style='color:var(--text-muted);font-size:12px;'>" + (effCclPct < 99.9f ? String("(") + String((int)effCclPct) + "%)" : String("")) + "</span></span></div>";
  html += "<div class='kpi-row'><span class='kpi-label'>" + String(T(S_ALLOW_DSG)) + "</span><span class='kpi-value'>" + String(allowDischarge ? "YES" : "NO") + " <span id='tpDis' style='color:var(--text-muted);font-size:12px;'>" + (effDclPct < 99.9f ? String("(") + String((int)effDclPct) + "%)" : String("")) + "</span></span></div>";
  if (isNightTime()) {
    html += "<div class='kpi-row'><span class='kpi-label'>🌙 " + String(T(S_NIGHT_MODE)) + "</span><span class='kpi-value' style='color:var(--accent);'>Active</span></div>";
  }
  html += "<div class='kpi-row'><span class='kpi-label'>" + String(T(S_LEARN)) + "</span><span class='kpi-value'>" + String(learnPhase==LEARN_FROM_FULL ? "RUNNING" : "IDLE") + "</span></div>";
  html += "<div class='kpi-row'><span class='kpi-label'>CAN2 (Inverter)</span><span class='kpi-value' id='can2st'>" + String(mcp2515_ok ? "<span style='color:var(--ok)'>OK</span>" : "<span style='color:var(--danger)'>FAIL</span>") + "</span></div>";
  html += "<div class='kpi-row'><span class='kpi-label'>CAN2 TX/RX/ERR</span><span class='kpi-value'><span id='mcpTx'>" + String(mcp2515_txCount) + "</span> / <span id='mcpRx'>" + String(mcp2515_rxCount) + "</span> / <span id='mcpErr'>" + String(mcp2515_errCount) + "</span></span></div>";
  {char hb[12]; snprintf(hb,sizeof(hb),"0x%08lX",(unsigned long)mcp2515_lastRxId);
  html += "<div class='kpi-row'><span class='kpi-label'>CAN2 Last RX</span><span class='kpi-value'><span id='mcpLastId'>" + String(hb) + "</span> [<span id='mcpLastData'>--</span>]</span></div>";
  html += "<div class='kpi-row'><span class='kpi-label'>CAN2 All IDs</span><span class='kpi-value' id='mcpAllIds' style='font-size:11px;'>--</span></div>";}
  if(invProto==INV_PYLON_HV){
    char pbuf[80]; snprintf(pbuf,sizeof(pbuf),"CVL=%u DVL=%u CCL=%u DCL=%u T=%d",pylonDbg_CVL,pylonDbg_DVL,pylonDbg_CCL,pylonDbg_DCL,pylonDbg_temp);
    html += "<div class='kpi-row'><span class='kpi-label'>Pylon TX</span><span class='kpi-value' id='pylonTx' style='font-size:11px;'>" + String(pbuf) + "</span></div>";
    html += "<div class='kpi-row'><span class='kpi-label'>Pylon StdIDs</span><span class='kpi-value'>" + String(pylonUseStdIds ? "YES" : "NO") + "</span></div>";
  }
  html += "</div></div>";

  // Contactors card
  html += "<div class='card'>";
  html += "<div class='card-title'><span>🔌</span> " + String(T(S_CONTACTORS)) + "</div>";
  html += "<div class='kpi'>";
  html += "<div class='kpi-row'><span class='kpi-label'><span id='dotPre' class='dot " + String(cPre?"dot-on":"dot-off") + "'></span>" + String(T(S_PRECHARGE)) + "</span><span class='kpi-value' id='lblPre'>" + String(cPre?"Closed":"Open") + "</span></div>";
  html += "<div class='kpi-row'><span class='kpi-label'><span id='dotChg' class='dot " + String(cChg?"dot-on":"dot-off") + "'></span>" + String(T(S_CHARGE)) + "</span><span class='kpi-value' id='lblChg'>" + String(cChg?"Closed":"Open") + "</span></div>";
  html += "<div class='kpi-row'><span class='kpi-label'><span id='dotDsg' class='dot " + String(cDsg?"dot-on":"dot-off") + "'></span>" + String(T(S_DISCHARGE)) + "</span><span class='kpi-value' id='lblDsg'>" + String(cDsg?"Closed":"Open") + "</span></div>";
  html += "</div></div>";

  // Pack summary card
  html += "<div class='card'>";
  html += "<div class='card-title'><span>📊</span> " + String(T(S_PACK_SUMMARY)) + "</div>";
  html += "<div class='kpi'>";
  html += "<div class='kpi-row'><span class='kpi-label'>" + String(T(S_TOTAL_V)) + "</span><span class='kpi-value'><span id='packV'>" + String(packTotalV, 2) + "</span> V</span></div>";
  html += "<div class='kpi-row'><span class='kpi-label'>" + String(T(S_CELL_MIN)) + "</span><span class='kpi-value'><span id='minCell'>" + String(packMinCellV, 3) + "</span> V <span style='color:var(--text-muted);font-size:12px;' id='minCellNum'>(BMS" + String(packMinCellModule) + "-" + String(packMinCellIndex) + ")</span></span></div>";
  html += "<div class='kpi-row'><span class='kpi-label'>" + String(T(S_CELL_MAX)) + "</span><span class='kpi-value'><span id='maxCell'>" + String(packMaxCellV, 3) + "</span> V <span style='color:var(--text-muted);font-size:12px;' id='maxCellNum'>(BMS" + String(packMaxCellModule) + "-" + String(packMaxCellIndex) + ")</span></span></div>";
  html += "<div class='kpi-row'><span class='kpi-label'>" + String(T(S_CELL_AVG)) + "</span><span class='kpi-value'><span id='avgCell'>" + String(packAvgCellV, 3) + "</span> V</span></div>";
  html += "<div class='kpi-row'><span class='kpi-label'>" + String(T(S_DELTA)) + "</span><span class='kpi-value'><span id='delta'>" + String(packDeltaV, 3) + "</span> V <span id='balanceBadge' style='font-size:11px;padding:2px 6px;border-radius:4px;margin-left:6px;'></span></span></div>";
  html += "<div id='balanceWarning' style='display:none;background:rgba(245,158,11,0.15);border-left:3px solid #f59e0b;padding:8px 12px;margin:8px 0;font-size:12px;color:#fbbf24;border-radius:0 4px 4px 0;'></div>";
  float balTimeH = estimateBalanceTimeH();
  if (balTimeH > 0.01f) {
    String balTimeStr = (balTimeH < 1.0f) ? String((int)(balTimeH * 60)) + " min" : String(balTimeH, 1) + " h";
    html += "<div class='kpi-row'><span class='kpi-label'>" + String(T(S_BAL_TIME_EST)) + "</span><span class='kpi-value' style='color:var(--text-muted);'>" + balTimeStr + "</span></div>";
  }
  html += "<div class='kpi-row'><span class='kpi-label'>" + String(T(S_TEMP_AVG)) + "</span><span class='kpi-value'><span id='tavg'>" + String(packAvgTempC, 1) + "</span> °C</span></div>";
  html += "<div class='kpi-row'><span class='kpi-label'>" + String(T(S_ACTIVE_CELLS)) + "</span><span class='kpi-value' id='cellsActive'>" + String(packActiveCells) + "</span></div>";
  html += "</div></div>";

  // Energy card
  html += "<div class='card'>";
  html += "<div class='card-title'><span>⚡</span> " + String(T(S_ENERGY)) + "</div>";
  html += "<div class='kpi'>";
  html += "<div class='kpi-row'><span class='kpi-label'>" + String(T(S_SOC)) + "</span><span class='kpi-value stat-big'><span id='soc'>" + String(socPct, 1) + "</span>%</span></div>";
  html += "<div class='kpi-row'><span class='kpi-label'>" + String(T(S_SOH)) + "</span><span class='kpi-value'>" + String(sohPct, 1) + "% (" + String(ecfg.sohAh,1) + " Ah)</span></div>";
  html += "<div class='kpi-row'><span class='kpi-label'>" + String(T(S_CYCLES)) + "</span><span class='kpi-value' id='cycles'>" + String(cycleCountEq, 2) + "</span></div>";
  html += "<div class='kpi-row'><span class='kpi-label'>" + String(T(S_CURRENT)) + "</span><span class='kpi-value'><span id='currentA'>" + (currentValid ? String((float)current_mA/1000.0f, 3) : String("0.000")) + "</span> A</span></div>";
  html += "<div class='kpi-row'><span class='kpi-label'>" + String(T(S_POWER)) + "</span><span class='kpi-value'><span id='powerW'>" + String(power_W, 1) + "</span> W</span></div>";
  html += "<div class='kpi-row'><span class='kpi-label'>Learn dAh</span><span class='kpi-value'>" + String(learnDischargeAh, 2) + " Ah</span></div>";
  html += "</div></div>";

  html += "</div>"; // end grid

  // User counters section
  html += "<div class='section-title'><span>📊</span> " + String(T(S_USER_COUNTERS)) + "</div>";
  html += "<div class='grid grid-2'>";
  html += "<div class='card'><div class='kpi'>";
  html += "<div class='kpi-row'><span class='kpi-label'>" + String(T(S_CHARGE_AH)) + "</span><span class='kpi-value stat-big'><span id='uChgAh'>" + String(user_chargeAh, 2) + "</span> Ah</span></div>";
  html += "<div class='kpi-row'><span class='kpi-label'>kWh</span><span class='kpi-value'><span id='uChgKwh'>" + String(user_chargeWh / 1000.0f, 3) + "</span> kWh</span></div>";
  html += "</div></div>";
  html += "<div class='card'><div class='kpi'>";
  html += "<div class='kpi-row'><span class='kpi-label'>" + String(T(S_DISCHARGE_AH)) + "</span><span class='kpi-value stat-big'><span id='uDsgAh'>" + String(user_dischargeAh, 2) + "</span> Ah</span></div>";
  html += "<div class='kpi-row'><span class='kpi-label'>kWh</span><span class='kpi-value'><span id='uDsgKwh'>" + String(user_dischargeWh / 1000.0f, 3) + "</span> kWh</span></div>";
  html += "</div></div>";
  html += "</div>";

  // Cell voltage section with view toggle
  html += "<div class='section-title'><span>🔋</span> " + String(T(S_CELL_VOLTAGES));
  html += "<div class='view-toggle'><button id='btnGrid'>Grid</button><button id='btnBars' class='active'>Bars</button></div></div>";
  html += "<div id='cellGrid' class='cell-grid' style='display:none;'></div>";
  html += "<div id='cellBars' class='cell-bars'></div>";

  // Real-time charts section with range selector
  html += "<div class='section-title'><span>📈</span> Real-Time Charts";
  html += "<select id='selRange' style='margin-left:auto;font-size:12px;padding:4px 8px;background:var(--card);color:var(--text);border:1px solid var(--border);border-radius:4px;'>";
  html += "<option value='300'>5 min</option><option value='900'>15 min</option><option value='1800' selected>30 min</option><option value='3600'>1 hour</option></select>";
  html += "<button id='btnHistory' class='btn btn-ghost' style='margin-left:8px;font-size:12px;padding:6px 12px;'>Load</button></div>";
  html += "<div class='chart-grid'>";
  html += "<div class='chart-container'><canvas id='chartVoltage'></canvas></div>";
  html += "<div class='chart-container'><canvas id='chartCurrent'></canvas></div>";
  html += "<div class='chart-container'><canvas id='chartSoc'></canvas></div>";
  html += "</div>";

  // Event log section
  html += "<div class='section-title'><span>📋</span> " + String(T(S_EVENT_LOG)) +
          " <button id='btnDlLog' class='btn btn-ghost' style='margin-left:auto;font-size:12px;padding:6px 12px;'>" +
          String(T(S_DOWNLOAD_LOG)) + "</button>" +
          " <a class='btn btn-danger' style='font-size:12px;padding:6px 12px;' href='/actions?clearEventLog=1' onclick=\"return confirm('Clear event log?');\">" +
          String(T(S_CLEAR_LOG)) + "</a></div>";
  html += "<div class='card'><div id='evtLog' style='font-family:monospace;font-size:12px;max-height:300px;overflow-y:auto;color:var(--text-muted);'>Loading...</div></div>";

  // Inject i18n labels for JS alert updates
  html += "<script>const _L_OK='" + String(T(S_OK)) + "',_L_WARN='" + String(T(S_WARN)) + "',_L_TRIP='" + String(T(S_ALERT_TRIP)) + "',_L_NOFAULT='" + String(T(S_NO_FAULTS)) + "';</script>";

  // Live update (polls /api/status and updates DOM only when rev changes)
  html += R"JS(
<script>
let lastRev = -1;

function setText(id, val){
  const el = document.getElementById(id);
  if(!el) return;
  const s = String(val);
  if(el.textContent !== s) el.textContent = s;
}
function setDot(id, closed){
  const el = document.getElementById(id);
  if(!el) return;
  el.className = closed ? 'dot dot-on' : 'dot dot-off';
  const lbl = document.getElementById(id.replace('dot','lbl'));
  if(lbl) lbl.textContent = closed ? 'Closed' : 'Open';
}
function colorForV(v){
  if(v >= 3.45) return {bg:'rgba(34,197,94,.25)',border:'#22c55e',text:'#4ade80'};
  if(v >= 3.20) return {bg:'rgba(245,158,11,.2)',border:'#f59e0b',text:'#fbbf24'};
  return {bg:'rgba(239,68,68,.2)',border:'#ef4444',text:'#f87171'};
}
function barGradient(v, minV, maxV){
  const range = maxV - minV;
  const t = range > 0.001 ? (v - minV) / range : 0.5;
  const hue = Math.round(t * 120);
  return `hsl(${hue}, 80%, 45%)`;
}
let cellViewMode = 'bars';
let lastCells = [];
function renderCells(cells){
  const grid = document.getElementById('cellGrid');
  if(!grid) return;
  let html = "";
  for(const c of cells){
    const v = c.v;
    const col = colorForV(v);
    const title = `BMS ${c.id} | Cell ${c.l} | Global #${c.g}`;
    html += `<div class="cell" title="${title}" style="background:${col.bg};border:1px solid ${col.border};">`;
    html += `<div class="cell-num" style="color:${col.text}">#${c.g}</div>`;
    html += `<div class="cell-v" style="color:${col.text}">${v.toFixed(3)}</div>`;
    html += `<div class="cell-id">BMS${c.id}</div>`;
    html += `</div>`;
  }
  grid.innerHTML = html;
}
function renderCellsBars(cells){
  const bars = document.getElementById('cellBars');
  if(!bars || cells.length === 0) return;
  const minV = Math.min(...cells.map(c => c.v));
  const maxV = Math.max(...cells.map(c => c.v));
  const scaleMin = Math.max(2.5, minV - 0.05);
  const scaleMax = maxV + 0.05;
  let html = "";
  for(const c of cells){
    const v = c.v;
    const pct = ((v - scaleMin) / (scaleMax - scaleMin)) * 100;
    const color = barGradient(v, minV, maxV);
    const title = `BMS ${c.id} | Cell ${c.l} | Global #${c.g}`;
    html += `<div class="cell-bar-col" title="${title}">`;
    html += `<div class="cell-bar-wrap">`;
    html += `<div class="cell-bar" style="height:${pct.toFixed(1)}%;background:${color};"></div>`;
    html += `<div class="cell-bar-val">${v.toFixed(3)}</div>`;
    html += `</div>`;
    html += `<div class="cell-bar-num">#${c.g}</div>`;
    html += `</div>`;
  }
  bars.innerHTML = html;
}
function updateCellView(cells){
  lastCells = cells;
  if(cellViewMode === 'grid') renderCells(cells);
  else renderCellsBars(cells);
}
function toggleView(mode){
  cellViewMode = mode;
  const grid = document.getElementById('cellGrid');
  const bars = document.getElementById('cellBars');
  const btnGrid = document.getElementById('btnGrid');
  const btnBars = document.getElementById('btnBars');
  if(mode === 'grid'){
    if(grid) grid.style.display = '';
    if(bars) bars.style.display = 'none';
    if(btnGrid) btnGrid.classList.add('active');
    if(btnBars) btnBars.classList.remove('active');
    if(lastCells.length) renderCells(lastCells);
  } else {
    if(grid) grid.style.display = 'none';
    if(bars) bars.style.display = '';
    if(btnGrid) btnGrid.classList.remove('active');
    if(btnBars) btnBars.classList.add('active');
    if(lastCells.length) renderCellsBars(lastCells);
  }
}
document.getElementById('btnGrid')?.addEventListener('click', () => toggleView('grid'));
document.getElementById('btnBars')?.addEventListener('click', () => toggleView('bars'));

// Chart setup
const MAX_POINTS = 120;
const chartOpts = (title, color, autoScale = false) => ({
  responsive: true,
  maintainAspectRatio: false,
  animation: false,
  plugins: {
    legend: { display: false },
    title: { display: true, text: title, color: '#e2e8f0', font: { size: 14, weight: 600 } }
  },
  scales: {
    x: { display: false },
    y: autoScale ? { ticks: { color: '#94a3b8' }, grid: { color: '#334155' } } : { ticks: { color: '#94a3b8' }, grid: { color: '#334155' } }
  },
  elements: { point: { radius: 0 }, line: { tension: 0.3, borderWidth: 2 } }
});
const makeData = (color) => ({
  labels: Array(MAX_POINTS).fill(''),
  datasets: [{ data: [], borderColor: color, backgroundColor: color + '33', fill: true }]
});

let chartV, chartA, chartS;
function initCharts(){
  const ctxV = document.getElementById('chartVoltage');
  const ctxA = document.getElementById('chartCurrent');
  const ctxS = document.getElementById('chartSoc');
  if(!ctxV || !ctxA || !ctxS || typeof Chart === 'undefined') return;
  chartV = new Chart(ctxV, { type: 'line', data: makeData('#3b82f6'), options: chartOpts('Pack Voltage (V)', '#3b82f6', true) });
  chartA = new Chart(ctxA, { type: 'line', data: makeData('#22c55e'), options: chartOpts('Current (A)', '#22c55e', true) });
  chartS = new Chart(ctxS, { type: 'line', data: makeData('#f59e0b'), options: {...chartOpts('SoC (%)', '#f59e0b'), scales: {x:{display:false}, y:{min:0,max:100,ticks:{color:'#94a3b8'},grid:{color:'#334155'}}}} });
}
function pushChart(chart, val){
  if(!chart) return;
  const d = chart.data.datasets[0].data;
  d.push(val);
  if(d.length > MAX_POINTS) d.shift();
  chart.update('none');
}
function setChartData(chart, arr){
  if(!chart) return;
  chart.data.datasets[0].data = arr;
  chart.data.labels = arr.map((_,i) => i);
  chart.update('none');
}
async function loadHistory(){
  const btn = document.getElementById('btnHistory');
  const sel = document.getElementById('selRange');
  const range = sel ? sel.value : '1800';
  if(btn) btn.textContent = 'Loading...';
  try {
    const r = await fetch('/api/history?range=' + range, {cache:'no-store'});
    const h = await r.json();
    if(h.points && h.points.length > 0){
      setChartData(chartV, h.points.map(p => p.v));
      setChartData(chartA, h.points.map(p => p.a));
      setChartData(chartS, h.points.map(p => p.soc));
      const mins = Math.round(parseInt(range) / 60);
      if(btn) btn.textContent = h.points.length + ' pts (' + mins + 'm)';
    } else {
      if(btn) btn.textContent = 'No data';
    }
  } catch(e) {
    if(btn) btn.textContent = 'Error';
  }
  setTimeout(() => { if(btn) btn.textContent = 'Load'; }, 3000);
}
initCharts();
document.getElementById('btnHistory')?.addEventListener('click', loadHistory);
document.getElementById('selRange')?.addEventListener('change', loadHistory);

function updateBalanceIndicator(delta, minMod, minIdx) {
  const badge = document.getElementById('balanceBadge');
  const warn = document.getElementById('balanceWarning');
  if (!badge) return;
  let color, bg, text;
  if (delta < 0.020) { color = '#22c55e'; bg = 'rgba(34,197,94,0.2)'; text = 'Excellent'; }
  else if (delta < 0.050) { color = '#22c55e'; bg = 'rgba(34,197,94,0.2)'; text = 'Good'; }
  else if (delta < 0.100) { color = '#f59e0b'; bg = 'rgba(245,158,11,0.2)'; text = 'Fair'; }
  else if (delta < 0.200) { color = '#f97316'; bg = 'rgba(249,115,22,0.2)'; text = 'Poor'; }
  else { color = '#ef4444'; bg = 'rgba(239,68,68,0.2)'; text = 'Critical'; }
  badge.style.background = bg;
  badge.style.color = color;
  badge.textContent = text;
  if (warn) {
    if (delta >= 0.100) {
      const pct = Math.min(95, Math.round(delta * 200));
      warn.innerHTML = '<b>Imbalance alert:</b> Cell BMS' + minMod + '-' + minIdx + ' is limiting capacity (~' + pct + '% loss). Consider balancing.';
      warn.style.display = 'block';
    } else {
      warn.style.display = 'none';
    }
  }
}

async function tick(){
  try{
    const r = await fetch('/api/status', {cache:'no-store'});
    const j = await r.json();
    if(j.rev === lastRev) return;
    lastRev = j.rev;

    setText('ids', j.ids || '');
    setText('packV', j.packV.toFixed(2));
    setText('minCell', j.minCell.toFixed(3));
    setText('maxCell', j.maxCell.toFixed(3));
    setText('delta', j.delta.toFixed(3));
    updateBalanceIndicator(j.delta, j.minCellMod, j.minCellIdx);
    setText('tavg', j.tavg.toFixed(1));
    setText('cellsActive', j.cellsActive);

    setText('soc', j.soc.toFixed(1));
    setText('cycles', j.cycles.toFixed(2));
    setText('currentA', j.currentA.toFixed(3));
    setText('powerW', j.powerW.toFixed(1));
    setText('avgCell', j.avgCell.toFixed(3));
    setText('minCellNum', '(BMS'+j.minCellMod+'-'+j.minCellIdx+')');
    setText('maxCellNum', '(BMS'+j.maxCellMod+'-'+j.maxCellIdx+')');
    setText('uChgAh', j.uChgAh.toFixed(2));
    setText('uDsgAh', j.uDsgAh.toFixed(2));
    setText('uChgKwh', j.uChgKwh.toFixed(3));
    setText('uDsgKwh', j.uDsgKwh.toFixed(3));

    // Update alert banner class and label dynamically
    const alertBox = document.getElementById('alertBox');
    const alertLabel = document.getElementById('alertLabel');
    const faultSpan = document.getElementById('fault');
    if(alertBox){
      if(j.critical){
        alertBox.className='alert alert-danger';
        if(alertLabel) alertLabel.textContent=_L_TRIP;
        if(faultSpan) faultSpan.textContent=j.fault||'';
      } else if(j.directional){
        alertBox.className='alert alert-warning';
        if(alertLabel) alertLabel.textContent=_L_WARN;
        if(faultSpan) faultSpan.textContent=j.fault||'';
      } else {
        alertBox.className='alert alert-ok';
        if(alertLabel) alertLabel.textContent=_L_OK;
        if(faultSpan) faultSpan.textContent=_L_NOFAULT;
      }
    }

    var tc = document.getElementById('tpChg');
    if(tc) tc.textContent = j.taperChg < 0.999 ? '(' + Math.round(j.taperChg*100) + '%' + (j.cvlV > 0 ? ' CVL:' + j.cvlV + 'V' : '') + ')' : '';
    var td = document.getElementById('tpDis');
    if(td) td.textContent = j.taperDis < 0.999 ? '(' + Math.round(j.taperDis*100) + '%)' : '';

    setDot('dotPre', j.cPre);
    setDot('dotChg', j.cChg);
    setDot('dotDsg', j.cDsg);

    if(j.mcp2515tx!==undefined){
      setText('mcpTx', j.mcp2515tx);
      setText('mcpRx', j.mcp2515rx);
      setText('mcpErr', j.mcp2515err);
      setText('mcpLastId', j.mcpLastId||'');
      setText('mcpLastData', j.mcpLastData||'');
      setText('mcpAllIds', j.mcpAllIds||'');
      if(j.pylonTx) setText('pylonTx', j.pylonTx);
    }

    updateCellView(j.cells || []);

    // Update charts
    pushChart(chartV, j.packV);
    pushChart(chartA, j.currentA);
    pushChart(chartS, j.soc);
  }catch(e){}
}

setInterval(tick, 1000);
tick();

// Event log
const EVT_NAMES=['STATE','CONTACTOR','FAULT','CLEAR','ARM','DISARM','RESET','PERM'];
async function fetchEvents(){
  try{
    const r=await fetch('/api/events',{cache:'no-store'});
    const d=await r.json();
    const el=document.getElementById('evtLog');
    if(!el||!d.events.length){if(el)el.innerHTML='No events';return;}
    let h='<table style="width:100%;border-collapse:collapse;">';
    h+='<tr style="border-bottom:1px solid var(--border);"><th style="text-align:left;padding:4px;">Time</th><th style="text-align:left;padding:4px;">Type</th><th style="text-align:left;padding:4px;">Event</th></tr>';
    for(const e of d.events){
      const ts=e.tsStr||('uptime '+e.ts+'s');
      h+=`<tr style="border-bottom:1px solid var(--border);"><td style="padding:4px;">${ts}</td><td style="padding:4px;">${EVT_NAMES[e.type]||'?'}</td><td style="padding:4px;">${e.msg}</td></tr>`;
    }
    h+='</table>';
    el.innerHTML=h;
    el.scrollTop=el.scrollHeight;
  }catch(e){}
}
setTimeout(fetchEvents,1500);
setInterval(fetchEvents,5000);

document.getElementById('btnDlLog')?.addEventListener('click',async()=>{
  try{
    const r=await fetch('/api/events',{cache:'no-store'});
    const d=await r.json();
    let csv='Time,Type,Event\n';
    for(const e of d.events){
      const ts=e.tsStr||('uptime '+e.ts+'s');
      csv+=`${ts},${EVT_NAMES[e.type]||'?'},"${e.msg}"\n`;
    }
    const b=new Blob([csv],{type:'text/csv'});
    const a=document.createElement('a');
    a.href=URL.createObjectURL(b);
    a.download='bms_events_'+Date.now()+'.csv';
    a.click();
    URL.revokeObjectURL(a.href);
  }catch(e){alert('Download failed');}
});
</script>
)JS";

  html += "</body></html>";
  server.send(200, "text/html", html);
}

static void handleConfig() {
  if (!isAdmin()) { requireAdmin(); return; }

  String html = htmlHeader(String("⚙️ ") + T(S_SETTINGS));

  // Quick settings row
  html += "<div class='grid grid-2' style='margin-bottom:20px;'>";

  html += "<div class='card'>";
  html += "<div class='card-title'><span>🌐</span> " + String(T(S_LANGUAGE)) + "</div>";
  html += "<div class='nav'>" + langLinks() + "</div>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<div class='card-title'><span>🔬</span> " + String(T(S_CHEMISTRY)) + "</div>";
  html += "<div class='nav'>";
  html += String("<a class='btn ") + (chem==CHEM_LFP ? "btn-primary" : "btn-ghost") + "' href=\"/applyPreset?chem=LFP\">" + String(T(S_APPLY_LFP)) + "</a>";
  html += String("<a class='btn ") + (chem==CHEM_NMC ? "btn-primary" : "btn-ghost") + "' href=\"/applyPreset?chem=NMC\">" + String(T(S_APPLY_NMC)) + "</a>";
  html += String("<a class='btn ") + (chem==CHEM_LTO ? "btn-primary" : "btn-ghost") + "' href=\"/applyPreset?chem=LTO\">" + String(T(S_APPLY_LTO)) + "</a>";
  html += "</div></div>";

  html += "</div>"; // end quick settings grid

  // Main form
  html += "<div class='card'>";
  html += "<div class='card-title'><span>⚙️</span> " + String(T(S_LIMITS)) + "</div>";
  html += "<form action='/save' method='GET'>";

  // -- Charge Protection --
  html += "<div class='section-title'><span>🔺</span> Charge Protection</div>";
  html += "<div class='form-grid'>";
  html += "<div><label>" + String(T(S_CELL_OV)) + " (V)</label><input name='cellOv' value='" + String(limits.cellOv, 3) + "'></div>";
  html += "<div><label>" + String(T(S_CELL_OV_RECOV)) + "</label><input name='cOvR' value='" + String(limits.cellOvRecov, 3) + "'></div>";
  html += "<div><label>" + String(T(S_PACK_OV)) + " (V)</label><input name='pOv' value='" + String(limits.packOv, 2) + "'></div>";
  html += "<div><label>" + String(T(S_PACK_OV_RECOV)) + "</label><input name='pOvR' value='" + String(limits.packOvRecov, 2) + "'></div>";
  html += "<div><label>" + String(T(S_TAPER_CHG_V)) + "</label><input name='tpChg' value='" + String(limits.taperChgStartV, 3) + "'></div>";
  html += "<div><label>" + String(T(S_TAPER_CHG_END_V)) + "</label><input name='tpChgE' value='" + String(limits.taperChgEndV, 3) + "'></div>";
  html += "<div><label>" + String(T(S_DYN_CVL_OFFSET)) + "</label><input name='dynCvl' type='number' step='0.1' min='0' max='10' value='" + String(limits.dynCvlOffsetV, 1) + "'></div>";
  html += "</div>";

  // -- Discharge Protection --
  html += "<div class='section-title'><span>🔻</span> Discharge Protection</div>";
  html += "<div class='form-grid'>";
  html += "<div><label>" + String(T(S_CELL_UV)) + " (V)</label><input name='cellUv' value='" + String(limits.cellUv, 3) + "'></div>";
  html += "<div><label>" + String(T(S_CELL_UV_RECOV)) + "</label><input name='cUvR' value='" + String(limits.cellUvRecov, 3) + "'></div>";
  html += "<div><label>" + String(T(S_PACK_UV)) + " (V)</label><input name='pUv' value='" + String(limits.packUv, 2) + "'></div>";
  html += "<div><label>" + String(T(S_PACK_UV_RECOV)) + "</label><input name='pUvR' value='" + String(limits.packUvRecov, 2) + "'></div>";
  html += "<div><label>" + String(T(S_TAPER_DIS_V)) + "</label><input name='tpDis' value='" + String(limits.taperDisStartV, 3) + "'></div>";
  html += "<div><label>" + String(T(S_TAPER_DIS_END_V)) + "</label><input name='tpDisE' value='" + String(limits.taperDisEndV, 3) + "'></div>";
  html += "</div>";

  // -- General Protection --
  html += "<div class='section-title'><span>🛡️</span> General Protection</div>";
  html += "<div class='form-grid'>";
  html += "<div><label>" + String(T(S_DELTA_MAX)) + " (V)</label><input name='dMax' value='" + String(limits.deltaMax, 3) + "'></div>";
  html += "<div><label>" + String(T(S_DELTA_RECOV)) + "</label><input name='dRec' value='" + String(limits.deltaRecov, 3) + "'></div>";
  html += "<div><label>" + String(T(S_TEMP_MIN)) + " (°C)</label><input name='tMin' value='" + String(limits.tempMin, 1) + "'></div>";
  html += "<div><label>" + String(T(S_TEMP_MAX)) + " (°C)</label><input name='tMax' value='" + String(limits.tempMax, 1) + "'></div>";
  html += "<div><label>" + String(T(S_COMM_TIMEOUT)) + " (ms)</label><input name='cto' value='" + String(limits.commTimeoutMs) + "'></div>";
  html += "<div><label>" + String(T(S_EXPECTED_CELLS)) + "</label><input name='expCells' type='number' min='0' value='" + String(limits.expectedCells) + "'></div>";
  html += "</div>";

  // -- Tapering Behavior --
  html += "<div class='section-title'><span>📉</span> Tapering Behavior</div>";
  html += "<div class='form-grid'>";
  html += "<div><label>" + String(T(S_TAPER_RECOVERY)) + "</label><input name='tpRec' type='number' step='0.1' min='0.1' max='100' value='" + String(limits.taperRecoveryPctS, 1) + "'></div>";
  html += "<div><label>" + String(T(S_TAPER_HOLD)) + "</label><input name='tpHold' type='number' min='0' max='60000' value='" + String(limits.taperHoldMs) + "'></div>";
  html += "</div>";

  // -- System --
  html += "<div class='section-title'><span>🔧</span> System</div>";
  html += "<div class='form-grid'>";
  html += "<div><label>" + String(T(S_PRECHARGE_MS)) + "</label><input name='pcms' value='" + String((unsigned)limits.prechargeMs) + "'></div>";
  html += "<div><label>" + String(T(S_BAL_CURRENT)) + "</label><input name='balA' type='number' step='0.1' min='0.1' max='5' value='" + String(balanceCurrentA, 1) + "'></div>";
  html += "</div>";

  html += "<div class='section-title'><span>🌙</span> " + String(T(S_NIGHT_MODE)) + "</div>";
  html += "<div class='form-grid'>";
  html += "<div><label><input type='checkbox' name='nightEn' " + String(nightModeEnabled ? "checked" : "") + "> " + String(T(S_NIGHT_MODE)) + "</label></div>";
  html += "<div><label>" + String(T(S_NIGHT_START)) + " (0-23)</label><input name='nightS' type='number' min='0' max='23' value='" + String(nightStartHour) + "'></div>";
  html += "<div><label>" + String(T(S_NIGHT_END)) + " (0-23)</label><input name='nightE' type='number' min='0' max='23' value='" + String(nightEndHour) + "'></div>";
  html += "<div><label>" + String(T(S_NIGHT_CCL)) + "</label><input name='nightCcl' type='number' min='0' max='100' value='" + String(nightCclPct) + "'></div>";
  html += "<div><label>" + String(T(S_NIGHT_DCL)) + "</label><input name='nightDcl' type='number' min='0' max='100' value='" + String(nightDclPct) + "'></div>";
  html += "</div>";

  html += "<div class='section-title'><span>⚡</span> " + String(T(S_ENERGY)) + "</div>";
  html += "<div class='form-grid'>";
  html += "<div><label>" + String(T(S_CAP_AH)) + "</label><input name='capAh' value='" + String(ecfg.capacityAh, 1) + "'></div>";
  html += "<div><label>" + String(T(S_SOH_AH)) + "</label><input name='sohAh' value='" + String(ecfg.sohAh, 1) + "'></div>";
  html += "<div><label>" + String(T(S_INIT_SOC)) + "</label><input name='initSoc' value='" + String(ecfg.initSocPct, 1) + "'></div>";
  html += "<div><label>" + String(T(S_CURR_POL)) + "</label><input name='cPol' value='" + String((int)ecfg.currentPolarity) + "'></div>";
  html += "<div><label>" + String(T(S_EFF)) + " (0.90-1.00)</label><input name='eff' value='" + String(ecfg.coulombEff, 3) + "'></div>";
  html += "</div>";

  html += "<div class='section-title'><span>📚</span> " + String(T(S_LEARN)) + "</div>";
  html += "<div class='form-grid'>";
  html += "<div><label>" + String(T(S_FULL_V)) + " (V)</label><input name='fV' value='" + String(ecfg.fullCellV, 3) + "'></div>";
  html += "<div><label>" + String(T(S_EMPTY_V)) + " (V)</label><input name='eV' value='" + String(ecfg.emptyCellV, 3) + "'></div>";
  html += "<div><label>" + String(T(S_TAIL_A)) + "</label><input name='tail' value='" + String(ecfg.tailCurrentA, 2) + "'></div>";
  html += "<div><label>" + String(T(S_HOLD_MS)) + "</label><input name='hold' value='" + String((unsigned)ecfg.holdMs) + "'></div>";
  html += "</div>";

  // Inverter protocol section (inside form)
  html += "<div class='section-title'><span>🔁</span> Inverter Protocol (CAN)</div>";
  html += "<p style='color:var(--text-muted);font-size:13px;margin-bottom:16px;'>CAN bus speed: 250 kbps.</p>";
  html += "<div class='form-grid'>";
  html += "<div><label>Protocol</label><select name='invP'>";
  auto optInv = [&](InverterProto p, const char* name){
    html += "<option value='" + String((int)p) + "'";
    if (invProto == p) html += " selected";
    html += ">" + String(name) + "</option>";
  };
  optInv(INV_OFF, "OFF");
  optInv(INV_BYD_HVS, "BYD Battery-Box Premium HVS");
  optInv(INV_PYLON_HV, "Pylontech Force H2 (HV)");
  optInv(INV_PYLON_LV, "Pylontech US2000 (0x35x)");
  optInv(INV_SMA_CAN, "SMA/Victron/Ingeteam (0x35x)");
  optInv(INV_SOFAR, "Sofar (placeholder)");
  optInv(INV_SOLAX, "Solax (placeholder)");
  optInv(INV_SUNGROW, "Sungrow (placeholder)");
  optInv(INV_GROWATT_HV, "Growatt HV (placeholder)");
  optInv(INV_GROWATT_LV, "Growatt LV (placeholder)");
  optInv(INV_FOXESS, "FoxESS (placeholder)");
  optInv(INV_SMA_TRIPOWER, "SMA Tripower (placeholder)");
  html += "</select></div>";
  html += "<div><label>Max Charge (A)</label><input name='invChgA' value='" + String(invUserMaxChgA) + "'></div>";
  html += "<div><label>Max Discharge (A)</label><input name='invDisA' value='" + String(invUserMaxDisA) + "'></div>";
  html += "<div><label>Charge Target V (dV, 0=auto)</label><input name='invChgV' value='" + String(invUserMaxChgV_dV) + "'></div>";
  html += "<div><label>Discharge Min V (dV, 0=auto)</label><input name='invDisV' value='" + String(invUserMinDisV_dV) + "'></div>";
  html += "</div>";

  // MQTT section
  html += "<div class='section-title'><span>📡</span> " + String(T(S_MQTT));
  // Status badge
  if (mqttEndpoint.length() == 0) {
    html += " <span style='font-size:12px;padding:2px 8px;border-radius:4px;background:#6b7280;color:#fff;margin-left:8px;'>Disabled</span>";
  } else if (mqttClient.connected()) {
    html += " <span style='font-size:12px;padding:2px 8px;border-radius:4px;background:#22c55e;color:#fff;margin-left:8px;'>Connected</span>";
  } else {
    html += " <span style='font-size:12px;padding:2px 8px;border-radius:4px;background:#ef4444;color:#fff;margin-left:8px;'>Disconnected</span>";
  }
  html += "</div>";
  html += "<div class='form-grid'>";
  html += "<div><label>" + String(T(S_MQTT_ENDPOINT)) + "</label><input name='mqttEp' value='" + mqttEndpoint + "' placeholder='broker.example.com'></div>";
  html += "<div><label>" + String(T(S_MQTT_PORT)) + "</label><input name='mqttPt' type='number' min='1' max='65535' value='" + String(mqttPort) + "'></div>";
  html += "<div><label>" + String(T(S_MQTT_INSECURE)) + "</label><input name='mqttIns' type='checkbox'" + String(mqttInsecure ? " checked" : "") + "></div>";
  html += "<div><label>" + String(T(S_MQTT_USER)) + "</label><input name='mqttUsr' value='" + mqttUser + "' autocomplete='off'></div>";
  html += "<div><label>" + String(T(S_MQTT_PASS)) + "</label><input name='mqttPwd' type='password' value='" + String(mqttPass.length() > 0 ? "****" : "") + "' autocomplete='off'></div>";
  html += "</div>";

  html += "<div style='margin-top:20px;display:flex;gap:12px;flex-wrap:wrap;'>";
  html += "<button type='submit'>" + String(T(S_SAVE)) + "</button>";
  html += "<a class='btn btn-danger' href='/actions?resetEnergy=1'>" + String(T(S_RESET_ENERGY)) + "</a>";
  html += "</div>";
  html += "</form></div>";



  // Live update (polls /api/status and updates DOM only when rev changes)
  html += R"JS(
<script>
let lastRev = -1;

function setText(id, val){
  const el = document.getElementById(id);
  if(!el) return;
  const s = String(val);
  if(el.textContent !== s) el.textContent = s;
}
function setDot(id, closed){
  const el = document.getElementById(id);
  if(!el) return;
  el.className = closed ? 'dot dot-on' : 'dot dot-off';
  const lbl = document.getElementById(id.replace('dot','lbl'));
  if(lbl) lbl.textContent = closed ? 'Closed' : 'Open';
}
function colorForV(v){
  if(v >= 3.45) return {bg:'rgba(34,197,94,.25)',border:'#22c55e',text:'#4ade80'};
  if(v >= 3.20) return {bg:'rgba(245,158,11,.2)',border:'#f59e0b',text:'#fbbf24'};
  return {bg:'rgba(239,68,68,.2)',border:'#ef4444',text:'#f87171'};
}
function renderCells(cells){
  const grid = document.getElementById('cellGrid');
  if(!grid) return;
  let html = "";
  for(const c of cells){
    const v = c.v;
    const col = colorForV(v);
    const title = `BMS ${c.id} | Cell ${c.l} | Global #${c.g}`;
    html += `<div class="cell" title="${title}" style="background:${col.bg};border:1px solid ${col.border};">`;
    html += `<div class="cell-num" style="color:${col.text}">#${c.g}</div>`;
    html += `<div class="cell-v" style="color:${col.text}">${v.toFixed(3)}</div>`;
    html += `<div class="cell-id">BMS${c.id}</div>`;
    html += `</div>`;
  }
  grid.innerHTML = html;
}

async function tick(){
  try{
    const r = await fetch('/api/status', {cache:'no-store'});
    const j = await r.json();
    if(j.rev === lastRev) return;
    lastRev = j.rev;

    setText('ids', j.ids || '');
    setText('packV', j.packV.toFixed(2));
    setText('minCell', j.minCell.toFixed(3));
    setText('maxCell', j.maxCell.toFixed(3));
    setText('delta', j.delta.toFixed(3));
    setText('tavg', j.tavg.toFixed(1));
    setText('cellsActive', j.cellsActive);

    setText('soc', j.soc.toFixed(1));
    setText('cycles', j.cycles.toFixed(2));
    setText('currentA', j.currentA.toFixed(3));

    const fault = document.getElementById('fault');
    if(fault) fault.textContent = j.fault || '';

    setDot('dotPre', j.cPre);
    setDot('dotChg', j.cChg);
    setDot('dotDsg', j.cDsg);

    renderCells(j.cells || []);
  }catch(e){}
}

setInterval(tick, 1000);
tick();
</script>
)JS";

  html += "</body></html>";
  server.send(200, "text/html", html);
}

static void handleSetLang() {
  requireAnyUser();
  if (!isUserOrAdmin()) return;

  String l = server.arg("l");
  if (l == "en") lang = LANG_EN;
  else if (l == "de") lang = LANG_DE;
  else lang = LANG_ES;
  saveLangOnly();
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "OK");
}

static void handleApplyPreset() {
  if (!isAdmin()) { requireAdmin(); return; }

  String c = server.arg("chem");
  if (c == "LFP") { applyPreset(CHEM_LFP); chem = CHEM_LFP; }
  else if (c == "NMC") { applyPreset(CHEM_NMC); chem = CHEM_NMC; }
  else if (c == "LTO") { applyPreset(CHEM_LTO); chem = CHEM_LTO; }
  saveConfig();
  server.sendHeader("Location", "/config");
  server.send(302, "text/plain", "OK");
}

static void handleSave() {
  if (!isAdmin()) { requireAdmin(); return; }

  if (server.hasArg("cellOv")) limits.cellOv = server.arg("cellOv").toFloat();
  if (server.hasArg("cellUv")) limits.cellUv = server.arg("cellUv").toFloat();
  if (server.hasArg("dMax")) limits.deltaMax = server.arg("dMax").toFloat();
  if (server.hasArg("tMin")) limits.tempMin = server.arg("tMin").toFloat();
  if (server.hasArg("tMax")) limits.tempMax = server.arg("tMax").toFloat();
  if (server.hasArg("pOv")) limits.packOv = server.arg("pOv").toFloat();
  if (server.hasArg("pUv")) limits.packUv = server.arg("pUv").toFloat();
  if (server.hasArg("dRec")) limits.deltaRecov = server.arg("dRec").toFloat();
  if (server.hasArg("cOvR")) limits.cellOvRecov = server.arg("cOvR").toFloat();
  if (server.hasArg("cUvR")) limits.cellUvRecov = server.arg("cUvR").toFloat();
  if (server.hasArg("pOvR")) limits.packOvRecov = server.arg("pOvR").toFloat();
  if (server.hasArg("pUvR")) limits.packUvRecov = server.arg("pUvR").toFloat();
  if (server.hasArg("cto")) limits.commTimeoutMs = (uint32_t)server.arg("cto").toInt();
  if (server.hasArg("pcms")) limits.prechargeMs = (uint32_t)server.arg("pcms").toInt();
  if (server.hasArg("expCells")) { int v = server.arg("expCells").toInt(); limits.expectedCells = (uint16_t)(v > 0 ? v : 0); }
  if (server.hasArg("tpChg")) limits.taperChgStartV = server.arg("tpChg").toFloat();
  if (server.hasArg("tpChgE")) limits.taperChgEndV = server.arg("tpChgE").toFloat();
  if (server.hasArg("tpDis")) limits.taperDisStartV = server.arg("tpDis").toFloat();
  if (server.hasArg("tpDisE")) limits.taperDisEndV = server.arg("tpDisE").toFloat();
  if (server.hasArg("tpRec")) limits.taperRecoveryPctS = constrain(server.arg("tpRec").toFloat(), 0.1f, 100.0f);
  if (server.hasArg("tpHold")) limits.taperHoldMs = (uint16_t)constrain(server.arg("tpHold").toInt(), 0, 60000);
  if (server.hasArg("dynCvl")) limits.dynCvlOffsetV = constrain(server.arg("dynCvl").toFloat(), 0.0f, 10.0f);
  if (server.hasArg("balA")) balanceCurrentA = constrain(server.arg("balA").toFloat(), 0.1f, 5.0f);

  // Night mode settings
  nightModeEnabled = server.hasArg("nightEn");
  if (server.hasArg("nightS")) nightStartHour = (uint8_t)constrain(server.arg("nightS").toInt(), 0, 23);
  if (server.hasArg("nightE")) nightEndHour = (uint8_t)constrain(server.arg("nightE").toInt(), 0, 23);
  if (server.hasArg("nightCcl")) nightCclPct = (uint8_t)constrain(server.arg("nightCcl").toInt(), 0, 100);
  if (server.hasArg("nightDcl")) nightDclPct = (uint8_t)constrain(server.arg("nightDcl").toInt(), 0, 100);

  if (server.hasArg("capAh")) ecfg.capacityAh = max(1.0f, server.arg("capAh").toFloat());
  if (server.hasArg("sohAh")) ecfg.sohAh = max(1.0f, server.arg("sohAh").toFloat());
  if (server.hasArg("initSoc")) ecfg.initSocPct = constrain(server.arg("initSoc").toFloat(), 0.0f, 100.0f);
  if (server.hasArg("cPol")) {
    int p = server.arg("cPol").toInt();
    ecfg.currentPolarity = (p >= 0) ? 1 : -1;
  }
  if (server.hasArg("eff")) ecfg.coulombEff = constrain(server.arg("eff").toFloat(), 0.90f, 1.00f);

  if (server.hasArg("fV")) ecfg.fullCellV = server.arg("fV").toFloat();
  if (server.hasArg("eV")) ecfg.emptyCellV = server.arg("eV").toFloat();
  if (server.hasArg("tail")) ecfg.tailCurrentA = constrain(server.arg("tail").toFloat(), 0.1f, 500.0f);
  if (server.hasArg("hold")) ecfg.holdMs = max((uint32_t)5000, (uint32_t)server.arg("hold").toInt());

  // Inverter settings
  if (server.hasArg("invP")) invProto = (InverterProto)server.arg("invP").toInt();
  if (server.hasArg("invChgA")) invUserMaxChgA = (uint16_t)constrain(server.arg("invChgA").toInt(), 0, 500);
  if (server.hasArg("invDisA")) invUserMaxDisA = (uint16_t)constrain(server.arg("invDisA").toInt(), 0, 500);
  if (server.hasArg("invChgV")) invUserMaxChgV_dV = (uint16_t)server.arg("invChgV").toInt();
  if (server.hasArg("invDisV")) invUserMinDisV_dV = (uint16_t)server.arg("invDisV").toInt();

  // MQTT settings
  bool mqttChanged = false;
  if (server.hasArg("mqttEp")) {
    String newEp = server.arg("mqttEp");
    if (newEp != mqttEndpoint) { mqttEndpoint = newEp; mqttChanged = true; }
  }
  if (server.hasArg("mqttPt")) {
    uint16_t newPt = (uint16_t)constrain(server.arg("mqttPt").toInt(), 1, 65535);
    if (newPt != mqttPort) { mqttPort = newPt; mqttChanged = true; }
  }
  {
    bool newIns = server.hasArg("mqttIns");
    if (newIns != mqttInsecure) { mqttInsecure = newIns; mqttChanged = true; }
  }
  if (server.hasArg("mqttUsr")) {
    String newUsr = server.arg("mqttUsr");
    if (newUsr != mqttUser) { mqttUser = newUsr; mqttChanged = true; }
  }
  if (server.hasArg("mqttPwd")) {
    String pw = server.arg("mqttPwd");
    if (pw != "****" && pw != mqttPass) { mqttPass = pw; mqttChanged = true; }
  }

  saveConfig();

  // Normalize SoC to new capacity bounds (use learned capacity)
  float cap = max(1.0f, ecfg.sohAh);
  coulombBalance_Ah = constrain(coulombBalance_Ah, 0.0f, cap);
  socPct = constrain((coulombBalance_Ah / cap) * 100.0f, 0.0f, 100.0f);
  saveEnergyState();

  // Apply MQTT settings if changed (will reconnect on next mqttLoop cycle)
  if (mqttChanged) {
    mqttClient.disconnect();
    mqttEnabled = (mqttEndpoint.length() > 0);
    if (mqttEnabled) {
      if (mqttInsecure) {
        mqttClient.setClient(mqttPlainClient);
      } else {
        mqttClient.setClient(mqttTlsClient);
      }
      mqttClient.setServer(mqttEndpoint.c_str(), mqttPort);
      Serial.printf("🔄 MQTT reconfigurado: %s:%d (TLS=%s)\n", mqttEndpoint.c_str(), mqttPort, mqttInsecure ? "no" : "si");
    } else {
      Serial.println("ℹ️ MQTT deshabilitado");
    }
  }

  server.sendHeader("Location", "/config");
  server.send(302, "text/plain", "OK");
}

static void handleActions() {
  requireAnyUser();
  if (!isUserOrAdmin()) return;

  if (server.hasArg("arm")) {
    bool wantArm = (server.arg("arm").toInt() == 1);
    if (wantArm && limits.expectedCells > 0 && packActiveCells != limits.expectedCells) {
      logEvent(EVT_FAULT, "ARM rejected: cells %d/%d", packActiveCells, limits.expectedCells);
      server.send(200, "text/html", "<html><body><p>ARM rejected: cell count mismatch ("
        + String(packActiveCells) + "/" + String(limits.expectedCells)
        + ")</p><a href='/'>Back</a></body></html>");
      return;
    }
    armed = wantArm;
    logEvent(armed ? EVT_ARM : EVT_DISARM, armed ? "System ARMED" : "System DISARMED");
    state = ST_OPEN;
    // Cancel any active soft disconnect when arming/disarming
    if (softDisconnectActive) {
      softDisconnectActive = false;
      logEvent(EVT_STATE, "SOFT_DISCONNECT: cancelled by arm/disarm");
    }
    if (!armed) setContactors(false,false,false,false);
  }
  if (server.hasArg("reset")) {
    if (state == ST_TRIPPED) {
      logEvent(EVT_RESET, "TRIP reset -> OPEN");
      state = ST_OPEN;
    }
  }
  if (server.hasArg("resetEnergy")) {
    if (!isAdmin()) { requireAdmin(); return; }
    logEvent(EVT_RESET, "Energy state reset");
    resetEnergyState();
  }
  if (server.hasArg("resetUserCounters")) {
    user_chargeAh = 0.0f;
    user_dischargeAh = 0.0f;
    user_chargeWh = 0.0f;
    user_dischargeWh = 0.0f;
    logEvent(EVT_RESET, "User counters reset");
  }
  if (server.hasArg("clearEventLog")) {
    if (!isAdmin()) { requireAdmin(); return; }
    clearEventLog();
  }

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "OK");
}

// Forward declarations for inverter protocol functions
static void inverter_rx(uint32_t rxId, const uint8_t* rxData, uint8_t rxLen);
static void inverter_tx_tick(uint32_t nowMs);

// ===================== CAN task =====================
static void canTask(void*) {
  uint32_t lastPoll = 0;
  uint32_t lastEnergySave = 0;
  uint32_t lastLog = 0;

  for (;;) {
    uint32_t now = millis();

    // poll JK IDs 1..10
    if (now - lastPoll >= 400) {
      lastPoll = now;
      for (uint8_t id=1; id<=MAX_MODULES; id++) requestModule(id);
    }

    // receive window
    uint32_t end = now + 40;
    while (millis() < end) {
      twai_message_t rx = {};
      if (twai_receive(&rx, pdMS_TO_TICKS(5)) != ESP_OK) continue;
      if (rx.extd) continue;

      uint16_t sid = (uint16_t)rx.identifier;

      // current sensor
      if (sid == AHBC_ID1 || sid == AHBC_ID2) {
        int32_t mA = 0;
        if (decodeAhbcCurrent(rx, mA)) {
          integrateCurrent(mA, millis());
        }
        continue;
      }

      // JK modules: std ID equals module ID
      uint8_t id = (uint8_t)sid;
      if (id < 1 || id > MAX_MODULES) continue;

      modules[id].lastSeenMs = millis();
      uint8_t fid = rx.data[0];

      if (fid == 0x01 && rx.data_length_code >= 8) {
        modules[id].active = true;

        // NOTE: you previously fixed temp scaling by x10 (you observed 1.3 vs 13).
        // Here we assume JK sends temperature in 0.1°C units:
        // JK temperature scaling: on your packs, raw value is in 1.0°C units (e.g., 14 -> 14°C)
        modules[id].tempC = (float)((rx.data[1] << 8) | rx.data[2]);

        modules[id].packV  = ((rx.data[3] << 8) | rx.data[4]) / 100.0f;
        modules[id].cellCount = rx.data[7];
      }

      if (fid == 0x04 && rx.data_length_code == 8) {
        uint8_t start_cell = rx.data[1];
        for (uint8_t j=0; j<3; j++) {
          uint16_t mv = (uint16_t(rx.data[2 + j*2]) << 8) | rx.data[3 + j*2];
          float v = mv / 1000.0f;
          uint8_t cell_num = start_cell + j + 1;
          if (cell_num>=1 && cell_num<=MAX_CELLS_PER_MODULE) {
            modules[id].cells[cell_num-1] = v;
          }
        }
      }
    }

    // Read MCP2515 (inverter bus) and dispatch to inverter_rx
#ifdef MCP2515_CS
    if (mcp2515_ok) {
      struct can_frame rxFrame;
      while (mcp2515.checkReceive()) {
        if (mcp2515.readMessage(&rxFrame) == MCP2515::ERROR_OK) {
          mcp2515_rxCount++;
          mcp2515_lastRxId = rxFrame.can_id;
          mcp2515_lastRxDlc = rxFrame.can_dlc;
          memcpy(mcp2515_lastRxData, rxFrame.data, min((uint8_t)8, rxFrame.can_dlc));
          mcp2515_trackId(rxFrame.can_id);
          inverter_rx(rxFrame.can_id, rxFrame.data, rxFrame.can_dlc);
        } else {
          break;
        }
      }
      inverter_tx_tick(now);
    }
#endif

    controlTick();

    if (now - lastEnergySave > 5000) {
      lastEnergySave = now;
      saveEnergyState();
    }

    // Record history every 5 seconds
    if (now - lastHistorySample >= 5000) {
      lastHistorySample = now;
      recordHistory();
    }

    // serial status every 2s
    if (now - lastLog > 2000) {
      lastLog = now;
      Serial.printf("📊 IDs:%s | V=%.2fV | min=%.3f max=%.3f d=%.3f | Tavg=%.1fC | SoC=%.1f%% | I=%.2fA | SoH=%.1fAh | cyc=%.2f | learn=%s dAh=%.2f\n",
        activeIdsCsv.c_str(),
        packTotalV, packMinCellV, packMaxCellV, packDeltaV,
        packAvgTempC,
        socPct,
        currentValid ? ((float)current_mA/1000.0f) : 0.0f,
        ecfg.sohAh,
        cycleCountEq,
        (learnPhase==LEARN_FROM_FULL ? "RUN" : "IDLE"),
        learnDischargeAh
      );
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}


// ===================== Inverter protocol TX/RX (subset) =====================
static inline void canSendStd(uint16_t id, const uint8_t* d, uint8_t len){
  twai_message_t m = {};
  m.extd = 0;
  m.rtr = 0;
  m.identifier = id;
  m.data_length_code = len;
  for(int i=0;i<len;i++) m.data[i]=d[i];
  twai_transmit(&m, pdMS_TO_TICKS(10));
}

#ifdef MCP2515_CS
static inline void canSendMcp(uint32_t id, const uint8_t* d, uint8_t len){
  if(!mcp2515_ok) return;
  struct can_frame frame;
  frame.can_id = id;
  frame.can_dlc = len;
  for(int i=0;i<len;i++) frame.data[i]=d[i];
  if(mcp2515.sendMessage(&frame) == MCP2515::ERROR_OK){
    mcp2515_txCount++;
  } else {
    mcp2515_errCount++;
  }
}
#else
static inline void canSendMcp(uint32_t id, const uint8_t* d, uint8_t len){
  (void)id; (void)d; (void)len; // MCP2515 not available
}
#endif

static void byd_send_initial(){
  // BYD_250/290/2D0/3D0 (see Battery-Emulator project)
  uint8_t d250[8] = {0x03,0x29,0x00,0x66,0,0,0x02,0x09};
  // capacity in kWh*10? BE uses reported_total_capacity_Wh/100. We'll approximate from capacityAh*packVnom
  uint16_t capWh = (uint16_t)min(65000.0f, ecfg.sohAh * 3.2f * 24.0f); // rough; can be improved
  uint16_t cap = (uint16_t)(capWh/100);
  d250[4] = (cap>>8)&0xFF; d250[5]=cap&0xFF;
  canSendMcp(0x250,d250,8);
  uint8_t d290[8] = {0x06,0x37,0x10,0xD9,0,0,0,0};
  canSendMcp(0x290,d290,8);
  uint8_t d2d0[8] = {0x00,0x42,0x59,0x44,0,0,0,0};
  canSendMcp(0x2D0,d2d0,8);
  // multi frames
  uint8_t a0[8]={0x00,0x42,0x61,0x74,0x74,0x65,0x72,0x79};
  uint8_t a1[8]={0x01,0x2D,0x42,0x6F,0x78,0x20,0x50,0x72};
  uint8_t a2[8]={0x02,0x65,0x6D,0x69,0x75,0x6D,0x20,0x48};
  uint8_t a3[8]={0x03,0x56,0x53,0x00,0x00,0x00,0x00,0x00};
  canSendMcp(0x3D0,a0,8); canSendMcp(0x3D0,a1,8); canSendMcp(0x3D0,a2,8); canSendMcp(0x3D0,a3,8);
}

#ifndef CAN_EFF_FLAG
#define CAN_EFF_FLAG 0x80000000UL
#endif

// Send Pylon frame: maps extended IDs (0x4210→0x421) when inverter uses standard IDs
static inline void pylonSend(uint32_t id, const uint8_t* d, uint8_t len) {
  if(pylonUseStdIds){
    // Map: 0x4210→0x421, 0x4220→0x422, ..., 0x7310→0x731, 0x7320→0x732
    canSendMcp(id >> 4, d, len);
  } else {
    canSendMcp(id | CAN_EFF_FLAG, d, len);
  }
}

static void inverter_rx(uint32_t rxId, const uint8_t* rxData, uint8_t rxLen){
  if(invProto==INV_OFF) return;
  // Any message from inverter marks it alive for BYD
  if(invProto==INV_BYD_HVS){
    if(rxId==0x151){
      invStarted=true;
      if(rxLen>=1 && (rxData[0] & 0x01)){
        byd_send_initial();
        invInitialSent=true;
      }
    } else if(rxId==0x091 || rxId==0x0D1 || rxId==0x111){
      invStarted=true;
    }
  }
  if(invProto==INV_PYLON_HV){
    uint32_t bareId = rxId & 0x1FFFFFFFUL;  // strip flags
    // Accept both extended 0x4200 and standard 0x420 (Ingeteam uses standard 11-bit IDs)
    if(bareId == 0x4200){
      invStarted=true;
      pylonUseStdIds = false;
    }
    if(bareId == 0x420){
      invStarted=true;
      pylonUseStdIds = true;
    }
    // 0x620 heartbeat from Ingeteam — mark inverter as alive
    if(bareId == 0x620){
      invStarted=true;
    }
  }
}

static void inverter_tx_tick(uint32_t nowMs){
  if(invProto==INV_OFF) return;

  // compute shared values
  float packV = packTotalV;
  float packI = currentValid ? ((float)current_mA/1000.0f) : 0.0f;
  float tavg = packAvgTempC;
  uint16_t v_dV = (uint16_t)max(0.0f, packV*10.0f);
  int16_t  i_dA = (int16_t)(packI*10.0f);
  int16_t  t_dC = (int16_t)(tavg*10.0f);
  // Cap SoC at 99% for inverter until FULL is confirmed by learning algorithm
  float invSocPct = (!fullConfirmed && socPct > 99.0f) ? 99.0f : socPct;
  uint16_t soc_pptt = (uint16_t)constrain((int)(invSocPct*100.0f), 0, 10000);
  float sohPct = (ecfg.capacityAh>1e-3f) ? (ecfg.sohAh/ecfg.capacityAh*100.0f) : 100.0f;
  uint16_t soh_pptt = (uint16_t)constrain((int)(sohPct*100.0f), 0, 10000);

  // Directional limits from your protection logic (includes taper + night mode):
  uint16_t maxChg_dA = (uint16_t)constrain((int)(invUserMaxChgA * taperChgFactor * nightModeFactor * 10), 0, 20000);
  uint16_t maxDis_dA = (uint16_t)constrain((int)(invUserMaxDisA * taperDisFactor * nightModeDclFactor * 10), 0, 20000);
  if(!allowCharge) maxChg_dA = 0;
  if(!allowDischarge) maxDis_dA = 0;

  // Force CCL/DCL to 0 during soft disconnect to let inverter reduce current before opening contactors
  if(softDisconnectActive) {
    maxChg_dA = 0;
    maxDis_dA = 0;
  }

  // Dynamic CVL: reduce charge voltage when tapering to help inverter do CC→CV
  uint16_t dynChgV_dV = 0xFFFF;
  if (limits.dynCvlOffsetV > 0.001f && taperChgFactor < 0.999f && packTotalV > 1.0f) {
    float dynV = packTotalV + limits.dynCvlOffsetV;
    dynChgV_dV = (uint16_t)constrain(dynV * 10.0f, 0.0f, 65535.0f);
  }

  // Update debug display (shared across all protocols)
  pylonDbg_CCL = maxChg_dA;
  pylonDbg_DCL = maxDis_dA;
  pylonDbg_temp = t_dC;

  if(invProto==INV_BYD_HVS){
    if(!invStarted) return;

    // Send initial once if inverter has started and we haven't sent yet
    if(!invInitialSent){
      byd_send_initial();
      invInitialSent=true;
    }

    // 2s: 0x110 limits
    if(nowMs - invLast2s >= 2000){
      invLast2s = nowMs;
      // BYD HVS: fixed CVL/DVL — tapering controls current via CCL, no need for dynamic CVL
      uint16_t chgV = invUserMaxChgV_dV ? invUserMaxChgV_dV : (uint16_t)(packActiveCells * limits.cellOv * 10.0f);
      uint16_t disV = invUserMinDisV_dV ? invUserMinDisV_dV : (uint16_t)(packActiveCells * limits.cellUv * 10.0f);
      pylonDbg_CVL = chgV; pylonDbg_DVL = disV;
      uint8_t d110[8] = {(uint8_t)(chgV>>8),(uint8_t)chgV,(uint8_t)(disV>>8),(uint8_t)disV,
                         (uint8_t)(maxDis_dA>>8),(uint8_t)maxDis_dA,(uint8_t)(maxChg_dA>>8),(uint8_t)maxChg_dA};
      canSendMcp(0x110,d110,8);
    }
    // 10s: 0x150 states + 0x1D0 info + 0x210 temp min/max (we only have avg, so mirror)
    if(nowMs - invLast10s >= 10000){
      invLast10s = nowMs;
      uint16_t remAh = (uint16_t)max(0.0f, ecfg.sohAh * (socPct/100.0f));
      uint16_t fullAh = (uint16_t)max(0.0f, ecfg.sohAh);
      uint8_t d150[8] = {(uint8_t)(soc_pptt>>8),(uint8_t)soc_pptt,(uint8_t)(soh_pptt>>8),(uint8_t)soh_pptt,
                         (uint8_t)(remAh>>8),(uint8_t)remAh,(uint8_t)(fullAh>>8),(uint8_t)fullAh};
      canSendMcp(0x150,d150,8);
      uint8_t d1d0[8] = {(uint8_t)(v_dV>>8),(uint8_t)v_dV,(uint8_t)(i_dA>>8),(uint8_t)i_dA,
                         (uint8_t)(t_dC>>8),(uint8_t)t_dC,0,0};
      canSendMcp(0x1D0,d1d0,8);
      uint8_t d210[8] = {(uint8_t)(t_dC>>8),(uint8_t)t_dC,(uint8_t)(t_dC>>8),(uint8_t)t_dC,0,0,0,0};
      canSendMcp(0x210,d210,8);
    }
    // 60s alarm (zeros)
    if(nowMs - invLast60s >= 60000){
      invLast60s = nowMs;
      uint8_t d190[8] = {0,0,3,0,0,0,0,0};
      canSendMcp(0x190,d190,8);
    }
  }

  // ── Pylontech Force H2 (HV) — big-endian, standard or extended IDs ──
  // Sends ALL frames (setup + system data) on every inverter poll.
  // Ingeteam sends 0x420 with byte0=0x00 as a general poll — never req=0x02.
  if(invProto==INV_PYLON_HV){
    if(!invStarted) return;
    // Send periodically every 2s (don't wait for request — some inverters just poll)
    if(nowMs - invLast2s < 2000) return;
    invLast2s = nowMs;

    // ── Setup frames (0x7310 + 0x7320) — sent every cycle ──
    uint8_t d7310[8] = {0x01,0x00,0x02,0x01,0x01,0x02,0x00,0x00};
    pylonSend(0x7310, d7310, 8);

    uint8_t nMod = 0;
    for(int i=1; i<=MAX_MODULES; i++) if(modules[i].active) nMod++;
    uint16_t totalCells = packActiveCells;
    uint8_t cellsPerMod = nMod > 0 ? (uint8_t)(totalCells / nMod) : (uint8_t)totalCells;
    float nomPerCell = (limits.cellOv + limits.cellUv) / 2.0f;
    uint16_t nomV = (uint16_t)(packActiveCells * nomPerCell);
    uint16_t capAh = (uint16_t)ecfg.sohAh;
    uint8_t d7320[8] = {
      (uint8_t)(totalCells & 0xFF), (uint8_t)(totalCells >> 8),
      nMod, cellsPerMod,
      (uint8_t)(nomV & 0xFF), (uint8_t)(nomV >> 8),
      (uint8_t)(capAh & 0xFF), (uint8_t)(capAh >> 8)
    };
    pylonSend(0x7320, d7320, 8);

    // ── System data frames (0x4210–0x4290) ──
    uint16_t chgV_dV = invUserMaxChgV_dV ? invUserMaxChgV_dV : (uint16_t)(packActiveCells * limits.cellOv * 10.0f);
    if (dynChgV_dV < chgV_dV) chgV_dV = dynChgV_dV;
    uint16_t disV_dV = invUserMinDisV_dV ? invUserMinDisV_dV : (uint16_t)(packActiveCells * limits.cellUv * 10.0f);
    int16_t temp_dC = (int16_t)(packAvgTempC * 10.0f);
    uint16_t temp4210 = (uint16_t)(temp_dC + 1000);  // Pylon HV spec: +1000 offset in 0x4210
    uint8_t socInt = (uint8_t)constrain((int)invSocPct, 0, 100);
    uint8_t sohInt = (uint8_t)constrain((int)sohPct, 0, 100);

    // Store CVL/DVL debug (CCL/DCL/temp set in common code above)
    pylonDbg_CVL = chgV_dV; pylonDbg_DVL = disV_dV;

    // 0x4210 — Pack voltage, current, temperature (+1000 offset), SOC, SOH (big-endian)
    uint8_t d4210[8] = {
      (uint8_t)(v_dV >> 8), (uint8_t)(v_dV & 0xFF),
      (uint8_t)(i_dA >> 8), (uint8_t)(i_dA & 0xFF),
      (uint8_t)(temp4210 >> 8), (uint8_t)(temp4210 & 0xFF),
      socInt, sohInt
    };
    pylonSend(0x4210, d4210, 8);

    // 0x4220 — Limits: CVL, DVL, CCL, DCL (big-endian, 0.1V / 0.1A)
    uint8_t d4220[8] = {
      (uint8_t)(chgV_dV >> 8), (uint8_t)(chgV_dV & 0xFF),
      (uint8_t)(disV_dV >> 8), (uint8_t)(disV_dV & 0xFF),
      (uint8_t)(maxChg_dA >> 8), (uint8_t)(maxChg_dA & 0xFF),
      (uint8_t)(maxDis_dA >> 8), (uint8_t)(maxDis_dA & 0xFF)
    };
    pylonSend(0x4220, d4220, 8);

    // 0x4230 — Cell voltage extremes (mV, big-endian)
    uint16_t maxCellmV = (uint16_t)(packMaxCellV * 1000.0f);
    uint16_t minCellmV = (uint16_t)(packMinCellV * 1000.0f);
    uint8_t d4230[8] = {
      (uint8_t)(maxCellmV >> 8), (uint8_t)(maxCellmV & 0xFF),
      (uint8_t)(minCellmV >> 8), (uint8_t)(minCellmV & 0xFF),
      packMaxCellModule, packMaxCellIndex,
      packMinCellModule, packMinCellIndex
    };
    pylonSend(0x4230, d4230, 8);

    // 0x4240 — Temperature extremes (0.1°C signed, big-endian, no offset)
    uint8_t d4240[8] = {
      (uint8_t)((uint16_t)temp_dC >> 8), (uint8_t)((uint16_t)temp_dC & 0xFF),
      (uint8_t)((uint16_t)temp_dC >> 8), (uint8_t)((uint16_t)temp_dC & 0xFF),
      1, 1, 1, 1
    };
    pylonSend(0x4240, d4240, 8);

    // 0x4250 — System status: 0=Sleep, 1=Charge, 2=Discharge, 3=Idle
    uint8_t status = 3;
    if(packI > 0.5f) status = 1;
    else if(packI < -0.5f) status = 2;
    uint8_t d4250[8] = {status, 0, 0, 0, 0, 0, 0, 0};
    pylonSend(0x4250, d4250, 8);

    // 0x4260 — Energy info (placeholder)
    uint8_t d4260[8] = {0};
    pylonSend(0x4260, d4260, 8);

    // 0x4270 — Module temperatures (same as 0x4240)
    pylonSend(0x4270, d4240, 8);

    // 0x4280 — Permission flags: 0x00=allowed, 0xAA=forbidden
    uint8_t chgPerm = (allowCharge && !softDisconnectActive) ? 0x00 : 0xAA;
    uint8_t disPerm = (allowDischarge && !softDisconnectActive) ? 0x00 : 0xAA;
    uint8_t d4280[8] = {chgPerm, disPerm, 0, 0, 0, 0, 0, 0};
    pylonSend(0x4280, d4280, 8);

    // 0x4290 — Alarms (all zeros = no alarm)
    uint8_t d4290[8] = {0};
    pylonSend(0x4290, d4290, 8);
    return;
  }

  // ── Standard 0x35x protocol — Pylontech US2000 / SMA / Victron / Batrium ──
  // Used by INV_PYLON_LV (manufacturer "PYLON") and INV_SMA_CAN (manufacturer per brand)
  // Sends all frames every 1 second: 0x351 (limits), 0x355 (SOC), 0x356 (V/I/T), 0x359 (alarms), 0x35C (flags), 0x35E (name)
  if(invProto==INV_PYLON_LV || invProto==INV_SMA_CAN){
    if(nowMs - invLast2s < 1000) return;
    invLast2s = nowMs;

    uint16_t chgV_dV = invUserMaxChgV_dV ? invUserMaxChgV_dV : (uint16_t)(packActiveCells * limits.cellOv * 10.0f);
    if (dynChgV_dV < chgV_dV) chgV_dV = dynChgV_dV;
    uint16_t disV_dV = invUserMinDisV_dV ? invUserMinDisV_dV : (uint16_t)(packActiveCells * limits.cellUv * 10.0f);
    int16_t chgA_dA = allowCharge ? (int16_t)(invUserMaxChgA * taperChgFactor * nightModeFactor * 10) : 0;
    int16_t disA_dA = allowDischarge ? (int16_t)(invUserMaxDisA * taperDisFactor * nightModeDclFactor * 10) : 0;
    if(softDisconnectActive) { chgA_dA = 0; disA_dA = 0; }
    pylonDbg_CVL = chgV_dV; pylonDbg_DVL = disV_dV;

    // 0x351: CVL, CCL, DCL, DVL (little-endian, 0.1V / 0.1A signed)
    uint8_t d351[8] = {
      (uint8_t)(chgV_dV & 0xFF), (uint8_t)(chgV_dV >> 8),
      (uint8_t)(chgA_dA & 0xFF), (uint8_t)(chgA_dA >> 8),
      (uint8_t)(disA_dA & 0xFF), (uint8_t)(disA_dA >> 8),
      (uint8_t)(disV_dV & 0xFF), (uint8_t)(disV_dV >> 8)
    };
    canSendMcp(0x351, d351, 8);

    // 0x355: SOC, SOH (little-endian, 1%)
    uint16_t soc_pct = (uint16_t)constrain((int)invSocPct, 0, 100);
    uint16_t soh_pct = (uint16_t)constrain((int)sohPct, 0, 100);
    uint8_t d355[4] = {
      (uint8_t)(soc_pct & 0xFF), (uint8_t)(soc_pct >> 8),
      (uint8_t)(soh_pct & 0xFF), (uint8_t)(soh_pct >> 8)
    };
    canSendMcp(0x355, d355, 4);

    // 0x356: Voltage (0.01V), Current (0.1A), Temperature (0.1°C) — all signed LE
    int16_t v_cV = (int16_t)(packV * 100.0f);
    int16_t t_dC_35x = (int16_t)(tavg * 10.0f);
    uint8_t d356[6] = {
      (uint8_t)(v_cV & 0xFF), (uint8_t)(v_cV >> 8),
      (uint8_t)(i_dA & 0xFF), (uint8_t)(i_dA >> 8),
      (uint8_t)(t_dC_35x & 0xFF), (uint8_t)(t_dC_35x >> 8)
    };
    canSendMcp(0x356, d356, 6);

    // 0x35C: Flags — bit7=charge enable, bit6=discharge enable
    uint8_t flags = 0;
    if(allowCharge && !softDisconnectActive) flags |= 0x80;
    if(allowDischarge && !softDisconnectActive) flags |= 0x40;
    uint8_t d35C[2] = { flags, 0x00 };
    canSendMcp(0x35C, d35C, 2);

    // 0x35E: Manufacturer name (8 bytes ASCII)
    if(invProto==INV_PYLON_LV){
      uint8_t d35E[8] = {'P','Y','L','O','N',' ',' ',' '};
      canSendMcp(0x35E, d35E, 8);
    } else {
      uint8_t d35E[8] = {'J','K','B','M','S',' ',' ',' '};
      canSendMcp(0x35E, d35E, 8);
    }

    // 0x359: Alarms and warnings (7 bytes)
    uint8_t alarms1 = 0, alarms2 = 0, warnings1 = 0, warnings2 = 0;
    if(criticalFault) alarms1 |= 0x01;
    if(faultOv || faultPackOv) alarms1 |= 0x02;
    if(faultUv || faultPackUv) alarms1 |= 0x04;
    if(faultTemp && packAvgTempC > limits.tempMax) alarms1 |= 0x08;
    if(faultTemp && packAvgTempC < limits.tempMin) alarms1 |= 0x10;
    if(directionalFault) warnings1 |= 0x01;
    if(faultDelta) warnings1 |= 0x02;
    uint8_t d359[7] = { alarms1, alarms2, warnings1, warnings2, 0x00, 0x00, 0x00 };
    canSendMcp(0x359, d359, 7);
  }
}


static void handleApiRev() {
  requireAnyUser();
  if (!isUserOrAdmin()) return;
  String j = String("{\"rev\":") + String(ui_rev) + "}";
  server.send(200, "application/json", j);
}


static void handleApiStatus() {
  requireAnyUser();
  if (!isUserOrAdmin()) return;

  String j = "{";
  j += "\"rev\":" + String(ui_rev) + ",";
  j += "\"armed\":" + String(armed ? 1 : 0) + ",";
  j += "\"state\":\"" + String(state==ST_OPEN?"OPEN":state==ST_PRECHARGING?"PRECHARGING":state==ST_CLOSED?"CLOSED":"TRIPPED") + "\",";
  j += "\"ids\":\"" + activeIdsCsv + "\",";

  j += "\"packV\":" + String(packTotalV, 2) + ",";
  j += "\"minCell\":" + String(packMinCellV, 3) + ",";
  j += "\"maxCell\":" + String(packMaxCellV, 3) + ",";
  j += "\"delta\":" + String(packDeltaV, 3) + ",";
  j += "\"tavg\":" + String(packAvgTempC, 1) + ",";
  j += "\"cellsActive\":" + String(packActiveCells) + ",";
  j += "\"expectedCells\":" + String(limits.expectedCells) + ",";

  j += "\"soc\":" + String(socPct, 1) + ",";
  j += "\"sohAh\":" + String(ecfg.sohAh, 1) + ",";
  j += "\"cycles\":" + String(cycleCountEq, 2) + ",";
  j += "\"currentA\":" + String(currentValid ? ((float)current_mA/1000.0f) : 0.0f, 3) + ",";
  j += "\"powerW\":" + String(power_W, 1) + ",";
  j += "\"avgCell\":" + String(packAvgCellV, 3) + ",";
  j += "\"minCellMod\":" + String(packMinCellModule) + ",";
  j += "\"minCellIdx\":" + String(packMinCellIndex) + ",";
  j += "\"maxCellMod\":" + String(packMaxCellModule) + ",";
  j += "\"maxCellIdx\":" + String(packMaxCellIndex) + ",";
  j += "\"uChgAh\":" + String(user_chargeAh, 2) + ",";
  j += "\"uDsgAh\":" + String(user_dischargeAh, 2) + ",";
  j += "\"uChgKwh\":" + String(user_chargeWh / 1000.0f, 3) + ",";
  j += "\"uDsgKwh\":" + String(user_dischargeWh / 1000.0f, 3) + ",";

  j += "\"allowChg\":" + String(allowCharge ? 1 : 0) + ",";
  j += "\"allowDsg\":" + String(allowDischarge ? 1 : 0) + ",";
  j += "\"taperChg\":" + String(taperChgFactor, 2) + ",";
  j += "\"taperDis\":" + String(taperDisFactor, 2) + ",";
  { float cvl = 0.0f;
    if (limits.dynCvlOffsetV > 0.001f && taperChgFactor < 0.999f && packTotalV > 1.0f)
      cvl = packTotalV + limits.dynCvlOffsetV;
    j += "\"cvlV\":" + String(cvl, 1) + ",";
  }
  j += "\"nightMode\":" + String(isNightTime() ? 1 : 0) + ",";
  j += "\"nightCcl\":" + String(nightModeFactor, 2) + ",";
  j += "\"nightDcl\":" + String(nightModeDclFactor, 2) + ",";
  j += "\"balTimeH\":" + String(estimateBalanceTimeH(), 2) + ",";
  j += "\"critical\":" + String(criticalFault ? 1 : 0) + ",";
  j += "\"directional\":" + String(directionalFault ? 1 : 0) + ",";
  j += "\"fault\":\"" + faultText + "\",";

  j += "\"cMain\":" + String(cMain ? 1 : 0) + ",";
  j += "\"cPre\":"  + String(cPre  ? 1 : 0) + ",";
  j += "\"cChg\":"  + String(cChg  ? 1 : 0) + ",";
  j += "\"cDsg\":"  + String(cDsg  ? 1 : 0) + ",";

  j += "\"mcp2515\":" + String(mcp2515_ok ? 1 : 0) + ",";
  j += "\"mcp2515tx\":" + String(mcp2515_txCount) + ",";
  j += "\"mcp2515rx\":" + String(mcp2515_rxCount) + ",";
  j += "\"mcp2515err\":" + String(mcp2515_errCount) + ",";
  char hexBuf[12]; snprintf(hexBuf, sizeof(hexBuf), "0x%08lX", (unsigned long)mcp2515_lastRxId);
  j += "\"mcpLastId\":\"" + String(hexBuf) + "\",";
  {char db[32]; snprintf(db,sizeof(db),"%02X %02X %02X %02X %02X %02X %02X %02X",
    mcp2515_lastRxData[0],mcp2515_lastRxData[1],mcp2515_lastRxData[2],mcp2515_lastRxData[3],
    mcp2515_lastRxData[4],mcp2515_lastRxData[5],mcp2515_lastRxData[6],mcp2515_lastRxData[7]);
  j += "\"mcpLastData\":\"" + String(db) + "\",";
  j += "\"mcpLastDlc\":" + String(mcp2515_lastRxDlc) + ",";
  String ids=""; for(uint8_t i=0;i<mcp2515_seenCount;i++){
    char ib[12]; snprintf(ib,sizeof(ib),"0x%lX",(unsigned long)(mcp2515_seenIds[i]&0x1FFFFFFFUL));
    if(mcp2515_seenIds[i]&0x80000000UL) strcat(ib,"*"); // * = extended
    if(i>0) ids+=" "; ids+=ib;}
  j += "\"mcpAllIds\":\"" + ids + "\",";}
  {char ptx[80]; snprintf(ptx,sizeof(ptx),"CVL=%u DVL=%u CCL=%u DCL=%u T=%d",pylonDbg_CVL,pylonDbg_DVL,pylonDbg_CCL,pylonDbg_DCL,pylonDbg_temp);
  j += "\"pylonTx\":\"" + String(ptx) + "\",";}
  j += "\"learnPhase\":\"" + String(learnPhase==LEARN_FROM_FULL ? "RUN" : "IDLE") + "\",";
  j += "\"learnDAh\":" + String(learnDischargeAh, 2) + ",";
  j += "\"fullConfirmed\":" + String(fullConfirmed ? 1 : 0) + ",";

  j += "\"cells\":[";
  uint16_t g = 1;
  bool first = true;
  for (uint8_t id=1; id<=MAX_MODULES; id++) {
    if (!modules[id].active) continue;
    for (uint8_t i=0; i<modules[id].cellCount && i<MAX_CELLS_PER_MODULE; i++) {
      float v = modules[id].cells[i];
      if (v <= 0.001f) continue;
      if (!first) j += ",";
      first = false;
      j += "{";
      j += "\"g\":" + String(g) + ",";
      j += "\"id\":" + String(id) + ",";
      j += "\"l\":" + String(i+1) + ",";
      j += "\"v\":" + String(v, 3);
      j += "}";
      g++;
    }
  }
  j += "]";
  j += "}";

  server.send(200, "application/json", j);
}

static void handleApiHistory() {
  requireAnyUser();
  if (!isUserOrAdmin()) return;

  // Get optional range parameter (seconds to show, default=all)
  uint16_t rangeSeconds = 0;  // 0 = all data
  if (server.hasArg("range")) {
    rangeSeconds = (uint16_t)server.arg("range").toInt();
  }

  // Calculate how many points to include based on range (5s per sample)
  uint16_t maxPoints = historyCount;
  if (rangeSeconds > 0) {
    maxPoints = min((uint16_t)(rangeSeconds / 5), historyCount);
  }

  // Calculate downsampling factor to limit response size (max ~120 points for UI)
  uint16_t step = 1;
  if (maxPoints > 120) {
    step = (maxPoints + 119) / 120;  // ceil division
  }

  String j = "{\"points\":[";
  bool first = true;

  // Output oldest to newest (within range, downsampled)
  uint16_t startIdx = historyCount - maxPoints;
  for (uint16_t i = startIdx; i < historyCount; i += step) {
    uint16_t idx = (historyHead + HISTORY_SIZE - historyCount + i) % HISTORY_SIZE;
    if (!first) j += ",";
    first = false;
    j += "{";
    j += "\"ts\":" + String(history[idx].ts) + ",";
    j += "\"v\":" + String(history[idx].packV, 2) + ",";
    j += "\"a\":" + String(history[idx].currentA, 2) + ",";
    j += "\"soc\":" + String(history[idx].soc, 1) + ",";
    j += "\"min\":" + String(history[idx].minCell, 3) + ",";
    j += "\"max\":" + String(history[idx].maxCell, 3) + ",";
    j += "\"t\":" + String(history[idx].tempC, 1);
    j += "}";
  }
  j += "],\"count\":" + String(historyCount) + ",\"range\":" + String(rangeSeconds) + ",\"step\":" + String(step) + "}";

  server.send(200, "application/json", j);
}

static void handleApiEvents() {
  requireAnyUser();
  if (!isUserOrAdmin()) return;

  uint16_t stored = (evtCount < EVENT_LOG_SIZE) ? evtCount : EVENT_LOG_SIZE;
  String j = "{\"events\":[";
  bool first = true;
  for (uint16_t i = 0; i < stored; i++) {
    uint8_t idx = (evtCount < EVENT_LOG_SIZE) ? i : ((evtHead + EVENT_LOG_SIZE - stored + i) % EVENT_LOG_SIZE);
    if (!first) j += ",";
    first = false;
    j += "{\"ts\":" + String((long)eventLog[idx].ts) + ",\"type\":" + String((int)eventLog[idx].type) + ",\"msg\":\"" + String(eventLog[idx].msg) + "\",\"tsStr\":\"";
    if (eventLog[idx].ts > 1700000000) {
      char tbuf[20];
      struct tm ti;
      localtime_r(&eventLog[idx].ts, &ti);
      strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &ti);
      j += tbuf;
    } else {
      j += "uptime " + String((long)eventLog[idx].ts) + "s";
    }
    j += "\"}";
  }
  j += "],\"count\":" + String(stored) + "}";
  server.send(200, "application/json", j);
}

// ===================== MQTT Functions =====================

static const char* stateToStr(SysState s) {
  switch(s) {
    case ST_OPEN: return "OPEN";
    case ST_PRECHARGING: return "PRECHARGING";
    case ST_CLOSED: return "CLOSED";
    case ST_TRIPPED: return "TRIPPED";
    default: return "UNKNOWN";
  }
}

static void mqttBuildTopics() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(mqttDeviceId, sizeof(mqttDeviceId), "%02X%02X%02X%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  snprintf(mqttTopicTelemetry, sizeof(mqttTopicTelemetry), "bms/%s/telemetry", mqttDeviceId);
  snprintf(mqttTopicFaults,    sizeof(mqttTopicFaults),    "bms/%s/faults",    mqttDeviceId);
  snprintf(mqttTopicStatus,    sizeof(mqttTopicStatus),    "bms/%s/status",    mqttDeviceId);
  snprintf(mqttTopicAck,       sizeof(mqttTopicAck),       "bms/%s/ack",       mqttDeviceId);
  snprintf(mqttTopicOnline,    sizeof(mqttTopicOnline),    "bms/%s/online",    mqttDeviceId);
  snprintf(mqttTopicCmdPrefix, sizeof(mqttTopicCmdPrefix), "bms/%s/cmd/",      mqttDeviceId);
}

static void mqttPublishTelemetry() {
  if (!mqttClient.connected()) return;

  JsonDocument doc;
  struct tm ti;
  time_t now = time(nullptr);
  localtime_r(&now, &ti);
  doc["ts"] = (ti.tm_year > 100) ? (uint32_t)now : millis() / 1000;

  doc["packV"]    = serialized(String(packTotalV, 2));
  doc["minCell"]  = serialized(String(packMinCellV, 3));
  doc["maxCell"]  = serialized(String(packMaxCellV, 3));
  doc["delta"]    = serialized(String(packDeltaV, 3));
  doc["tavg"]     = serialized(String(packAvgTempC, 1));
  doc["soc"]      = serialized(String(socPct, 1));
  doc["currentA"] = serialized(String(currentValid ? ((float)current_mA / 1000.0f) : 0.0f, 2));
  doc["powerW"]   = serialized(String(power_W, 1));
  doc["state"]    = stateToStr(state);
  doc["armed"]    = armed ? 1 : 0;
  doc["allowChg"] = allowCharge ? 1 : 0;
  doc["allowDsg"] = allowDischarge ? 1 : 0;
  doc["taperChg"] = serialized(String(taperChgFactor, 2));
  doc["taperDis"] = serialized(String(taperDisFactor, 2));
  doc["faults"]   = faultText;

  JsonArray cells = doc["cells"].to<JsonArray>();
  for (uint8_t id = 1; id <= MAX_MODULES; id++) {
    if (!modules[id].active) continue;
    JsonArray modArr = cells.add<JsonArray>();
    for (uint8_t i = 0; i < modules[id].cellCount && i < MAX_CELLS_PER_MODULE; i++) {
      modArr.add(serialized(String(modules[id].cells[i], 3)));
    }
  }

  char buf[1024];
  size_t len = serializeJson(doc, buf, sizeof(buf));
  mqttClient.publish(mqttTopicTelemetry, (const uint8_t*)buf, len, false);
}

static void mqttPublishFaultChange() {
  if (!mqttClient.connected()) return;
  if (faultText == lastFaultText) return;
  lastFaultText = faultText;

  JsonDocument doc;
  doc["faults"] = faultText;
  doc["critical"] = criticalFault ? 1 : 0;
  doc["directional"] = directionalFault ? 1 : 0;
  char buf[256];
  size_t len = serializeJson(doc, buf, sizeof(buf));
  mqttClient.publish(mqttTopicFaults, (const uint8_t*)buf, len, false);
}

static void mqttPublishStateChange() {
  if (!mqttClient.connected()) return;
  String s = stateToStr(state);
  if (s == lastStateStr) return;
  lastStateStr = s;

  JsonDocument doc;
  doc["state"] = s;
  doc["armed"] = armed ? 1 : 0;
  char buf[128];
  size_t len = serializeJson(doc, buf, sizeof(buf));
  mqttClient.publish(mqttTopicStatus, (const uint8_t*)buf, len, true);
}

static void mqttSendAck(const char* cmdType, bool ok, const char* msg = nullptr) {
  if (!mqttClient.connected()) return;
  JsonDocument doc;
  doc["cmd"] = cmdType;
  doc["ok"] = ok ? 1 : 0;
  if (msg) doc["msg"] = msg;
  char buf[256];
  size_t len = serializeJson(doc, buf, sizeof(buf));
  mqttClient.publish(mqttTopicAck, (const uint8_t*)buf, len, false);
}

// Forward declarations for config/action handlers used by MQTT
static void loadConfig();
static void saveConfig();

static void mqttOnMessage(char* topic, byte* payload, unsigned int length) {
  if (length > 512) return;

  char json[513];
  memcpy(json, payload, length);
  json[length] = 0;

  String t(topic);
  String prefix(mqttTopicCmdPrefix);

  if (!t.startsWith(prefix)) return;
  String cmd = t.substring(prefix.length());

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    mqttSendAck(cmd.c_str(), false, "JSON parse error");
    return;
  }

  if (cmd == "config") {
    // Update limits from received JSON
    bool changed = false;
    if (doc["cellOv"].is<float>())  { limits.cellOv   = doc["cellOv"].as<float>();  changed = true; }
    if (doc["cellUv"].is<float>())  { limits.cellUv   = doc["cellUv"].as<float>();  changed = true; }
    if (doc["packOv"].is<float>())  { limits.packOv   = doc["packOv"].as<float>();  changed = true; }
    if (doc["packUv"].is<float>())  { limits.packUv   = doc["packUv"].as<float>();  changed = true; }
    if (doc["deltaV"].is<float>())  { limits.deltaMax  = doc["deltaV"].as<float>(); changed = true; }
    if (doc["tempMax"].is<float>()) { limits.tempMax   = doc["tempMax"].as<float>(); changed = true; }
    if (doc["tempMin"].is<float>()) { limits.tempMin   = doc["tempMin"].as<float>(); changed = true; }
    if (changed) {
      saveConfig();
      mqttSendAck("config", true, "Config updated and saved");
    } else {
      mqttSendAck("config", false, "No recognized fields");
    }
  }
  else if (cmd == "action") {
    String action = doc["action"] | "";
    if (action == "arm") {
      armed = true;
      mqttSendAck("action", true, "Armed");
    } else if (action == "disarm") {
      armed = false;
      mqttSendAck("action", true, "Disarmed");
    } else if (action == "reset") {
      armed = false;
      state = ST_OPEN;
      mqttSendAck("action", true, "Reset to OPEN");
    } else {
      mqttSendAck("action", false, "Unknown action");
    }
  }
  else if (cmd == "ota") {
    String url = doc["url"] | "";
    String checksum = doc["checksum"] | "";  // MD5 hex
    String version = doc["version"] | "";

    if (url.length() == 0) {
      mqttSendAck("ota", false, "Missing 'url' field");
      return;
    }

    // Prevent OTA while contactors are closed
    if (state == ST_CLOSED || state == ST_PRECHARGING) {
      mqttSendAck("ota", false, "Cannot OTA while contactors closed");
      return;
    }

    mqttSendAck("ota", true, "OTA starting...");
    delay(100);  // Let ACK publish

    // Use insecure or TLS client based on URL
    WiFiClient& otaClient = url.startsWith("https") ? (WiFiClient&)mqttTlsClient : (WiFiClient&)mqttPlainClient;

    httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (checksum.length() == 32) {
      Update.setMD5(checksum.c_str());
    }

    t_httpUpdate_return ret = httpUpdate.update(otaClient, url);

    switch (ret) {
      case HTTP_UPDATE_OK:
        // Device will reboot automatically
        break;
      case HTTP_UPDATE_FAILED:
        mqttSendAck("ota", false, httpUpdate.getLastErrorString().c_str());
        break;
      case HTTP_UPDATE_NO_UPDATES:
        mqttSendAck("ota", false, "No update available");
        break;
    }
  }
  else {
    mqttSendAck(cmd.c_str(), false, "Unknown command");
  }
}

#ifdef HA_DISCOVERY
static void mqttPublishHADiscovery() {
  if (!mqttClient.connected()) return;

  struct HASensor {
    const char* entity;
    const char* name;
    const char* valueTemplate;
    const char* unit;
    const char* deviceClass;
  };

  HASensor sensors[] = {
    {"packV",    "Pack Voltage",    "{{ value_json.packV }}",    "V",  "voltage"},
    {"minCell",  "Min Cell",        "{{ value_json.minCell }}",  "V",  "voltage"},
    {"maxCell",  "Max Cell",        "{{ value_json.maxCell }}",  "V",  "voltage"},
    {"delta",    "Cell Delta",      "{{ value_json.delta }}",    "V",  "voltage"},
    {"soc",      "State of Charge", "{{ value_json.soc }}",      "%",  "battery"},
    {"tavg",     "Temperature",     "{{ value_json.tavg }}",     "°C", "temperature"},
    {"currentA", "Current",         "{{ value_json.currentA }}", "A",  "current"},
    {"powerW",   "Power",           "{{ value_json.powerW }}",   "W",  "power"},
  };

  for (auto& s : sensors) {
    JsonDocument disc;
    disc["name"] = String("BMS ") + s.name;
    disc["state_topic"] = mqttTopicTelemetry;
    disc["value_template"] = s.valueTemplate;
    disc["unit_of_measurement"] = s.unit;
    disc["device_class"] = s.deviceClass;
    disc["unique_id"] = String("bms_") + mqttDeviceId + "_" + s.entity;

    JsonObject dev = disc["device"].to<JsonObject>();
    dev["identifiers"].add(String("bms_") + mqttDeviceId);
    dev["name"] = String("JK BMS ") + mqttDeviceId;
    dev["manufacturer"] = "Terrepower";
    dev["model"] = "JK BMS Master";

    char topic[128];
    snprintf(topic, sizeof(topic), "homeassistant/sensor/bms_%s/%s/config", mqttDeviceId, s.entity);
    char buf[512];
    size_t len = serializeJson(disc, buf, sizeof(buf));
    mqttClient.publish(topic, (const uint8_t*)buf, len, true);
  }

  // Armed switch
  {
    JsonDocument disc;
    disc["name"] = "BMS Armed";
    disc["state_topic"] = mqttTopicStatus;
    disc["command_topic"] = String("bms/") + mqttDeviceId + "/cmd/action";
    disc["value_template"] = "{{ 'ON' if value_json.armed == 1 else 'OFF' }}";
    disc["payload_on"] = "{\"action\":\"arm\"}";
    disc["payload_off"] = "{\"action\":\"disarm\"}";
    disc["unique_id"] = String("bms_") + mqttDeviceId + "_armed";

    JsonObject dev = disc["device"].to<JsonObject>();
    dev["identifiers"].add(String("bms_") + mqttDeviceId);
    dev["name"] = String("JK BMS ") + mqttDeviceId;
    dev["manufacturer"] = "Terrepower";
    dev["model"] = "JK BMS Master";

    char topic[128];
    snprintf(topic, sizeof(topic), "homeassistant/switch/bms_%s/armed/config", mqttDeviceId);
    char buf[512];
    size_t len = serializeJson(disc, buf, sizeof(buf));
    mqttClient.publish(topic, (const uint8_t*)buf, len, true);
  }
}
#endif

static bool mqttConnect() {
  if (mqttEndpoint.length() == 0) return false;

  char willTopic[48];
  snprintf(willTopic, sizeof(willTopic), "bms/%s/online", mqttDeviceId);

  bool connected = false;
  if (mqttUser.length() > 0) {
    // Connect with username/password
    connected = mqttClient.connect(mqttDeviceId, mqttUser.c_str(), mqttPass.c_str(), willTopic, 1, true, "0");
  } else {
    // Connect without authentication
    connected = mqttClient.connect(mqttDeviceId, nullptr, nullptr, willTopic, 1, true, "0");
  }

  if (connected) {
    Serial.printf("✅ MQTT conectado a %s\n", mqttEndpoint.c_str());
    mqttReconnectInterval = MQTT_RECONNECT_MIN_MS;

    // Subscribe to command topics
    char subTopic[48];
    snprintf(subTopic, sizeof(subTopic), "bms/%s/cmd/#", mqttDeviceId);
    mqttClient.subscribe(subTopic);

    // Publish online
    mqttClient.publish(willTopic, (const uint8_t*)"1", 1, true);

#ifdef HA_DISCOVERY
    mqttPublishHADiscovery();
#endif
    return true;
  }
  Serial.printf("❌ MQTT connect fail rc=%d\n", mqttClient.state());
  return false;
}

static void mqttSetup() {
  mqttBuildTopics();

  // Try loading certs from LittleFS
  if (LittleFS.begin(true)) {
    File ca = LittleFS.open("/mqtt_ca.pem", "r");
    if (ca) {
      String caCert = ca.readString();
      ca.close();
      // WiFiClientSecure needs the cert to persist - use static buffer
      static char caBuf[2048];
      caCert.toCharArray(caBuf, sizeof(caBuf));
      mqttTlsClient.setCACert(caBuf);
    }

    File cert = LittleFS.open("/mqtt_cert.pem", "r");
    if (cert) {
      String clientCert = cert.readString();
      cert.close();
      static char certBuf[2048];
      clientCert.toCharArray(certBuf, sizeof(certBuf));
      mqttTlsClient.setCertificate(certBuf);
    }

    File key = LittleFS.open("/mqtt_key.pem", "r");
    if (key) {
      String clientKey = key.readString();
      key.close();
      static char keyBuf[2048];
      clientKey.toCharArray(keyBuf, sizeof(keyBuf));
      mqttTlsClient.setPrivateKey(keyBuf);
    }

    // Load config from JSON file as fallback ONLY if NVS endpoint is empty
    // NVS settings (loaded in loadConfig) take priority
    if (mqttEndpoint.length() == 0) {
      File cfg = LittleFS.open("/mqtt_config.json", "r");
      if (cfg) {
        JsonDocument cfgDoc;
        deserializeJson(cfgDoc, cfg);
        cfg.close();
        mqttEndpoint = cfgDoc["endpoint"] | "";
        if (cfgDoc["port"].is<int>()) {
          mqttPort = cfgDoc["port"].as<uint16_t>();
        }
        if (cfgDoc["insecure"].is<bool>()) {
          mqttInsecure = cfgDoc["insecure"].as<bool>();
        }
        if (cfgDoc["user"].is<const char*>()) {
          mqttUser = cfgDoc["user"].as<String>();
        }
        if (cfgDoc["password"].is<const char*>()) {
          mqttPass = cfgDoc["password"].as<String>();
        }
      }
    }
  }

  if (mqttEndpoint.length() > 0) {
    mqttEnabled = true;
    if (mqttInsecure) {
      mqttClient.setClient(mqttPlainClient);
      Serial.println("⚠️ MQTT modo inseguro (sin TLS) - solo para desarrollo");
    } else {
      mqttClient.setClient(mqttTlsClient);
    }
    mqttClient.setServer(mqttEndpoint.c_str(), mqttPort);
    mqttClient.setCallback(mqttOnMessage);
    mqttClient.setBufferSize(1280);
    Serial.printf("🔌 MQTT configurado: %s:%d (ID: %s)\n", mqttEndpoint.c_str(), mqttPort, mqttDeviceId);
  } else {
    Serial.println("ℹ️ MQTT deshabilitado (sin endpoint configurado)");
  }
}

static void mqttLoop() {
  if (!mqttEnabled) return;
  if (WiFi.status() != WL_CONNECTED) return;

  mqttClient.loop();

  if (!mqttClient.connected()) {
    uint32_t now = millis();
    if (now - mqttLastReconnectAttempt >= mqttReconnectInterval) {
      mqttLastReconnectAttempt = now;
      Serial.printf("🔄 MQTT reconectando... (rc=%d)\n", mqttClient.state());
      if (!mqttConnect()) {
        mqttReconnectInterval = min(mqttReconnectInterval * 2, (uint32_t)MQTT_RECONNECT_MAX_MS);
      }
    }
    return;
  }

  uint32_t now = millis();
  if (now - mqttLastTelemetry >= MQTT_TELEMETRY_INTERVAL_MS) {
    mqttLastTelemetry = now;
    mqttPublishTelemetry();
  }

  mqttPublishFaultChange();
  mqttPublishStateChange();
}

// ===================== End MQTT =====================

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("⏳ Iniciando JK BMS + Contactores + SoC/SoH learning ...");

  loadConfig();
  loadEnergyState();

  pinMode(GPIO_PRECHARGE_CONTACTOR, OUTPUT);
  pinMode(GPIO_CHG_CONTACTOR, OUTPUT);
  pinMode(GPIO_DSG_CONTACTOR, OUTPUT);
  setContactors(false,false,false,false);

#ifdef ME2107_EN
  pinMode(ME2107_EN, OUTPUT);
  digitalWrite(ME2107_EN, HIGH);
#endif
#ifdef CAN_SPEED_MODE
  pinMode(CAN_SPEED_MODE, OUTPUT);
  digitalWrite(CAN_SPEED_MODE, LOW);
#endif

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("🌐 Conectando a WiFi");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
    if (millis() - t0 > 20000) {
      Serial.println("\n⚠️ WiFi timeout, reintentando...");
      WiFi.disconnect(true, true);
      delay(1000);
      WiFi.begin(ssid, password);
      t0 = millis();
    }
  }
  Serial.println();
  Serial.printf("✅ WiFi OK. IP: %s\n", WiFi.localIP().toString().c_str());

  // NTP time sync
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
  Serial.println("🕐 NTP configurado");

  // CAN init
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)CAN_TX, (gpio_num_t)CAN_RX, TWAI_MODE_NORMAL);
  twai_timing_config_t t = TWAI_TIMING_CONFIG_250KBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  esp_err_t err = twai_driver_install(&g, &t, &f);
  if (err != ESP_OK) Serial.printf("❌ twai_driver_install: %d\n", (int)err);
  err = twai_start();
  if (err != ESP_OK) Serial.printf("❌ twai_start: %d\n", (int)err);
  else Serial.println("✅ Bus CAN1 (BMS) activo - 250 kbps");

  // MCP2515 init (second CAN bus for inverter at 500 kbps)
#ifdef MCP2515_CS
  SPI.begin(MCP2515_SCK, MCP2515_MISO, MCP2515_MOSI, MCP2515_CS);
#ifdef MCP2515_RST
  pinMode(MCP2515_RST, OUTPUT);
  digitalWrite(MCP2515_RST, LOW);
  delay(10);
  digitalWrite(MCP2515_RST, HIGH);
  delay(10);
#endif
  mcp2515.reset();

  // Loopback self-test
  mcp2515.setBitrate(CAN_500KBPS, MCP_16MHZ);
  mcp2515.setLoopbackMode();
  {
    struct can_frame txTest, rxTest;
    txTest.can_id = 0x7FF;
    txTest.can_dlc = 2;
    txTest.data[0] = 0xAB;
    txTest.data[1] = 0xCD;
    bool loopOk = false;
    if (mcp2515.sendMessage(&txTest) == MCP2515::ERROR_OK) {
      delay(10);
      if (mcp2515.readMessage(&rxTest) == MCP2515::ERROR_OK) {
        if (rxTest.can_id == 0x7FF && rxTest.data[0] == 0xAB && rxTest.data[1] == 0xCD) {
          loopOk = true;
        }
      }
    }
    if (loopOk) {
      Serial.println("✅ MCP2515 loopback OK");
    } else {
      Serial.println("⚠️ MCP2515 loopback FAIL - check SPI wiring");
    }
  }

  // Switch to normal mode for real operation
  mcp2515.reset();
  if (mcp2515.setBitrate(CAN_500KBPS, MCP_16MHZ) == MCP2515::ERROR_OK) {
    // Accept ALL frames (standard + extended) — masks=0 with ext=false means "don't care" for all bits including IDE
    mcp2515.setFilterMask(MCP2515::MASK0, false, 0x00000000);
    mcp2515.setFilterMask(MCP2515::MASK1, false, 0x00000000);
    mcp2515.setFilter(MCP2515::RXF0, false, 0x00000000);
    mcp2515.setFilter(MCP2515::RXF1, false, 0x00000000);
    mcp2515.setFilter(MCP2515::RXF2, false, 0x00000000);
    mcp2515.setFilter(MCP2515::RXF3, false, 0x00000000);
    mcp2515.setFilter(MCP2515::RXF4, false, 0x00000000);
    mcp2515.setFilter(MCP2515::RXF5, false, 0x00000000);
    if (mcp2515.setNormalMode() == MCP2515::ERROR_OK) {
      mcp2515_ok = true;
      Serial.println("✅ Bus CAN2 (Inverter) activo - 500 kbps via MCP2515");
    } else {
      Serial.println("❌ MCP2515 setNormalMode FAIL");
    }
  } else {
    Serial.println("❌ MCP2515 setBitrate FAIL");
  }
#endif

  server.on("/", handleRoot);
  server.on("/config", handleConfig);
  server.on("/setlang", handleSetLang);
  server.on("/applyPreset", handleApplyPreset);
  server.on("/save", handleSave);
  server.on("/actions", handleActions);
  server.on("/api/rev", handleApiRev);
  server.on("/api/status", handleApiStatus);
  server.on("/api/history", handleApiHistory);
  server.on("/api/events", handleApiEvents);
  server.begin();
  Serial.println("🌍 Servidor web listo");

  mqttSetup();
  loadEventLog();  // Load persistent event log from LittleFS

  xTaskCreatePinnedToCore(canTask, "canTask", 16384, nullptr, 1, nullptr, 1);
  Serial.println("🧵 canTask arrancada (core 1)");
}

void loop() {
  server.handleClient();
  mqttLoop();
}
