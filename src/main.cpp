#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <flood_logic.h>

static constexpr int JSN_TRIG_PIN = 5;
static constexpr int JSN_ECHO_PIN = 18;
static constexpr int RAIN_DO_PIN = 27;
static constexpr int DHT_PIN = 4;
static constexpr int BUZZER_PIN = 25;
static constexpr int LED_PIN = 26;

static constexpr int OLED_WIDTH = 128;
static constexpr int OLED_HEIGHT = 64;
static constexpr int OLED_RESET = -1;
static constexpr uint8_t OLED_ADDR = 0x3C;

static constexpr float TANK_DEPTH_CM = 100.0f;
static constexpr float ALERT_ENTER_PERCENT = 1.0f;
static constexpr float ALERT_EXIT_PERCENT = 0.0f;

static constexpr uint32_t SENSOR_INTERVAL_MS = 2500;
static constexpr uint32_t OLED_INTERVAL_MS = 1000;
static constexpr uint32_t SERIAL_INTERVAL_MS = 10000;
static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 60000;
static constexpr uint32_t MQTT_RETRY_MS = 5000;
static constexpr uint32_t WIFI_RETRY_MS = 15000;
static constexpr uint8_t JSN_SAMPLES = 5;
static constexpr uint8_t RAIN_DEBOUNCE_COUNT = 3;
static constexpr uint8_t DHT_TYPE = DHT22;

// Injected by PlatformIO build flags so credentials stay out of the repo.
#ifndef FLOOD_WIFI_SSID
#define FLOOD_WIFI_SSID ""
#endif

#ifndef FLOOD_WIFI_PASSWORD
#define FLOOD_WIFI_PASSWORD ""
#endif

#ifndef FLOOD_MQTT_BROKER_HOST
#define FLOOD_MQTT_BROKER_HOST ""
#endif

#ifndef FLOOD_MQTT_USERNAME
#define FLOOD_MQTT_USERNAME ""
#endif

#ifndef FLOOD_MQTT_PASSWORD
#define FLOOD_MQTT_PASSWORD ""
#endif

#ifndef FLOOD_MQTT_BROKER_PORT
#define FLOOD_MQTT_BROKER_PORT 1883
#endif

static const char *WIFI_SSID = FLOOD_WIFI_SSID;
static const char *WIFI_PASSWORD = FLOOD_WIFI_PASSWORD;
static const char *MQTT_BROKER_HOST = FLOOD_MQTT_BROKER_HOST;
static constexpr uint16_t MQTT_BROKER_PORT = FLOOD_MQTT_BROKER_PORT;
static const char *MQTT_USERNAME = FLOOD_MQTT_USERNAME;
static const char *MQTT_PASSWORD = FLOOD_MQTT_PASSWORD;
static char mqttClientId[32] = "esp32-flood-monitor";
static const char *MQTT_TELEMETRY_TOPIC = "floodmonitor/telemetry";
static const char *MQTT_STATUS_TOPIC = "floodmonitor/status";
static const char *MQTT_DEBUG_TOPIC = "floodmonitor/debug";
static const char *MQTT_LEGACY_WATER_TOPIC = "iot/flood/water_distance";
static const char *MQTT_LEGACY_RAIN_TOPIC = "iot/flood/rain_status";
static const char *MQTT_LEGACY_TEMP_TOPIC = "iot/flood/temperature_c";
static const char *MQTT_LEGACY_HUM_TOPIC = "iot/flood/humidity_pct";

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
DHT dht(DHT_PIN, DHT_TYPE);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

struct SensorData {
  float distanceCm = -1.0f;
  float waterLevelPercent = -1.0f;
  float temperatureC = NAN;
  float humidityPercent = NAN;
  bool rainDetected = false;
  bool validDistance = false;
  bool validDht = false;
};

static SensorData data;
static SensorData lastPrintedData;
static uint32_t lastSensorReadMs = 0;
static uint32_t lastOledUpdateMs = 0;
static uint32_t lastSerialPrintMs = 0;
static uint32_t lastSerialHeartbeatMs = 0;
static uint32_t lastMqttRetryMs = 0;
static bool hasPrintedOnce = false;
static bool floodAlertLatched = false;
static floodlogic::RainDebounce rainDebounce;
static bool displayReady = false;

