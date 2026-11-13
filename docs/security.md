# Security Assessment - ESP32 Flood Monitor

Status: code-review assessment of the firmware in this repository.
It is not the result of a live penetration test. Findings marked with a
`Reproduce` block can be verified on a lab network. A local broker hardening
example is included in `deploy/mosquitto/`.

Scope: ESP32 firmware, MQTT topics, and the data path to Node-RED.
Out of scope: broker configuration, Node-RED, InfluxDB, and Grafana, because
they are not versioned in this repository.

Assumption: the broker runs on a local network. If the broker is reachable
from the internet, every High finding below becomes Critical.

## 1. System and trust boundaries

```text
sensors -> ESP32 --Wi-Fi (WPA2-PSK)--> LAN --> MQTT 1883 --> Node-RED
                                                  |              |
                                           no TLS, optional       v
                                           username/password   InfluxDB
                                                |                 |
                                                v                 v
                                          floodmonitor/*       Grafana
                                          iot/flood/*
```

The device has no inbound listener (no web server, no OTA). All traffic is
outbound. The most important asset is not the telemetry - it is the alarm
decision: a false "NORMAL" means no flood warning.

## 2. Assets

1. Alert integrity (`floodmonitor/status`, `alarm_active`, `flood_alert`).
2. Wi-Fi PSK and MQTT broker credentials.
3. Telemetry history in InfluxDB/Grafana.
4. Device availability (a node that cannot publish is a silent node).

## 3. Findings

| ID | Finding | Severity | Depends on |
|----|---------|----------|------------|
| S1 | Anonymous MQTT access allowed by broker | High | broker config |
| S2 | MQTT traffic and credentials sent in plaintext | High | broker config |
| S3 | Retained status topic can be spoofed or suppressed | High | S1 or stolen creds |
| S4 | Credentials recoverable from firmware and build files | Medium | physical/local access |
| S5 | Fixed MQTT client ID allows forced disconnect | Medium | S1 or stolen creds |
| S6 | No message authenticity or replay protection | Medium | S1 or stolen creds |
| S7 | Shared Wi-Fi key and flat network | Medium | network design |
| S8 | Telemetry duplicated on debug and legacy topics | Low | S1 |
| S9 | No flash encryption or secure boot | Low | physical access |

### S1 - Anonymous MQTT access (High)

**What:** Broker authentication is optional in firmware (`main.cpp:166-169`).
If `FLOOD_MQTT_USERNAME` is empty, the client connects anonymously.
**Impact:** Anyone on the LAN can subscribe to all telemetry and publish
arbitrary values, including `ALERT`/`NORMAL`. There is no authorization.
**Reproduce:** `mosquitto_sub -h <broker-ip> -t '#' -v` from a laptop on the
same network. If data appears, access is anonymous.
**Fix:** Create one broker account per device and enable per-topic ACLs.
The ready-to-use example is in `deploy/mosquitto/` (section 5).

### S2 - Plaintext MQTT and credentials (High)

**What:** The client uses `WiFiClient`, not `WiFiClientSecure`
(`main.cpp:78`), so MQTT runs on TCP 1883 with no TLS. MQTT 3.1.1 sends the
username and password in the CONNECT packet in clear text.
**Impact:** Anyone who can capture LAN traffic reads the broker password and
all telemetry; with the password they can also publish.
**Reproduce:** `tcpdump -i any -A 'tcp port 1883'` while the device publishes.
**Fix:** For local use, keep the broker on a trusted/segmented network and use
unique device credentials. For remote use, TLS on 8883 and credentials in NVS
are required (section 6).

### S3 - Retained status topic can be spoofed or suppressed (High)

**What:** `floodmonitor/status` is retained and carries `online`/`offline`,
`ALERT`, and `NORMAL` (`main.cpp:166-169`, `main.cpp:300`). A retained message
stays on the broker after the publisher disconnects.
**Impact:** A single publish can suppress a real alarm or create a fake one,
and the fake state survives the attacker leaving. A consumer that trusts this
topic (as the docs recommend) is misled.
**Reproduce, then undo it:**
`mosquitto_pub -h <broker-ip> -t floodmonitor/status -m NORMAL -r`
The next real status update overwrites it; until then Grafana/Node-RED see
`NORMAL`.
**Fix:** Broker ACLs so only the device identity may write `floodmonitor/status`
(`deploy/mosquitto/acl`); do not mix connection state and alarm state on one
topic; treat status as a hint, not proof. Full fix requires authenticated/signed
payloads (S6).

### S4 - Credentials recoverable from firmware and build files (Medium)

**What:** Wi-Fi and broker credentials are compiled into the binary as plain
strings via build flags (`platformio.ini:38-42`) and also appear in generated
files such as `compile_commands.json` and `.vscode/c_cpp_properties.json`.
**Impact:** Anyone with the device, a firmware dump, or an old build folder can
read the credentials. Strings are not encrypted at rest.
**Reproduce:** `strings .pio/build/esp32dev/firmware.elf | grep -i flood` after
building with real credentials.
**Fix already applied:** `compile_commands.json` and generated `.vscode/*`
files are now git-ignored, so they are not committed. Keep `.pio/` out of Git.
**Remaining fix:** use a throwaway/guest Wi-Fi and a broker account limited to
the device topics; do not reuse a personal password.

### S5 - Fixed MQTT client ID (Medium)

