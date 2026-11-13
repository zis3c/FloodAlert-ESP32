# Contributing

This is a small firmware project. Keep changes focused and keep the alarm path
working when the network or an optional part fails.

## Setup

```bash
python -m pip install "platformio>=6.1,<7"
pio run -e esp32dev
```

Credentials come from environment variables. See the [README](README.md) for
the full list. Never commit real credentials; `compile_commands.json` and
`.pio/` are generated and ignored.

## Before opening a pull request

Run the same checks as CI:

```bash
pio test -e native
pio run -e esp32dev
pio check -e esp32dev --fail-on-defect=low --skip-packages
```

The native tests need a host C++ compiler. On Windows, MinGW or MSYS2 works.

## What goes where

| Change | Location |
| --- | --- |
| Pure decision logic | `lib/flood_logic/` plus a test in `test/` |
| Hardware I/O, timing, MQTT | `src/main.cpp` |
| Broker access rules | `deploy/mosquitto/acl` |
| Deployment steps | `docs/operations.md` |
| Wiring or calibration | `docs/hardware.md` |
| Security claims | `docs/security.md` |

## Tests

Add a test when you change a branch, a threshold, a parser, or any code in
`lib/flood_logic/`. A test that cannot fail is not useful. The existing tests
cover water-level clamping, alert hysteresis, and rain debounce.

Hardware changes should be described in the pull request: sensor model, wiring,
supply voltage, and the calibration method used.

## Style

- Build with `-Wall -Wextra -Werror`; do not silence a warning, fix it.
- Keep one logical change per commit, in the imperative mood
  (`Fix blocking Wi-Fi reconnect`, not `fixed stuff`).
- Comments explain why, not what. One line is usually enough.
- Documentation uses short sentences, tables, and copy-paste commands.
- Do not add a feature that the alarm path does not need. Reliability comes
  before extras.

## Security

Read [SECURITY.md](SECURITY.md) before reporting a vulnerability. Do not put
exploit details, broker addresses, or credentials in a public issue.
