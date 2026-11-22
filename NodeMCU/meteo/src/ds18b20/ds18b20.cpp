#include "../config.h"

#if METEO_ENABLE_DS18B20
#include "ds18b20.h"

#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <cmath>

namespace ds18b20 {
namespace {
OneWire bus(D2);
DallasTemperature sensor(&bus);
}  // namespace

void begin() {
  sensor.begin();
}

float readTemperature() {
  if (sensor.getDeviceCount() == 0) {
    sensor.begin();  // Повторяем поиск, если при запуске датчик не ответил.
  }

  if (sensor.getDeviceCount() != 0) {
    sensor.requestTemperatures();
    const float temperature = sensor.getTempCByIndex(0);
    if (temperature != DEVICE_DISCONNECTED_C) {
      Serial.print(F("DS18B20 temperature (C): "));
      Serial.println(temperature);
      return temperature;
    }
  }

  Serial.println(F("DS18B20 read error"));
  return NAN;
}

}  // namespace ds18b20
#endif  // METEO_ENABLE_DS18B20