**What:** `MQTT_CLIENT_ID` is the constant `esp32-flood-monitor`
(`main.cpp:67`) for every device.
**Impact:** MQTT brokers allow one session per client ID. A second client using
the same ID forces the device offline in a loop - a cheap denial of service.
It also makes multi-node deployment broken from day one.
**Reproduce:** `mosquitto_sub -h <broker-ip> -i esp32-flood-monitor -t '#'`
and watch the device status go offline.
**Fix applied:** the ID is now derived from the MAC address
(`esp32-flood-<last 3 bytes>`) in `main.cpp:145-149`.

### S6 - No message authenticity or replay protection (Medium)

**What:** Telemetry is plain JSON with no signature, HMAC, timestamp, or
sequence number.
**Impact:** A valid captured message can be replayed, and a forged message is
indistinguishable from a real one even if the network is later encrypted.
**Fix:** Out of scope for a local hobby deployment. If remote, sign payloads
with a per-device key and include a timestamp/nonce.

### S7 - Shared Wi-Fi key and flat network (Medium)

**What:** WPA2-PSK with one shared password; the broker, laptop, and device
share the same LAN.
**Impact:** Any device or guest on the Wi-Fi is on the same segment as the
broker. A compromised laptop gives full MQTT access.
**Fix:** Put the device and broker on a separate IoT VLAN/SSID and block
client-to-client traffic where possible.

### S8 - Duplicate telemetry topics (Low)

**What:** The same payload is published to `floodmonitor/telemetry`,
`floodmonitor/debug`, and four `iot/flood/*` topics (`main.cpp:296-315`).
**Impact:** More copies of the data for an eavesdropper, and more topics that
must be covered by ACLs. No PII is exposed.
**Fix:** Keep the legacy topics only if something still consumes them; otherwise
remove them and the debug duplicate.

### S9 - No flash encryption or secure boot (Low)

**What:** Standard Arduino ESP32 build with no flash encryption, secure boot, or
NVS encryption.
**Impact:** Physical USB/serial access allows firmware readout (ties into S4).
Ties to physical access, not remote attack.
**Fix:** Accept for a local device you control. Enable flash encryption and
secure boot only if the node is deployed in a public place.

## 4. What already reduces risk

- No inbound services on the device; no web UI or OTA to attack.
- Credentials are not in Git; they are injected at build time.
- `compile_commands.json` and generated `.vscode/*` are ignored (S4).
- Broker username/password are supported when set.
- Last Will marks the device `offline` when it dies.
- CI uses a read-only token and pinned dependencies.

These limit exposure. None of them authenticate the device or the data.

## 5. Recommended mitigations, cheapest first

1. **Broker ACLs and one account per device.** The device only needs to write
   `floodmonitor/telemetry`, `floodmonitor/debug`, `floodmonitor/status`, and
   `iot/flood/#`; it never subscribes. Deny everything else. Example:
   `deploy/mosquitto/mosquitto.conf` and `deploy/mosquitto/acl`.
2. **Unique client ID** from the MAC address (S5). Done in firmware.
3. **Network segmentation**: IoT VLAN/SSID, broker not exposed to the internet.
4. **Unique device credential** instead of a shared broker account.
5. **Only if remote:** TLS 8883, credentials in NVS, payload signing, flash
   encryption/secure boot. Do not build these just for the resume.

### Apply the local broker example

```bash
# Create the two accounts (never commit the generated passwd file).
mosquitto_passwd -c deploy/mosquitto/passwd flood-device
mosquitto_passwd deploy/mosquitto/passwd flood-reader

# Run Mosquitto with the config and ACL.
docker run --rm -p 1883:1883 \
  -v "$PWD/deploy/mosquitto:/mosquitto/config" \
  eclipse-mosquitto:2
```

If the container cannot read the files, make them readable:
`chmod 644 deploy/mosquitto/passwd deploy/mosquitto/acl`.

Build the firmware with the device account:

```bash
export FLOOD_WIFI_SSID="..."
export FLOOD_WIFI_PASSWORD="..."
export FLOOD_MQTT_BROKER_HOST="..."
export FLOOD_MQTT_USERNAME="flood-device"
export FLOOD_MQTT_PASSWORD="..."
pio run
```

Verify the ACL before writing it up:

```bash
# Allowed: reader can subscribe.
mosquitto_sub -h <broker-ip> -u flood-reader -P <pass> -t 'floodmonitor/#' -v

# Denied: reader cannot publish a fake alarm.
mosquitto_pub -h <broker-ip> -u flood-reader -P <pass> \
  -t floodmonitor/status -m ALERT -r
```

## 6. Accepted risks (local deployment)

For a home/lab deployment on a trusted network, these are intentionally
accepted: plaintext MQTT (S2), no payload signing (S6), shared Wi-Fi (S7), and
no secure boot (S9). They must be revisited before any internet-facing use.

## 7. Detection ideas

- Broker log: alert when `floodmonitor/status` is published by an IP/MAC that
  is not the device.
- Alert on `online`/`offline` flapping (client ID collision, S5).
- Grafana: alert when telemetry is missing for N intervals - a silent node is
  also a failed flood monitor.

## Appendix - code references

- `main.cpp:67`, `main.cpp:145-149` - MAC-derived client ID
- `main.cpp:78` - plain `WiFiClient`, no TLS
- `main.cpp:166-169` - MQTT connect, optional auth, LWT on status topic
- `main.cpp:296-300` - telemetry/debug publish, retained status
- `main.cpp:306-315` - legacy topics
- `platformio.ini:38-42` - credentials as build flags
- `deploy/mosquitto/mosquitto.conf`, `deploy/mosquitto/acl` - broker hardening
