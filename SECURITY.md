# Security Policy

## Supported versions

| Version | Supported |
| --- | --- |
| `main` | Yes |
| Older commits | No |

This project is a local flood monitor. It is not a hardened internet-facing
product. The threat model and accepted risks are in
[docs/security.md](docs/security.md).

## Reporting a vulnerability

Do not open a public issue with exploit details.

1. Use GitHub's private vulnerability reporting for this repository if it is
   enabled under the Security tab.
2. Otherwise, open an issue that describes the affected component at a high
   level and ask for a private channel. Do not include credentials, broker
   addresses, or step-by-step exploitation in the public issue.

Include the affected commit, the component (firmware, CI, or broker example),
and the impact you expect. Reports are handled on a best-effort basis.

## Scope

In scope:

- `src/main.cpp` and `lib/flood_logic/`
- MQTT topic and payload handling
- `deploy/mosquitto/` configuration and ACL example
- `.github/workflows/ci.yml`
- `docs/security.md` claims and reproduction steps

Out of scope:

- Third-party libraries and the ESP32 Arduino core
- The Mosquitto, Node-RED, InfluxDB, or Grafana installations themselves
- Networks and brokers outside this repository
- Physical access to the device, unless it exposes a remote path

## Security model

- The reference deployment assumes a trusted local network.
- MQTT uses plain TCP on port 1883. There is no TLS in the firmware.
- Broker authentication is optional in the firmware, but required by the
  example broker configuration.
- Wi-Fi and broker credentials are compiled into the image through build
  flags. They are not encrypted at rest.
- The device has no inbound listener and no OTA update path.

Do not expose the broker or the device to the internet without TLS, per-device
credentials, broker ACLs, and a review of [docs/security.md](docs/security.md).

## Reporting secrets

If you accidentally commit credentials:

1. Rotate the Wi-Fi password and the broker password first.
2. Remove the file from the working tree.
3. Rewrite history only if the repository is public and the secret is still
   valid. Rotation is the real fix.

Never paste real credentials into an issue, pull request, or discussion.
