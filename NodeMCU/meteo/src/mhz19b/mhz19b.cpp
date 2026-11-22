#include "../config.h"

#if METEO_ENABLE_MHZ19B
#include "mhz19b.h"

#include <Arduino.h>
#include <MHZ19.h>
#include <SoftwareSerial.h>

namespace mhz19b {
namespace {
constexpr uint32_t warmupMs = 3 * 60 * 1000UL;
SoftwareSerial co2Serial;
MHZ19 sensor;
uint32_t startedAt = 0;
bool warmedUp = false;
}  // namespace

void begin() {
  co2Serial.begin(9600, SWSERIAL_8N1, D6, D0);  // RX, TX NodeMCU
  startedAt = millis();
  warmedUp = false;
  Serial.println(F("MH-Z19B warming up (3 min)"));
}

Reading read() {
  // Прогрев отсчитываем без delay, поэтому в это время BME280 и дисплей продолжают работать.
  if (!warmedUp) {
    if (millis() - startedAt < warmupMs) {
      return {0, false};
    }
    // Библиотека проверяет связь при запуске. Первый обмен начинаем после прогрева.
    sensor.begin(co2Serial);
    warmedUp = true;
  }

  const int ppm = sensor.getCO2(false);  // MH-Z19B UART command 0x86
  const auto error = sensor.errorCode;
  // Проверяем статус сразу после запроса: он относится именно к этому ответу.
  if (error != RESULT_OK || ppm <= 0) {
    Serial.print(F("MH-Z19B invalid reading, status: "));
    Serial.print(static_cast<int>(error));
    Serial.print(F(", ppm: "));
    Serial.println(ppm);
    return {ppm, false};
  }

  Serial.print(F("CO2 (ppm): "));
  Serial.println(ppm);
  return {ppm, true};
}

}  // namespace mhz19b
#endif  // METEO_ENABLE_MHZ19B