static void publishMqttMessage(const char *topic, const char *payload, bool retained = false) {
  if (mqttClient.connected()) {
    mqttClient.publish(topic, payload, retained);
  }
}

static void connectWifi() {
  static bool wifiAttempted = false;
  static bool wifiConnectedLogged = false;
  static uint32_t lastWifiAttemptMs = 0;

  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiConnectedLogged) {
      wifiConnectedLogged = true;
      Serial.print("WiFi connected: ");
      Serial.println(WiFi.localIP());
    }
    return;
  }

  if (wifiConnectedLogged) {
    wifiConnectedLogged = false;
    Serial.println("WiFi lost");
  }

  const uint32_t now = millis();
  if (wifiAttempted && now - lastWifiAttemptMs < WIFI_RETRY_MS) {
    return;
  }

  if (strlen(WIFI_SSID) == 0) {
    return;
  }

  lastWifiAttemptMs = now;
  wifiAttempted = true;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting WiFi");
  Serial.println();
}

static void initMqttClientId() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(mqttClientId, sizeof(mqttClientId), "esp32-flood-%02x%02x%02x", mac[3], mac[4], mac[5]);
}

static void connectMqtt() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  mqttClient.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);

  if (mqttClient.connected()) {
    return;
  }

  const bool hasAuth = strlen(MQTT_USERNAME) > 0;
  bool connected = false;

  if (hasAuth) {
    connected = mqttClient.connect(mqttClientId, MQTT_USERNAME, MQTT_PASSWORD,
                                   MQTT_STATUS_TOPIC, 0, true, "offline");
  } else {
    connected = mqttClient.connect(mqttClientId, MQTT_STATUS_TOPIC, 0, true, "offline");
  }

  if (connected) {
    publishMqttMessage(MQTT_STATUS_TOPIC, "online", true);
    Serial.println("MQTT connected");
  } else {
    Serial.print("MQTT failed, rc=");
    Serial.println(mqttClient.state());
  }
}

