#include <Arduino.h>
#include <ArduinoJson.h>
#include <DallasTemperature.h>
#include <EasyNTPClient.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <OneWire.h>
#include <PubSubClient.h>
#include <SFE_BMP180.h>
#include <WiFiClientSecureBearSSL.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include "Adafruit_Si7021.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

namespace {
constexpr uint32_t SEND_INTERVAL_MS = 30000UL;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 10000UL;
constexpr uint32_t WIFI_INITIAL_CONNECT_TIMEOUT_MS = 15000UL;
constexpr uint16_t HTTP_TIMEOUT_MS = 5000;
constexpr double STATION_ALTITUDE_M = 425.0;
constexpr float MIN_VALID_TEMPERATURE_C = -50.0F;
constexpr float MAX_VALID_TEMPERATURE_C = 70.0F;
constexpr unsigned long MIN_VALID_UNIX_TIME = 1700000000UL;

const char *MDNS_HOST = "mainstation";

IPAddress staticIP(192, 168, 1, 31);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(192, 168, 1, 1);

constexpr uint8_t I2C_SCL = 12;
constexpr uint8_t I2C_SDA = 13;
constexpr uint8_t ONE_WIRE_PIN = 4;

DeviceAddress sensor100cm = {0x28, 0xAB, 0xA0, 0x77, 0x91, 0x11, 0x02, 0xB0};
DeviceAddress sensor50cm = {0x28, 0x89, 0x92, 0x77, 0x91, 0x11, 0x02, 0x6F};
DeviceAddress sensor20cm = {0x28, 0xA7, 0xE8, 0x77, 0x91, 0x09, 0x02, 0x80};
DeviceAddress sensor10cm = {0x28, 0xB9, 0x77, 0x77, 0x91, 0x14, 0x02, 0x75};
DeviceAddress sensor5cm = {0x28, 0xF4, 0xD0, 0x77, 0x91, 0x09, 0x02, 0x4D};
DeviceAddress sensorGround5cm = {0x28, 0x30, 0xA4, 0x45, 0x92, 0x07, 0x02, 0x0B};
DeviceAddress sensor200cm = {0x28, 0x32, 0xD9, 0x35, 0x05, 0x00, 0x00, 0xCC};

struct Reading {
  float value;
  uint32_t lastValidMillis;
  bool valid;
};

Reading temp100cm = {NAN, 0, false};
Reading temp50cm = {NAN, 0, false};
Reading temp20cm = {NAN, 0, false};
Reading temp10cm = {NAN, 0, false};
Reading temp5cm = {NAN, 0, false};
Reading tempGround5cm = {NAN, 0, false};
Reading temp200cm = {NAN, 0, false};
Reading humidity = {NAN, 0, false};
Reading seaLevelPressure = {NAN, 0, false};

OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature ds18b20(&oneWire);
Adafruit_Si7021 si7021;
SFE_BMP180 bmp180;

WiFiUDP udp;
EasyNTPClient ntpClient(udp, NTP_SERVER);
WiFiClient mqttTransport;
PubSubClient mqttClient(mqttTransport);

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

bool si7021Ready = false;
bool bmp180Ready = false;
bool mdnsStarted = false;
uint32_t lastSendMillis = 0;
uint32_t lastWiFiAttemptMillis = 0;

bool isPlaceholder(const char *value) {
  return value == nullptr || value[0] == '\0' || strcmp(value, "CHANGE_ME") == 0;
}

bool temperatureIsValid(float value) {
  return !isnan(value) && value != DEVICE_DISCONNECTED_C &&
         value >= MIN_VALID_TEMPERATURE_C && value <= MAX_VALID_TEMPERATURE_C;
}

void updateReading(Reading &reading, float value, bool valid) {
  reading.valid = valid;
  if (valid) {
    reading.value = value;
    reading.lastValidMillis = millis();
  }
}

void configureDs18b20() {
  ds18b20.begin();
  ds18b20.setResolution(sensor100cm, 10);
  ds18b20.setResolution(sensor50cm, 10);
  ds18b20.setResolution(sensor20cm, 10);
  ds18b20.setResolution(sensor10cm, 10);
  ds18b20.setResolution(sensor5cm, 10);
  ds18b20.setResolution(sensorGround5cm, 10);
  ds18b20.setResolution(sensor200cm, 10);
}

void readDsTemperature(Reading &reading, const uint8_t *address) {
  const float value = ds18b20.getTempC(address);
  updateReading(reading, value, temperatureIsValid(value));
}

void readTemperatures() {
  // A single conversion command updates all DS18B20 devices in parallel.
  ds18b20.requestTemperatures();

  readDsTemperature(temp100cm, sensor100cm);
  readDsTemperature(temp50cm, sensor50cm);
  readDsTemperature(temp20cm, sensor20cm);
  readDsTemperature(temp10cm, sensor10cm);
  readDsTemperature(temp5cm, sensor5cm);
  readDsTemperature(tempGround5cm, sensorGround5cm);
  readDsTemperature(temp200cm, sensor200cm);
}

void readHumidity() {
  if (!si7021Ready) {
    si7021Ready = si7021.begin();
  }

  if (!si7021Ready) {
    updateReading(humidity, NAN, false);
    Serial.println(F("Si7021 unavailable"));
    return;
  }

  const float value = si7021.readHumidity();
  updateReading(humidity, value, !isnan(value) && value >= 0.0F && value <= 100.0F);
}

void readPressure() {
  if (!bmp180Ready) {
    bmp180Ready = bmp180.begin();
  }

  if (!bmp180Ready) {
    updateReading(seaLevelPressure, NAN, false);
    Serial.println(F("BMP180 unavailable"));
    return;
  }

  double temperature = NAN;
  double pressure = NAN;

  char status = bmp180.startTemperature();
  if (!status) {
    updateReading(seaLevelPressure, NAN, false);
    return;
  }
  delay(status);

  if (!bmp180.getTemperature(temperature)) {
    updateReading(seaLevelPressure, NAN, false);
    return;
  }

  status = bmp180.startPressure(3);
  if (!status) {
    updateReading(seaLevelPressure, NAN, false);
    return;
  }
  delay(status);

  if (!bmp180.getPressure(pressure, temperature)) {
    updateReading(seaLevelPressure, NAN, false);
    return;
  }

  const float correctedPressure =
      static_cast<float>(bmp180.sealevel(pressure, STATION_ALTITUDE_M));
  updateReading(seaLevelPressure, correctedPressure,
                !isnan(correctedPressure) &&
                    correctedPressure >= 850.0F &&
                    correctedPressure <= 1100.0F);
}

void sampleSensors() {
  readTemperatures();
  readHumidity();
  readPressure();
  Serial.println(F("Sensor sampling complete"));
}

void startMdnsIfNeeded() {
  if (WiFi.status() != WL_CONNECTED || mdnsStarted) {
    return;
  }

  mdnsStarted = MDNS.begin(MDNS_HOST);
  if (mdnsStarted) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("mDNS ready: http://%s.local/\n", MDNS_HOST);
  } else {
    Serial.println(F("mDNS initialization failed"));
  }
}

