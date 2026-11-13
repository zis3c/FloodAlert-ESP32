# Deployment and Operations

This page is the full deployment path: broker, firmware, first boot,
verification, calibration, monitoring, and updates. For a quick build, see the
[README](../README.md). For wiring, see [hardware.md](hardware.md).

## Checklist

| Step | Done when |
| --- | --- |
| 1. Broker prepared | Device can connect and publish to `floodmonitor/telemetry` |
| 2. Firmware configured | Build uses the correct Wi-Fi and broker settings |
| 3. Device flashed | Serial prints the boot banner and sensor lines |
| 4. Telemetry verified | `mosquitto_sub` shows JSON every 2.5 s |
| 5. Status verified | Retained `online` and `NORMAL` or `ALERT` are present |
| 6. Sensor calibrated | OLED water percentage matches a known level |
| 7. Dashboard connected | Grafana shows fresh points |
| 8. Monitoring configured | A missing node or missing telemetry raises an alert |

## 1. Prepare the broker

Use an existing broker or the local example in `deploy/mosquitto/`. The example
disables anonymous access and gives the device publish-only rights.

```bash
mosquitto_passwd -c deploy/mosquitto/passwd flood-device
mosquitto_passwd deploy/mosquitto/passwd flood-reader

docker run --rm -p 1883:1883 \
  -v "$PWD/deploy/mosquitto:/mosquitto/config" \
  eclipse-mosquitto:2
```

If the container cannot read the files, run
`chmod 644 deploy/mosquitto/passwd deploy/mosquitto/acl`.

Broker requirements:

- Reachable from the device on port 1883 (or your configured port).
- Anonymous access disabled for any non-test deployment.
- The device account may publish to `floodmonitor/#` and `iot/flood/#`.
- Dashboard and ingest accounts are read-only.

Without the example, the firmware still works with an open broker, but anyone
on the network can read or forge telemetry. See
[security.md](security.md).

## 2. Build and flash

Set the build variables in the shell that runs PlatformIO:

```powershell
$env:FLOOD_WIFI_SSID="your_wifi_name"
$env:FLOOD_WIFI_PASSWORD="your_wifi_password"
$env:FLOOD_MQTT_BROKER_HOST="192.168.1.10"
$env:FLOOD_MQTT_USERNAME="flood-device"
$env:FLOOD_MQTT_PASSWORD="your_broker_password"
```

```bash
pio run -e esp32dev
pio run -e esp32dev -t upload
pio device monitor -b 115200
```

Credentials are compiled into the image. Use a dedicated Wi-Fi network and a
device-only broker account, not a personal password.

## 3. First boot

A healthy boot prints approximately:

```text
Connecting WiFi
WiFi connected: 192.168.1.42
MQTT connected
ESP32 Flood Monitoring System Started
Distance: 42.35 cm | Water: 57.6% | Rain: NO | Temp: 28.4C | Hum: 81%
```

The OLED shows the distance, water percentage, rain state, temperature, and
humidity. If the OLED is missing, the firmware logs `SSD1306 init failed,
continuing without display` and keeps running.

If the broker connection fails, the serial output shows
`MQTT failed, rc=<code>` (for example `rc=-2` for an unreachable host).

## 4. Verify telemetry

From a machine with Mosquitto clients installed:

```bash
# All device topics
mosquitto_sub -h 192.168.1.10 -u flood-reader -P <password> -t 'floodmonitor/#' -v

# Telemetry only
mosquitto_sub -h 192.168.1.10 -u flood-reader -P <password> \
  -t floodmonitor/telemetry -v
```

Expected results:

- A JSON message every 2.5 s on `floodmonitor/telemetry`.
- A retained `online` on `floodmonitor/status`.
- A retained `NORMAL` or `ALERT` that follows the alarm state.
- The legacy topics publish when their values are valid.

A direct publish from the reader account to `floodmonitor/status` must be
rejected by the ACL. If it succeeds, the broker is still open.

## 5. Calibrate

Follow [hardware.md](hardware.md#calibration). Update `TANK_DEPTH_CM` and the
alert thresholds in `src/main.cpp`, then rebuild, flash, and run the tests.

## 6. Dashboards

The Node-RED, InfluxDB, and Grafana path is documented in
[mqtt_nodered_influx_grafana.md](mqtt_nodered_influx_grafana.md) and
[node_red_influx2_setup.md](node_red_influx2_setup.md). Import one of the
provided flow files, point it at the broker, and confirm that points arrive in
the bucket.

## 7. Monitoring

A silent node is also a failure. Configure at least one of:

- Alert when no telemetry arrives for 3 intervals (about 10 s).
- Alert when `floodmonitor/status` changes to `offline`.
- Alert when `alarm_active` becomes `true`.
- Watch the serial heartbeat if a technician is on site.

Use a broker-side alert when the status topic is published by an unexpected
client or IP.

## 8. Update firmware

There is no OTA path. Updates are manual:

1. Rebuild with the current credentials.
2. Stop the alarm for the maintenance window if needed.
3. Flash over USB and watch the first serial lines.
4. Confirm telemetry and status on the broker.
5. Recalibrate only if the sensor or mounting changed.

Keep the old `firmware.bin` from the CI artifact if you need to roll back.

## 9. Rotate credentials

1. Create a new broker password for `flood-device`.
2. Update the build environment variable.
3. Rebuild and flash.
4. Remove the old account or password from the broker.
5. If the Wi-Fi password changed, repeat the build for the new Wi-Fi value.

Rotate after sharing a build, replacing a device, or any suspected leak.

## 10. Decommission

1. Publish retained `offline` or clear the retained status topic.
2. Remove the device account and ACL entries from the broker.
3. Delete the local build folder: `.pio/`.
4. Delete the device from dashboards and alerting.

## Troubleshooting

Use the table in the [README](../README.md#troubleshooting) for common
symptoms. For security findings and accepted risks, see
[security.md](security.md).
