# Cloud Integration Guide

This guide describes the recommended architecture for integrating JK BMS Master with cloud services for monitoring, alerting, and historical data analysis.

---

## Architecture Overview

```
┌─────────────┐     MQTT      ┌─────────────┐     Write     ┌─────────────┐
│   ESP32     │──────────────►│  Telegraf   │──────────────►│  InfluxDB   │
│  JK BMS     │   TLS/Plain   │  (Bridge)   │  Line Proto   │  (TSDB)     │
│  Master     │               └─────────────┘               └──────┬──────┘
└─────────────┘                                                    │
       │                                                           │ Query
       │ MQTT                  ┌─────────────┐                     ▼
       └──────────────────────►│    Home     │              ┌─────────────┐
                               │  Assistant  │              │   Grafana   │
                               │  (Optional) │              │ (Dashboards)│
                               └─────────────┘              └─────────────┘
```

### Components

| Component | Purpose | Recommended |
|-----------|---------|-------------|
| **MQTT Broker** | Message routing | Mosquitto, EMQX, HiveMQ |
| **Telegraf** | MQTT → InfluxDB bridge | InfluxData Telegraf |
| **InfluxDB** | Time-series database | InfluxDB 2.x OSS |
| **Grafana** | Visualization & alerting | Grafana OSS |
| **Home Assistant** | Home automation (optional) | HA with MQTT integration |

---

## MQTT Topic Structure

The ESP32 publishes to these topics:

```
bms/{device_id}/telemetry    # Pack data every 5 seconds
bms/{device_id}/event        # Faults, state changes (on-event)
bms/{device_id}/online       # LWT: "online" or "offline"
```

And subscribes to:

```
bms/{device_id}/cmd/config   # Remote configuration updates
bms/{device_id}/cmd/action   # arm, disarm, reset commands
bms/{device_id}/cmd/ota      # Firmware update trigger
```

### Telemetry Payload (JSON)

```json
{
  "ts": 1707145200,
  "packV": 172.45,
  "currentA": 12.34,
  "soc": 85.2,
  "soh": 98.5,
  "minCell": 3.312,
  "maxCell": 3.345,
  "delta": 0.033,
  "tempAvg": 24.5,
  "tempMin": 22.0,
  "tempMax": 27.0,
  "state": "CLOSED",
  "armed": true,
  "fault": null,
  "ccl": 100,
  "dcl": 100,
  "cPre": false,
  "cChg": true,
  "cDsg": true,
  "cells": 48,
  "modules": 3,
  "learnPhase": "IDLE",
  "learnDAh": 0.00,
  "fullConfirmed": false,
  "balTimeH": 2.50,
  "nightActive": false,
  "taperChg": 1.00,
  "taperDis": 1.00
}
```

### Event Payload (JSON)

```json
{
  "ts": 1707145200,
  "type": "FAULT",
  "msg": "Cell OV: BMS2-Cell5 3.652V",
  "severity": "critical"
}
```

---

## Option 1: Self-Hosted Stack (Recommended)

### Requirements

- Linux server (Ubuntu 22.04+, Debian 12+, or Docker)
- 2GB RAM minimum, 4GB recommended
- 20GB storage for 1 year of data

### Docker Compose Setup

Create `docker-compose.yml`:

```yaml
version: '3.8'

services:
  mosquitto:
    image: eclipse-mosquitto:2
    container_name: mosquitto
    ports:
      - "1883:1883"
      - "8883:8883"
    volumes:
      - ./mosquitto/config:/mosquitto/config
      - ./mosquitto/data:/mosquitto/data
      - ./mosquitto/log:/mosquitto/log
    restart: unless-stopped

  telegraf:
    image: telegraf:latest
    container_name: telegraf
    volumes:
      - ./telegraf/telegraf.conf:/etc/telegraf/telegraf.conf:ro
    depends_on:
      - mosquitto
      - influxdb
    restart: unless-stopped

  influxdb:
    image: influxdb:2
    container_name: influxdb
    ports:
      - "8086:8086"
    volumes:
      - ./influxdb/data:/var/lib/influxdb2
      - ./influxdb/config:/etc/influxdb2
    environment:
      - DOCKER_INFLUXDB_INIT_MODE=setup
      - DOCKER_INFLUXDB_INIT_USERNAME=admin
      - DOCKER_INFLUXDB_INIT_PASSWORD=your-secure-password
      - DOCKER_INFLUXDB_INIT_ORG=home
      - DOCKER_INFLUXDB_INIT_BUCKET=bms
      - DOCKER_INFLUXDB_INIT_ADMIN_TOKEN=your-admin-token
    restart: unless-stopped

  grafana:
    image: grafana/grafana:latest
    container_name: grafana
    ports:
      - "3000:3000"
    volumes:
      - ./grafana/data:/var/lib/grafana
    environment:
      - GF_SECURITY_ADMIN_PASSWORD=your-grafana-password
    depends_on:
      - influxdb
    restart: unless-stopped
```

