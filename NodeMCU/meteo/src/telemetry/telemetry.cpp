#include "../config.h"

#if METEO_ENABLE_WIFI
#include "telemetry.h"

#include <Arduino.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <cmath>
#include <cstdio>

#if !__has_include("meteo_secrets.h")
#error "Copy src/telemetry/meteo_secrets.h.example to meteo_secrets.h and fill in the settings"
#endif
#include "meteo_secrets.h"

namespace telemetry {
namespace {
constexpr uint32_t sampleIntervalMs = 30 * 1000;
constexpr uint32_t retryIntervalMs = 5 * 1000;
constexpr uint16_t requestTimeoutMs = 700;
constexpr char station[] = METEO_STATION_ID;

constexpr bool validStationId() {
  if (sizeof(station) < 2 || sizeof(station) > 33) {
    return false;
  }
  for (size_t i = 0; i + 1 < sizeof(station); ++i) {
    const char c = station[i];
    if (!(c >= 'A' && c <= 'Z') && !(c >= 'a' && c <= 'z') &&
        !(c >= '0' && c <= '9') && c != '_' && c != '-') {
      return false;
    }
  }
  return true;
}

static_assert(validStationId(), "METEO_STATION_ID must be 1-32 ASCII letters, digits, underscores or hyphens");
static_assert(sizeof(METEO_TOKEN) >= 17, "METEO_TOKEN must be at least 16 characters");

char bootId[17];
char payload[320];
uint32_t sequence = 0;
uint32_t lastSampleAt = 0;
uint32_t lastAttemptAt = 0;
bool pending = false;

void formatMetric(char (&output)[20], float value, int decimals) {
  if (std::isfinite(value)) {
    std::snprintf(output, sizeof(output), "%.*f", decimals, value);
  } else {
    std::snprintf(output, sizeof(output), "null");
  }
}

bool prepareSample(const Reading& reading) {
  char bme[20], ds[20], humidity[20], pressure[20], co2[12];
  formatMetric(bme, reading.temperatureBme, 2);
  formatMetric(ds, reading.temperatureDs, 2);
  formatMetric(humidity, reading.humidity, 2);
  formatMetric(pressure, reading.pressureMmHg, 1);
  if (reading.co2Valid) {
    std::snprintf(co2, sizeof(co2), "%d", reading.co2Ppm);
  } else {
    std::snprintf(co2, sizeof(co2), "null");
  }
  const int size = std::snprintf(
      payload, sizeof(payload),
      "{\"station\":\"%s\",\"boot\":\"%s\",\"seq\":%lu,"
      "\"temp_bme_c\":%s,\"temp_ds_c\":%s,\"humidity_pct\":%s,"
      "\"pressure_mmhg\":%s,\"co2_ppm\":%s}",
      station, bootId, static_cast<unsigned long>(sequence), bme, ds, humidity, pressure, co2);
  return size > 0 && size < static_cast<int>(sizeof(payload));
}

int sendSample() {
  WiFiClient connection;
  HTTPClient request;
  if (!request.begin(connection, METEO_ROUTER_URL)) {
    return -1;
  }
  request.setTimeout(requestTimeoutMs);
  request.setReuse(false);
  request.addHeader(F("Authorization"), F("Bearer " METEO_TOKEN));
  request.addHeader(F("Content-Type"), F("application/json"));
  const int status = request.POST(payload);
  request.end();
  return status;
}
}  // namespace

void begin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(METEO_WIFI_SSID, METEO_WIFI_PASSWORD);

  // Идентификатор загрузки вместе с номером отсчёта позволяет распознать повторную отправку.
  const unsigned long high = ESP.random();
  const unsigned long low = ESP.random();
  std::snprintf(bootId, sizeof(bootId), "%08lx%08lx", high, (high == 0 && low == 0) ? 1UL : low);
  lastAttemptAt = millis() - retryIntervalMs;
}

void publish(const Reading& reading) {
  const uint32_t now = millis();
  if (sequence == 0 || now - lastSampleAt >= sampleIntervalMs) {
    lastSampleAt = now;
    ++sequence;
    pending = prepareSample(reading);
  }
  // При сбое Wi-Fi держим только последний отсчёт. Десятидневный буфер находится на роутере.
  if (!pending || WiFi.status() != WL_CONNECTED || now - lastAttemptAt < retryIntervalMs) {
    return;
  }

  lastAttemptAt = now;
  const int status = sendSample();
  if (status == HTTP_CODE_NO_CONTENT) {
    pending = false;
  } else {
    Serial.print(F("Telemetry POST: "));
    Serial.println(status);
  }
}

}  // namespace telemetry
#endif  // METEO_ENABLE_WIFI