void beginWiFi() {
  if (isPlaceholder(WIFI_SSID) || isPlaceholder(WIFI_PASSWORD)) {
    Serial.println(F("WiFi disabled: configure src/secrets.h first"));
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.config(staticIP, gateway, subnet, dns);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWiFiAttemptMillis = millis();
}

void connectWiFiInitially() {
  beginWiFi();
  if (isPlaceholder(WIFI_SSID) || isPlaceholder(WIFI_PASSWORD)) {
    return;
  }

  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED &&
         static_cast<uint32_t>(millis() - started) <
             WIFI_INITIAL_CONNECT_TIMEOUT_MS) {
    delay(250);
    yield();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("WiFi connected, IP: "));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(
        F("WiFi initial connection timed out; retrying in background"));
  }
}

void serviceWiFi() {
  if (WiFi.status() == WL_CONNECTED ||
      isPlaceholder(WIFI_SSID) ||
      isPlaceholder(WIFI_PASSWORD)) {
    return;
  }

  if (static_cast<uint32_t>(millis() - lastWiFiAttemptMillis) <
      WIFI_RETRY_INTERVAL_MS) {
    return;
  }

  Serial.println(F("Retrying WiFi connection"));
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWiFiAttemptMillis = millis();
}

void configureOta() {
  if (isPlaceholder(OTA_USERNAME) || isPlaceholder(OTA_PASSWORD)) {
    Serial.println(
        F("OTA disabled: configure OTA credentials in src/secrets.h"));
    return;
  }

  httpUpdater.setup(&httpServer, OTA_USERNAME, OTA_PASSWORD);
  Serial.printf(
      "Authenticated OTA endpoint enabled at /update for user '%s'\n",
      OTA_USERNAME);
}

