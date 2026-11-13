# Architecture

This page describes how the firmware is structured, how the main loop spends
its time, and where the safety-critical logic lives.

## Runtime overview

The firmware is a single-threaded Arduino sketch. Everything is cooperative:
`loop()` checks timers and runs short tasks. Inside the loop, the only blocking
operations are the ultrasonic sampling and the DHT22 read.

| Task | Period | Code |
| --- | --- | --- |
| Sensor read | 2500 ms | `readSensors()` |
| OLED refresh | 1000 ms | `updateOled()` |
| Serial status | 10000 ms, only when values change | `printSerialStatus()` |
| Serial heartbeat | 60000 ms | `loop()` |
| MQTT reconnect | 5000 ms while disconnected | `connectMqtt()` |
| Wi-Fi reconnect | 15000 ms while disconnected | `connectWifi()` |
| Alarm output | every loop | LED and buzzer |

## Startup sequence

1. Configure the sensor, buzzer, and LED pins.
2. Start serial at 115200 baud.
3. Start the Wi-Fi connection attempt. This does not block.
4. Build the MQTT client ID from the MAC address.
5. Set the MQTT buffer size and try the first broker connection.
6. Start I2C and the DHT22.
7. Initialize the OLED. A failure is logged and the firmware continues.
8. Draw the startup screen and print the boot banner.

Wi-Fi and MQTT are retried from `loop()`, so a missing network never stops the
sensor or alarm path.

## Main loop

`loop()` runs these checks in order on every pass:

```text
now = millis()

if Wi-Fi is down: connectWifi()          // starts a retry at most every 15 s
mqttClient.loop()                        // keepalive and inbound handling

if 2500 ms elapsed:
    readSensors()
    updateFloodAlertLatch()
    if MQTT is down and 5000 ms elapsed: connectMqtt()
    if MQTT is up: publishTelemetry(); publishLegacyTelemetry()

warning = isAlarmActive()
set LED and buzzer from warning

if 10000 ms elapsed and values changed: printSerialStatus()
else if 60000 ms elapsed: print heartbeat

if 1000 ms elapsed: updateOled()
```

## Sensor model

`SensorData` holds one consistent snapshot:

| Field | Meaning |
| --- | --- |
| `distanceCm` | Sensor-to-water distance, `-1` on timeout |
| `waterLevelPercent` | Clamped 0-100, `-1` for an invalid distance |
| `temperatureC`, `humidityPercent` | DHT22 values, `NAN` when invalid |
| `rainDetected` | Debounced rain state |
| `validDistance`, `validDht` | Validity flags used by the publishers |

### Ultrasonic sampling

`readDistanceCm()` takes five samples, discards timeouts, sorts the valid
values, and returns the median. A single read blocks for up to roughly 350 ms
in the worst case (5 x 30 ms timeout plus 40 ms spacing). This only happens
every 2.5 s. A failure returns `-1`, which keeps the previous alert latch.

### Water level

```text
waterDepth = TANK_DEPTH_CM - distanceCm
percent    = clamp(waterDepth / TANK_DEPTH_CM * 100, 0, 100)
```

The math lives in `lib/flood_logic/flood_logic.cpp` and is unit tested.

### Rain debounce

The rain input is active low. Three consecutive reads in the same state are
required before the debounced state changes. This prevents a single splash or
a noisy comparator from toggling the alarm. The debounce state machine is in
`RainDebounce` and is unit tested.

### Flood latch

The water alert is latched with hysteresis:

| Current state | Condition | Next state |
| --- | --- | --- |
| Not latched | water level >= `ALERT_ENTER_PERCENT` | Latched |
| Latched | water level <= `ALERT_EXIT_PERCENT` | Not latched |
| Latched | invalid reading (`-1`) | Latched |

The latch is deliberately sticky. Once water has been detected, a failed
sensor does not silently clear the alert.

### Alarm output

`isAlarmActive()` is the AND of the latched flood alert and the debounced rain
state. The local LED and buzzer follow this value on every loop. The same value
is published as `alarm_active`.

## MQTT lifecycle

- On connect, the device subscribes to nothing. It is a publisher only.
- The last will is retained `offline` on `floodmonitor/status`.
- After a successful connect, it publishes retained `online`.
- Each telemetry cycle publishes the JSON payload to `floodmonitor/telemetry`,
  the same payload to `floodmonitor/debug`, the legacy topics, and a retained
  `ALERT` or `NORMAL` to `floodmonitor/status`.
- If the connection drops, `connectMqtt()` retries every 5 s.
- The client ID is `esp32-flood-<last 3 MAC bytes>`, so two nodes do not fight
  over one broker session.

The topic and payload contract is in the [README](../README.md#mqtt-interface).
Broker hardening is in [`deploy/mosquitto/`](../deploy/mosquitto/).

## Failure handling

| Failure | Behavior |
| --- | --- |
| Wi-Fi unavailable | Sensor, OLED, and alarm keep running; reconnect every 15 s |
| Broker unavailable | Reconnect every 5 s; sensors keep sampling |
| OLED missing | Logged once; firmware continues without the display |
| Ultrasonic timeout | `distance_cm` is `-1`; alert latch is unchanged |
| DHT22 read failure | Temperature and humidity publish as `-999` |
| Invalid configuration | Empty SSID or broker host simply never connects; no crash |

## Timing and load

The expected load is one device publishing every 2.5 s. The loop has no dynamic
allocation in the steady state, no threads, and no blocking waits on the
network. The MQTT payload is a fixed 256-byte buffer.

Measured local `esp32dev` build:

| Resource | Used | Total |
| --- | --- | --- |
| RAM | 29,312 bytes (8.9%) | 327,680 bytes |
| Flash | 394,497 bytes (30.1%) | 1,310,720 bytes |

## Code map

| Path | Responsibility |
| --- | --- |
| `src/main.cpp` | Hardware setup, loop timing, sensor I/O, OLED, MQTT |
| `lib/flood_logic/flood_logic.*` | Water level, flood latch, rain debounce |
| `test/test_flood_logic/` | Unity tests for the logic library |
| `deploy/mosquitto/` | Local broker config and ACL example |
| `.github/workflows/ci.yml` | Tests, build, static analysis, artifact upload |

## Extending the firmware

- Change a timing value in the constants block at the top of `src/main.cpp`.
- Put pure decision logic in `lib/flood_logic/` and add a test before wiring it
  into the loop.
- Keep network work non-blocking. A blocking wait in `loop()` also blocks the
  alarm path.
- Do not add a second MQTT publisher with the same client ID.