### Mosquitto Configuration

Create `mosquitto/config/mosquitto.conf`:

```conf
# Basic settings
listener 1883
protocol mqtt

# TLS listener (optional but recommended)
listener 8883
protocol mqtt
certfile /mosquitto/config/certs/server.crt
keyfile /mosquitto/config/certs/server.key
cafile /mosquitto/config/certs/ca.crt

# Authentication
allow_anonymous false
password_file /mosquitto/config/passwd

# Persistence
persistence true
persistence_location /mosquitto/data/

# Logging
log_dest file /mosquitto/log/mosquitto.log
log_type all
```

Create password file:
```bash
docker exec -it mosquitto mosquitto_passwd -c /mosquitto/config/passwd bms_user
```

### Telegraf Configuration

Create `telegraf/telegraf.conf`:

```toml
# Global settings
[agent]
  interval = "5s"
  flush_interval = "10s"

# MQTT Consumer - Main telemetry
[[inputs.mqtt_consumer]]
  servers = ["tcp://mosquitto:1883"]
  topics = ["bms/+/telemetry"]
  username = "bms_user"
  password = "your-mqtt-password"
  data_format = "json"

  # Extract device_id from topic
  [[inputs.mqtt_consumer.topic_parsing]]
    topic = "bms/+/telemetry"
    measurement = "bms_telemetry"
    tags = "_/device_id/_"

# MQTT Consumer - Events
[[inputs.mqtt_consumer]]
  servers = ["tcp://mosquitto:1883"]
  topics = ["bms/+/event"]
  username = "bms_user"
  password = "your-mqtt-password"
  data_format = "json"

  [[inputs.mqtt_consumer.topic_parsing]]
    topic = "bms/+/event"
    measurement = "bms_events"
    tags = "_/device_id/_"

# MQTT Consumer - Online status
[[inputs.mqtt_consumer]]
  servers = ["tcp://mosquitto:1883"]
  topics = ["bms/+/online"]
  username = "bms_user"
  password = "your-mqtt-password"
  data_format = "value"
  data_type = "string"

  [[inputs.mqtt_consumer.topic_parsing]]
    topic = "bms/+/online"
    measurement = "bms_status"
    tags = "_/device_id/_"

# InfluxDB v2 Output
[[outputs.influxdb_v2]]
  urls = ["http://influxdb:8086"]
  token = "your-admin-token"
  organization = "home"
  bucket = "bms"
```

### Starting the Stack

```bash
# Create directories
mkdir -p mosquitto/{config,data,log} telegraf influxdb/{data,config} grafana/data

# Set permissions
chmod -R 777 mosquitto grafana influxdb

# Start services
docker-compose up -d

# Check logs
docker-compose logs -f
```

---

## Option 2: Cloud Providers

### AWS IoT Core + Timestream + Grafana Cloud

```
ESP32 → AWS IoT Core → IoT Rules → Timestream → Grafana Cloud
                              └──→ Lambda → SNS (Alerts)
```

**Pros:** Fully managed, auto-scaling, global
**Cons:** Cost at scale, vendor lock-in

### Azure IoT Hub + Azure Data Explorer

```
ESP32 → IoT Hub → Stream Analytics → Data Explorer → Grafana
                                └──→ Logic Apps (Alerts)
```

### InfluxDB Cloud + Grafana Cloud