static float readDistanceCm() {
  float samples[JSN_SAMPLES];
  uint8_t sampleCount = 0;

  for (uint8_t i = 0; i < JSN_SAMPLES; i++) {
    digitalWrite(JSN_TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(JSN_TRIG_PIN, HIGH);
    delayMicroseconds(5);
    digitalWrite(JSN_TRIG_PIN, LOW);

    const unsigned long duration = pulseIn(JSN_ECHO_PIN, HIGH, 30000UL);
    if (duration != 0) {
      samples[sampleCount++] = (duration * 0.0343f) / 2.0f;
    }

    delay(40);
  }

  if (sampleCount == 0) {
    return -1.0f;
  }

  for (uint8_t i = 0; i < sampleCount - 1; i++) {
    for (uint8_t j = i + 1; j < sampleCount; j++) {
      if (samples[j] < samples[i]) {
        const float temp = samples[i];
        samples[i] = samples[j];
        samples[j] = temp;
      }
    }
  }

  return samples[sampleCount / 2];
}

static void updateBuzzer(bool shouldBeOn) {
  digitalWrite(BUZZER_PIN, shouldBeOn ? HIGH : LOW);
}

static void readSensors() {
  data.distanceCm = readDistanceCm();
  data.validDistance = data.distanceCm >= 0.0f;
  data.waterLevelPercent =
      floodlogic::waterLevelPercent(data.distanceCm, TANK_DEPTH_CM);

  const bool rainRaw = digitalRead(RAIN_DO_PIN) == LOW;
  rainDebounce.update(rainRaw, RAIN_DEBOUNCE_COUNT);
  data.rainDetected = rainDebounce.latched();

  const float humidity = dht.readHumidity();
  const float temperature = dht.readTemperature();
  data.validDht = !isnan(humidity) && !isnan(temperature);
  if (data.validDht) {
    data.humidityPercent = humidity;
    data.temperatureC = temperature;
  } else {
    data.humidityPercent = NAN;
    data.temperatureC = NAN;
  }
}

static bool shouldPrintSerialStatus() {
  if (!hasPrintedOnce) {
    return true;
  }

  const bool distanceChanged =
      (data.validDistance != lastPrintedData.validDistance) ||
      (data.validDistance && fabsf(data.distanceCm - lastPrintedData.distanceCm) >= 1.0f);

  const bool waterChanged =
      (data.waterLevelPercent < 0.0f && lastPrintedData.waterLevelPercent >= 0.0f) ||
      (data.waterLevelPercent >= 0.0f && lastPrintedData.waterLevelPercent < 0.0f) ||
      (data.waterLevelPercent >= 0.0f && fabsf(data.waterLevelPercent - lastPrintedData.waterLevelPercent) >= 2.0f);

  const bool rainChanged = data.rainDetected != lastPrintedData.rainDetected;
  const bool dhtChanged =
      (data.validDht != lastPrintedData.validDht) ||
      (data.validDht &&
       (fabsf(data.temperatureC - lastPrintedData.temperatureC) >= 0.5f ||
        fabsf(data.humidityPercent - lastPrintedData.humidityPercent) >= 3.0f));

  return distanceChanged || waterChanged || rainChanged || dhtChanged;
}

static void updateFloodAlertLatch() {
  floodAlertLatched = floodlogic::nextFloodAlert(
      floodAlertLatched, data.waterLevelPercent, ALERT_ENTER_PERCENT, ALERT_EXIT_PERCENT);
}

static bool isAlarmActive() {
  return floodAlertLatched && rainDebounce.latched();
}

static void publishTelemetry() {
  char payload[256];
  const bool alarmActive = isAlarmActive();

  const float temperature = data.validDht ? data.temperatureC : -999.0f;
  const float humidity = data.validDht ? data.humidityPercent : -999.0f;

  snprintf(payload, sizeof(payload),
           "{\"distance_cm\":%.2f,\"water_level_percent\":%.1f,\"rain_detected\":%s,"
           "\"temperature_c\":%.2f,\"humidity_percent\":%.2f,\"flood_alert\":%s,"
           "\"alarm_active\":%s,\"wifi_rssi\":%ld}",
           data.validDistance ? data.distanceCm : -1.0f,
           data.waterLevelPercent,
           data.rainDetected ? "true" : "false",
           temperature,
           humidity,
           floodAlertLatched ? "true" : "false",
           alarmActive ? "true" : "false",
           WiFi.isConnected() ? WiFi.RSSI() : 0L);

  publishMqttMessage(MQTT_TELEMETRY_TOPIC, payload, false);
  publishMqttMessage(MQTT_DEBUG_TOPIC, payload, false);

  const char *status = alarmActive ? "ALERT" : "NORMAL";
  publishMqttMessage(MQTT_STATUS_TOPIC, status, true);
}

static void publishLegacyTelemetry() {
  if (data.validDistance) {
    const String distancePayload = String(data.distanceCm, 2);
    publishMqttMessage(MQTT_LEGACY_WATER_TOPIC, distancePayload.c_str(), false);
  }

  publishMqttMessage(MQTT_LEGACY_RAIN_TOPIC, data.rainDetected ? "1" : "0", false);

  if (data.validDht) {
    const String tempPayload = String(data.temperatureC, 2);
    const String humPayload = String(data.humidityPercent, 2);
    publishMqttMessage(MQTT_LEGACY_TEMP_TOPIC, tempPayload.c_str(), false);
    publishMqttMessage(MQTT_LEGACY_HUM_TOPIC, humPayload.c_str(), false);
  }
}

static void drawCenteredLine(int y, const char *text, int textSize = 1) {
  display.setTextSize(textSize);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  const int x = (OLED_WIDTH - static_cast<int>(w)) / 2;
  display.setCursor(x < 0 ? 0 : x, y);
  display.print(text);
}

static void updateOled() {
  if (!displayReady) {
    return;
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("ESP32 Flood Monitor");

  display.drawLine(0, 10, OLED_WIDTH - 1, 10, SSD1306_WHITE);

  display.setCursor(0, 14);
  display.print("Dist: ");
  if (data.validDistance) {
    display.print(data.distanceCm, 1);
    display.print(" cm");
  } else {
    display.print("ERR");
  }

  display.setCursor(0, 24);
  display.print("Water: ");
  if (data.waterLevelPercent >= 0.0f) {
    display.print(data.waterLevelPercent, 0);
    display.print("%");
  } else {
    display.print("ERR");
  }

  display.setCursor(0, 34);
  display.print("Rain: ");
  display.print(data.rainDetected ? "DETECTED" : "NO");

  display.setCursor(0, 44);
  display.print("Temp: ");
  if (data.validDht) {
    display.print(data.temperatureC, 1);
    display.print((char)247);
    display.print("C");
  } else {
    display.print("ERR");
  }

  display.setCursor(0, 54);
  display.print("Hum: ");
  if (data.validDht) {
    display.print(data.humidityPercent, 0);
    display.print("% ");
  } else {
    display.print("ERR ");
  }

  updateFloodAlertLatch();
  const bool warning = isAlarmActive();

  if (warning) {
    display.setCursor(84, 44);
    display.print("ALERT");
  } else {
    display.setCursor(84, 44);
    display.print("OK");
  }

  display.display();
}

static void printSerialStatus() {
  Serial.print("Distance: ");
  if (data.validDistance) {
    Serial.print(data.distanceCm, 2);
    Serial.print(" cm");
  } else {
    Serial.print("ERR");
  }

  Serial.print(" | Water: ");
  if (data.waterLevelPercent >= 0.0f) {
    Serial.print(data.waterLevelPercent, 1);
    Serial.print("%");
  } else {
    Serial.print("ERR");
  }

  Serial.print(" | Rain: ");
  Serial.print(data.rainDetected ? "YES" : "NO");

  Serial.print(" | Temp: ");
  if (data.validDht) {
    Serial.print(data.temperatureC, 1);
    Serial.print("C");
  } else {
    Serial.print("ERR");
  }

  Serial.print(" | Hum: ");
  if (data.validDht) {
    Serial.print(data.humidityPercent, 0);
    Serial.print("%");
  } else {
    Serial.print("ERR");
  }

  Serial.println();

  lastPrintedData = data;
  hasPrintedOnce = true;
}

void setup() {
  pinMode(JSN_TRIG_PIN, OUTPUT);
  pinMode(JSN_ECHO_PIN, INPUT);
  pinMode(RAIN_DO_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(JSN_TRIG_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(115200);
  delay(300);

  connectWifi();
  initMqttClientId();
  mqttClient.setBufferSize(512);
  connectMqtt();

  Wire.begin(21, 22);
  dht.begin();

  displayReady = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (displayReady) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    drawCenteredLine(18, "ESP32 Flood Monitor", 1);
    drawCenteredLine(34, "Starting...", 1);
    display.display();
  } else {
    Serial.println("SSD1306 init failed, continuing without display");
  }

  Serial.println();
  Serial.println("ESP32 Flood Monitoring System Started");
  delay(1000);
}

void loop() {
  const uint32_t now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    connectWifi();
  }

  mqttClient.loop();

  if (now - lastSensorReadMs >= SENSOR_INTERVAL_MS) {
    lastSensorReadMs = now;
    readSensors();
    updateFloodAlertLatch();

    if (!mqttClient.connected()) {
      if (now - lastMqttRetryMs >= MQTT_RETRY_MS) {
        lastMqttRetryMs = now;
        connectMqtt();
      }
    }

    if (mqttClient.connected()) {
      publishTelemetry();
      publishLegacyTelemetry();
    }
  }

  const bool warning = isAlarmActive();

  digitalWrite(LED_PIN, warning ? HIGH : LOW);
  updateBuzzer(warning);

  if (now - lastSerialPrintMs >= SERIAL_INTERVAL_MS && shouldPrintSerialStatus()) {
    lastSerialPrintMs = now;
    printSerialStatus();
  } else if (now - lastSerialHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
    lastSerialHeartbeatMs = now;
    Serial.println("Heartbeat: system running");
  }

  if (now - lastOledUpdateMs >= OLED_INTERVAL_MS) {
    lastOledUpdateMs = now;
    updateOled();
  }
}