void appendReading(String &url,
                   const __FlashStringHelper *parameter,
                   const Reading &reading) {
  if (!reading.valid) {
    return;
  }

  url += parameter;
  url += String(reading.value, 2);
}

unsigned long getValidUnixTime() {
  const unsigned long timestamp = ntpClient.getUnixTime();
  if (timestamp < MIN_VALID_UNIX_TIME) {
    Serial.println(F("NTP time invalid; HTTPS upload skipped"));
    return 0;
  }
  return timestamp;
}

void publishMeteotemplate() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("Meteotemplate upload skipped: WiFi offline"));
    return;
  }

  if (isPlaceholder(METEOTEMPLATE_API_PASSWORD)) {
    Serial.println(
        F("Meteotemplate upload disabled: API password not configured"));
    return;
  }

  if (isPlaceholder(METEOTEMPLATE_TLS_FINGERPRINT)) {
    Serial.println(
        F("Meteotemplate upload disabled: TLS fingerprint not configured"));
    return;
  }

  const unsigned long timestamp = getValidUnixTime();
  if (timestamp == 0) {
    return;
  }

  String url;
  url.reserve(384);
  url = F("https://");
  url += METEOTEMPLATE_HOST;
  url += F("/api.php?U=");
  url += String(timestamp);

  appendReading(url, F("&T="), temp200cm);
  appendReading(url, F("&H="), humidity);
  appendReading(url, F("&P="), seaLevelPressure);
  appendReading(url, F("&T1="), tempGround5cm);
  appendReading(url, F("&TS1="), temp5cm);
  if (temp5cm.valid) {
    url += F("&TSD1=5");
  }
  appendReading(url, F("&TS2="), temp10cm);
  if (temp10cm.valid) {
    url += F("&TSD2=10");
  }
  appendReading(url, F("&TS3="), temp20cm);
  if (temp20cm.valid) {
    url += F("&TSD3=20");
  }
  appendReading(url, F("&TS4="), temp50cm);
  if (temp50cm.valid) {
    url += F("&TSD4=50");
  }
  appendReading(url, F("&TS5="), temp100cm);
  if (temp100cm.valid) {
    url += F("&TSD5=100");
  }

  url += F("&PASS=");
  url += METEOTEMPLATE_API_PASSWORD;

  BearSSL::WiFiClientSecure secureClient;
  if (!secureClient.setFingerprint(METEOTEMPLATE_TLS_FINGERPRINT)) {
    Serial.println(F("Invalid Meteotemplate TLS fingerprint format"));
    return;
  }
  secureClient.setTimeout(HTTP_TIMEOUT_MS);

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(secureClient, url)) {
    Serial.println(F("Meteotemplate HTTPS initialization failed"));
    return;
  }

  const int httpCode = http.GET();
  if (httpCode >= 200 && httpCode < 300) {
    Serial.printf("Meteotemplate upload OK (%d)\n", httpCode);
  } else {
    Serial.printf("Meteotemplate upload failed (%d): %s\n",
                  httpCode,
                  HTTPClient::errorToString(httpCode).c_str());
  }
  http.end();
}