```
ESP32 → MQTT (HiveMQ Cloud) → Telegraf (VM) → InfluxDB Cloud → Grafana Cloud
```

**Estimated cost:** ~$20-50/month for small deployments

---

## ESP32 Configuration

### Via Web UI

1. Navigate to `http://<esp32-ip>/config`
2. Scroll to **MQTT** section
3. Configure:
   - **Endpoint:** `your-broker.example.com` or IP
   - **Port:** `1883` (plain) or `8883` (TLS)
   - **No TLS:** Check for local/insecure connections
   - **User/Password:** If authentication enabled
4. Click **Save**

### Via MQTT Config File

Create `data/mqtt_config.json` and upload to LittleFS:

```json
{
  "endpoint": "mqtt.example.com",
  "port": 8883,
  "insecure": false,
  "user": "bms_user",
  "pass": "your-password"
}
```

### TLS Certificates

For TLS connections, upload to LittleFS:
- `mqtt_ca.pem` — CA certificate
- `mqtt_cert.pem` — Client certificate (optional, for mutual TLS)
- `mqtt_key.pem` — Client private key (optional)

---

## Grafana Dashboard

### Import Dashboard

1. Open Grafana (`http://localhost:3000`)
2. Go to **Dashboards → Import**
3. Paste the JSON below or use ID (when published)

### Sample Dashboard JSON

```json
{
  "title": "JK BMS Master",
  "uid": "jk-bms-master",
  "panels": [
    {
      "title": "Pack Voltage",
      "type": "timeseries",
      "gridPos": {"h": 8, "w": 12, "x": 0, "y": 0},
      "targets": [{
        "query": "from(bucket: \"bms\") |> range(start: -1h) |> filter(fn: (r) => r._measurement == \"bms_telemetry\" and r._field == \"packV\")"
      }]
    },
    {
      "title": "Current",
      "type": "timeseries",
      "gridPos": {"h": 8, "w": 12, "x": 12, "y": 0},
      "targets": [{
        "query": "from(bucket: \"bms\") |> range(start: -1h) |> filter(fn: (r) => r._measurement == \"bms_telemetry\" and r._field == \"currentA\")"
      }]
    },
    {
      "title": "State of Charge",
      "type": "gauge",
      "gridPos": {"h": 8, "w": 6, "x": 0, "y": 8},
      "targets": [{
        "query": "from(bucket: \"bms\") |> range(start: -5m) |> filter(fn: (r) => r._measurement == \"bms_telemetry\" and r._field == \"soc\") |> last()"
      }],
      "options": {"min": 0, "max": 100}
    },
    {
      "title": "Cell Delta",
      "type": "stat",
      "gridPos": {"h": 8, "w": 6, "x": 6, "y": 8},
      "targets": [{
        "query": "from(bucket: \"bms\") |> range(start: -5m) |> filter(fn: (r) => r._measurement == \"bms_telemetry\" and r._field == \"delta\") |> last()"
      }],
      "options": {"colorMode": "value"}
    },
    {
      "title": "Min/Max Cell Voltage",
      "type": "timeseries",
      "gridPos": {"h": 8, "w": 12, "x": 12, "y": 8},
      "targets": [
        {"query": "from(bucket: \"bms\") |> range(start: -1h) |> filter(fn: (r) => r._measurement == \"bms_telemetry\" and r._field == \"minCell\")"},
        {"query": "from(bucket: \"bms\") |> range(start: -1h) |> filter(fn: (r) => r._measurement == \"bms_telemetry\" and r._field == \"maxCell\")"}
      ]
    }
  ]
}
```

---

## Alerting

### Grafana Alerts

1. Edit panel → **Alert** tab
2. Create alert rule:
   - **Condition:** `delta > 0.1` (100mV)
   - **Evaluate every:** 10s
   - **For:** 30s
3. Add notification channel (Email, Telegram, Slack)

### Telegram Bot Integration

