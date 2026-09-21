# Changelog

## [2.0.0] - 2026-09-21

### Security

- Removed committed Wi-Fi, MQTT and Meteotemplate credentials from firmware source.
- Added ignored local `src/secrets.h` configuration with a safe tracked template.
- Added username/password authentication to the HTTP OTA updater.
- Changed Meteotemplate transport from plaintext HTTP to HTTPS.
- Added explicit TLS certificate fingerprint verification; insecure TLS fallback is not used.
- Network integrations stay disabled when placeholder credentials are present.

### Fixed

- Added missing Si7021 initialization and validation.
- Added BMP180 initialization and return-value checks for every measurement step.
- Invalid DS18B20 readings no longer silently become fresh measurements.
- Invalid NTP time is rejected instead of sending a timestamp derived from device uptime.
- Replaced rollover-unsafe `millis() > last + interval` scheduling with subtraction-based timing.
- Added finite initial Wi-Fi connection timeout and background reconnect attempts.
- MQTT connection failures now report the PubSubClient state.
- MQTT client ID is unique per ESP8266 instead of the fixed `ESP32Client` identifier.
- Added bounded HTTP timeouts and HTTP status/error handling.

### Changed

- All DS18B20 sensors are converted in one bus-wide request instead of seven sequential conversions.
- DS18B20 resolution is configured once during startup rather than every reporting cycle.
- MQTT payload now includes per-reading validity flags and device uptime.
- Replaced dynamic ArduinoJson allocation with a fixed-size document.
- PlatformIO environment is named after the actual `d1_mini_pro` board.
- Added declared PlatformIO dependencies and removed the developer-specific upload port.
- Added CI firmware build for pull requests and pushes to `master`.
- Removed committed macOS `.DS_Store` files.
