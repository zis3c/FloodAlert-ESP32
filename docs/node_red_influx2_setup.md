# Node-RED to InfluxDB 2.x Setup

Use this flow to send MQTT telemetry into the `flood_telemetry` bucket so it appears in InfluxDB Data Explorer and Grafana.

## Import flow

Import:

- [docs/node_red_influx2_flow.json](c:/Users/khair/OneDrive/Documents/PlatformIO/Projects/PROJECT%20TASK/docs/node_red_influx2_flow.json)

## Required env vars

The function node reads these from Node-RED environment variables:

- `INFLUX_URL`
- `INFLUX_ORG`
- `INFLUX_BUCKET`
- `INFLUX_TOKEN`

In your Docker stack, these are already present in `.env`.

## What the flow writes

Measurement:

- `flood_monitor`

Fields:

- `distance_cm`
- `water_level_percent`
- `rain_detected`
- `temperature_c`
- `humidity_percent`
- `flood_alert`
- `alarm_active`
- `wifi_rssi`

Tags:

- `device=esp32-flood-monitor`
- `location=main_site`

## Data Explorer query

Use this in InfluxDB Data Explorer:

```flux
from(bucket: "flood_telemetry")
  |> range(start: -24h)
  |> filter(fn: (r) => r._measurement == "flood_monitor")
```

For a single field:

```flux
from(bucket: "flood_telemetry")
  |> range(start: -24h)
  |> filter(fn: (r) => r._measurement == "flood_monitor")
  |> filter(fn: (r) => r._field == "water_level_percent")
```

## If the bucket is empty

Check these first:

- ESP32 is publishing MQTT successfully
- Node-RED MQTT input is subscribed to `floodmonitor/telemetry`
- Influx token is valid
- `INFLUX_URL` points to the correct InfluxDB instance
- Time range in Data Explorer is wide enough, e.g. `Past 24h`

