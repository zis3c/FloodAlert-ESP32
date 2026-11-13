# Hardware and Calibration

This page covers the bill of materials, wiring, mounting, and calibration for
the flood monitor. Pin assignments are defined at the top of `src/main.cpp`.

## Bill of materials

| Part | Quantity | Notes |
| --- | --- | --- |
| ESP32 DevKit (esp32dev) | 1 | 3.3 V logic, 5 V tolerant USB input |
| JSN-SR04T waterproof ultrasonic | 1 | Requires a 5 V supply |
| Rain sensor module with digital output | 1 | Comparator output, active low |
| DHT22 / AM2302 | 1 | Temperature and humidity |
| SSD1306 128x64 I2C OLED | 1 | `0x3C` address, 3.3 V |
| Active buzzer | 1 | 5 V type preferred; drive with a transistor |
| LED | 1 | With a 220-330 ohm series resistor |
| Resistors | 2 | 1 kOhm and 2 kOhm for the echo divider |
| 10 kOhm resistor | 1 | DHT22 data pull-up |
| 5 V supply | 1 | Shared by the ultrasonic sensor and buzzer |

## Pin map

| Signal | ESP32 pin | Direction | Notes |
| --- | --- | --- | --- |
| JSN-SR04T TRIG | GPIO 5 | Output | 3.3 V trigger is accepted |
| JSN-SR04T ECHO | GPIO 18 | Input | 5 V output, use a divider |
| Rain sensor DO | GPIO 27 | Input pull-up | Active low when wet |
| DHT22 DATA | GPIO 4 | Input | 10 kOhm pull-up to 3.3 V |
| Buzzer | GPIO 25 | Output | Through a transistor for 5 V buzzers |
| Status LED | GPIO 26 | Output | Series resistor |
| OLED SDA | GPIO 21 | I2C | Module pull-ups are usually present |
| OLED SCL | GPIO 22 | I2C | Module pull-ups are usually present |

## Wiring notes

### Ultrasonic echo divider

The JSN-SR04T is powered from 5 V and its ECHO output swings to 5 V. The ESP32
is a 3.3 V part. Put a divider on ECHO before it reaches GPIO 18:

```text
ECHO ---- 1k ----+---- GPIO 18
                 |
                2k
                 |
                GND
```

The junction of the 1 kOhm and 2 kOhm resistors gives about 3.3 V. Trigger can
be driven directly from GPIO 5.

### Rain sensor

The module's DO pin is active low: it goes low when water is detected. The
firmware enables the internal pull-up, so a bare sensor board can connect
directly. Most modules are happy at 3.3 V; if yours is powered from 5 V, divide
or level-shift the DO output. Adjust the module potentiometer so the output
switches reliably with a wet finger and stays high when dry.

### DHT22

Use a 10 kOhm pull-up from DATA to 3.3 V. Keep the cable short. The firmware
reads the sensor every 2.5 s, above the 2 s minimum of the DHT22.

### Buzzer and LED

The buzzer is the loud part of the flood alarm. A 5 V active buzzer should be
switched by an NPN transistor with a base resistor, not driven directly from
GPIO 25. The LED on GPIO 26 lights whenever the alarm is active.

### Power and grounding

- The ESP32, JSN-SR04T, DHT22, OLED, rain sensor, and buzzer must share a ground.
- Power the JSN-SR04T from 5 V, not 3.3 V.
- If readings jitter when the buzzer sounds, give the buzzer its own 5 V branch
  and a decoupling capacitor near the driver.

## Mounting

Mount the ultrasonic sensor so it points straight down at the water surface,
with no pipe, mesh, or wire in the cone of the sensor. The JSN-SR04T has a
narrow beam but nearby walls can still create false echoes.

`TANK_DEPTH_CM` is the distance from the sensor face to the lowest point of the
basin, not the water depth. With the default value of 100 cm, the water level
percentage is:

```text
water_level_percent = (100 - distance_cm) / 100 * 100
```

The value is clamped to 0-100. A distance greater than the tank depth reads as
0 percent, and an invalid reading is reported as `-1`.

## Calibration

1. Mount the sensor at its final position and power the device.
2. Fill the basin to a known depth.
3. Read `Distance:` on the OLED or serial output.
4. Compare the measured distance with the real distance from the sensor face to
   the water surface. A small offset is normal; the median of 5 samples removes
   most jitter.
5. Set `TANK_DEPTH_CM` in `src/main.cpp` to the measured sensor-to-bottom
   distance if your installation is not 100 cm.
6. Adjust `ALERT_ENTER_PERCENT` and `ALERT_EXIT_PERCENT` if you want a different
   alarm band. The defaults latch at 1 percent of tank depth and clear at 0
   percent.
7. Rebuild and flash. Run `pio test -e native` after changing the logic.

The alarm is active only when the latched water alert is true and the rain
sensor has been wet for three consecutive reads. This avoids alarming on a
single wet reading or on rising water without rain.

## Measurement limits

- No temperature compensation. The ultrasonic speed of sound changes with air
  temperature; for a tank monitor this is usually a few millimetres.
- The echo timeout is 30 ms, which limits the usable range to roughly 5 m.
- One sensor gives one point measurement. It cannot detect a tilted water
  surface or an obstacle in the cone.
- The rain sensor is binary, not a rain gauge. It reports wet or dry, not
  rainfall amount.
- The firmware samples the ultrasonic sensor five times per update. That read
  can block for a few hundred milliseconds; the alarm state is still driven by
  the main loop between reads.
