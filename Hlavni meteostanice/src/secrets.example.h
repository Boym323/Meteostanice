#pragma once

// Copy this file to secrets.h and replace CHANGE_ME values.
// secrets.h is intentionally ignored by Git.

#define WIFI_SSID "CHANGE_ME"
#define WIFI_PASSWORD "CHANGE_ME"

#define MQTT_HOST "192.168.1.2"
#define MQTT_PORT 1883
#define MQTT_USERNAME "CHANGE_ME"
#define MQTT_PASSWORD "CHANGE_ME"
#define MQTT_TOPIC "meteostanice/zahrada"

#define OTA_USERNAME "admin"
#define OTA_PASSWORD "CHANGE_ME"

#define NTP_SERVER "192.168.1.1"

#define METEOTEMPLATE_HOST "pocasi-loucka.cz"
#define METEOTEMPLATE_API_PASSWORD "CHANGE_ME"

// SHA-1 certificate fingerprint accepted by BearSSL, for example:
// "AA BB CC DD EE FF 00 11 22 33 44 55 66 77 88 99 AA BB CC DD"
// Keep this current when the server certificate changes.
#define METEOTEMPLATE_TLS_FINGERPRINT "CHANGE_ME"
