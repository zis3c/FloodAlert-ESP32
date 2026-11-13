# ESP32 Flood Monitor -> MQTT -> Node-RED -> InfluxDB -> Grafana

This project now publishes telemetry from the ESP32 to MQTT. Use Node-RED to subscribe to the telemetry topic and write the data into InfluxDB, then visualize it in Grafana.

## MQTT Topics

- `floodmonitor/telemetry`
- `floodmonitor/status`
- `floodmonitor/debug`

## ESP32 Payload

The ESP32 publishes JSON like this:

```json
{
  "distance_cm": 42.35,
  "water_level_percent": 57.6,
  "rain_detected": true,
  "temperature_c": 28.40,
  "humidity_percent": 81.20,
  "flood_alert": true,
  "alarm_active": true,
  "wifi_rssi": -58
}
```

## Recommended Node-RED Flow

1. Install Node-RED nodes:
   - `node-red-contrib-influxdb`
   - optional: `node-red-dashboard` if you want quick widgets

2. Create a flow:
   - MQTT IN node subscribes to `floodmonitor/telemetry`
   - JSON node parses the payload
   - Function node maps the fields for InfluxDB
   - InfluxDB out node writes the point

3. Use these field names in InfluxDB:
   - `distance_cm`
   - `water_level_percent`
   - `rain_detected`
   - `temperature_c`
   - `humidity_percent`
   - `flood_alert`
   - `alarm_active`
   - `wifi_rssi`

## Function Node Example

```javascript
const p = msg.payload;

msg.payload = {
  measurement: "flood_monitor",
  tags: {
    device: "esp32-flood-monitor",
    location: "main_site"
  },
  fields: {
    distance_cm: Number(p.distance_cm),
    water_level_percent: Number(p.water_level_percent),
    rain_detected: p.rain_detected ? 1 : 0,
    temperature_c: Number(p.temperature_c),
    humidity_percent: Number(p.humidity_percent),
    flood_alert: p.flood_alert ? 1 : 0,
    alarm_active: p.alarm_active ? 1 : 0,
    wifi_rssi: Number(p.wifi_rssi)
  }
};

return msg;
```

## Grafana

Create panels for:

- Water level percentage
- Distance from sensor
- Temperature
- Humidity
- Rain detection
- Alarm active state

Suggested panel checks:

- `water_level_percent > 80` => flood warning zone
- `alarm_active == 1` => alarm state

## ESP32 Settings to Edit

Set these environment variables before building:

- `FLOOD_WIFI_SSID`
- `FLOOD_WIFI_PASSWORD`
- `FLOOD_MQTT_BROKER_HOST`
- `FLOOD_MQTT_USERNAME` if needed
- `FLOOD_MQTT_PASSWORD` if needed

The firmware reads these values through PlatformIO build flags, so they do not need to be hardcoded in `src/main.cpp`.

## Notes

- If your rain sensor output is inverted, change the rain polarity in the code.
- If you want the alarm to trigger only on water level and rain together, that logic is already in the firmware.
- If you want a separate Node-RED alert when `alarm_active` becomes true, subscribe to `floodmonitor/status`.