1. Create bot via [@BotFather](https://t.me/botfather)
2. Get chat ID via [@userinfobot](https://t.me/userinfobot)
3. In Grafana: **Alerting → Contact points → New**
   - Type: Telegram
   - Bot Token: `your-bot-token`
   - Chat ID: `your-chat-id`

### Sample Alert Rules

| Alert | Condition | Severity |
|-------|-----------|----------|
| High Delta | `delta > 0.080` | Warning |
| Critical Delta | `delta > 0.150` | Critical |
| Low SoC | `soc < 20` | Warning |
| Critical SoC | `soc < 10` | Critical |
| Offline | No data for 60s | Critical |
| High Temp | `tempMax > 45` | Warning |
| Cell OV | `maxCell > 3.65` | Critical |
| Cell UV | `minCell < 2.8` | Critical |

---

## Home Assistant Integration

The ESP32 automatically publishes Home Assistant MQTT Discovery messages. Entities appear automatically:

### Auto-discovered Entities

- `sensor.jkbms_pack_voltage`
- `sensor.jkbms_current`
- `sensor.jkbms_soc`
- `sensor.jkbms_soh`
- `sensor.jkbms_min_cell`
- `sensor.jkbms_max_cell`
- `sensor.jkbms_delta`
- `sensor.jkbms_temperature`
- `switch.jkbms_armed`
- `binary_sensor.jkbms_online`

### Manual Configuration (if needed)

```yaml
# configuration.yaml
mqtt:
  sensor:
    - name: "BMS Pack Voltage"
      state_topic: "bms/jkbms001/telemetry"
      value_template: "{{ value_json.packV }}"
      unit_of_measurement: "V"
      device_class: voltage

    - name: "BMS SoC"
      state_topic: "bms/jkbms001/telemetry"
      value_template: "{{ value_json.soc }}"
      unit_of_measurement: "%"
      device_class: battery
```

---

## Data Retention

### InfluxDB Retention Policies

```sql
-- Keep detailed data for 7 days
CREATE RETENTION POLICY "week" ON "bms" DURATION 7d REPLICATION 1

-- Downsample to 1-minute averages for 90 days
CREATE RETENTION POLICY "quarter" ON "bms" DURATION 90d REPLICATION 1

-- Downsample to 1-hour averages for 2 years
CREATE RETENTION POLICY "archive" ON "bms" DURATION 730d REPLICATION 1
```

### Continuous Queries (Downsampling)

```sql
-- 1-minute averages
CREATE CONTINUOUS QUERY "cq_1m" ON "bms"
BEGIN
  SELECT mean(*) INTO "quarter"."bms_telemetry"
  FROM "week"."bms_telemetry"
  GROUP BY time(1m), device_id
END

-- 1-hour averages
CREATE CONTINUOUS QUERY "cq_1h" ON "bms"
BEGIN
  SELECT mean(*) INTO "archive"."bms_telemetry"
  FROM "quarter"."bms_telemetry"
  GROUP BY time(1h), device_id
END
```

---

## Security Best Practices

1. **Always use TLS** for MQTT in production
2. **Use strong passwords** for MQTT and databases
3. **Firewall:** Only expose necessary ports
4. **VPN:** Consider WireGuard for remote access
5. **Updates:** Keep all components updated
6. **Backups:** Regular InfluxDB backups
7. **Certificates:** Use Let's Encrypt for TLS

---

## Troubleshooting

### ESP32 not connecting to MQTT

1. Check broker reachability: `ping broker-ip`
2. Verify credentials in `/config`
3. Check serial monitor for MQTT errors
4. Test with `mosquitto_sub -h broker -t 'bms/#' -v`

### No data in InfluxDB

1. Check Telegraf logs: `docker logs telegraf`
2. Verify topic names match
3. Test MQTT: `mosquitto_sub -h localhost -t 'bms/+/telemetry'`

### Grafana shows "No Data"

1. Verify InfluxDB data source configuration
2. Check bucket name matches
3. Test query in InfluxDB Data Explorer

---

## References

- [Mosquitto Documentation](https://mosquitto.org/documentation/)
- [Telegraf MQTT Consumer](https://docs.influxdata.com/telegraf/latest/plugins/inputs/mqtt_consumer/)
- [InfluxDB v2 Documentation](https://docs.influxdata.com/influxdb/v2/)
- [Grafana Alerting](https://grafana.com/docs/grafana/latest/alerting/)
- [Home Assistant MQTT](https://www.home-assistant.io/integrations/mqtt/)

---

**Last Updated:** March 2026