bool ensureMqttConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  if (mqttClient.connected()) {
    return true;
  }

  if (isPlaceholder(MQTT_USERNAME) || isPlaceholder(MQTT_PASSWORD)) {
    Serial.println(F("MQTT disabled: credentials not configured"));
    return false;
  }

  char clientId[32];
  snprintf(clientId,
           sizeof(clientId),
           "meteostanice-%06X",
           ESP.getChipId());

  if (!mqttClient.connect(clientId, MQTT_USERNAME, MQTT_PASSWORD)) {
    Serial.printf("MQTT connection failed, state=%d\n", mqttClient.state());
    return false;
  }

  Serial.println(F("MQTT connected"));
  return true;
}

void setJsonReading(JsonDocument &doc,
                    const char *key,
                    const Reading &reading) {
  if (reading.valid) {
    doc[key] = reading.value;
  } else {
    doc[key] = nullptr;
  }
}

void publishMqtt() {
  if (!ensureMqttConnected()) {
    return;
  }

  StaticJsonDocument<512> doc;
  setJsonReading(doc, "outTemp", temp200cm);
  setJsonReading(doc, "outHumidity", humidity);
  setJsonReading(doc, "barometer", seaLevelPressure);
  setJsonReading(doc, "extraTemp1", tempGround5cm);
  setJsonReading(doc, "extraTemp2", temp5cm);
  setJsonReading(doc, "soilTemp1", temp10cm);
  setJsonReading(doc, "soilTemp2", temp20cm);
  setJsonReading(doc, "soilTemp3", temp50cm);
  setJsonReading(doc, "soilTemp4", temp100cm);
  doc["signal1"] = WiFi.RSSI();
  doc["uptimeMs"] = millis();

  JsonObject valid = doc.createNestedObject("valid");
  valid["outTemp"] = temp200cm.valid;
  valid["outHumidity"] = humidity.valid;
  valid["barometer"] = seaLevelPressure.valid;
  valid["extraTemp1"] = tempGround5cm.valid;
  valid["extraTemp2"] = temp5cm.valid;
  valid["soilTemp1"] = temp10cm.valid;
  valid["soilTemp2"] = temp20cm.valid;
  valid["soilTemp3"] = temp50cm.valid;
  valid["soilTemp4"] = temp100cm.valid;

  char buffer[512];
  const size_t written = serializeJson(doc, buffer, sizeof(buffer));
  if (doc.overflowed() || written == 0 || written >= sizeof(buffer)) {
    Serial.println(F("MQTT JSON serialization failed"));
    return;
  }

  if (mqttClient.publish(MQTT_TOPIC, buffer)) {
    Serial.println(F("MQTT publish OK"));
  } else {
    Serial.printf("MQTT publish failed, state=%d\n", mqttClient.state());
  }
}

void publishData() {
  publishMeteotemplate();
  publishMqtt();
}
}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println(F("Meteostanice starting"));

  Wire.begin(I2C_SDA, I2C_SCL);
  configureDs18b20();

  si7021Ready = si7021.begin();
  if (!si7021Ready) {
    Serial.println(F("Si7021 not detected during startup"));
  }

  bmp180Ready = bmp180.begin();
  if (!bmp180Ready) {
    Serial.println(F("BMP180 not detected during startup"));
  }

  connectWiFiInitially();

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setBufferSize(512);

  configureOta();
  httpServer.begin();
  startMdnsIfNeeded();

  // Trigger the first sampling cycle immediately without breaking millis()
  // rollover safety.
  lastSendMillis = millis() - SEND_INTERVAL_MS;
}

void loop() {
  serviceWiFi();
  startMdnsIfNeeded();

  httpServer.handleClient();
  if (mdnsStarted && WiFi.status() == WL_CONNECTED) {
    MDNS.update();
  }

  if (mqttClient.connected()) {
    mqttClient.loop();
  }

  const uint32_t now = millis();
  if (static_cast<uint32_t>(now - lastSendMillis) >= SEND_INTERVAL_MS) {
    lastSendMillis = now;
    sampleSensors();
    publishData();
  }

  yield();
}
