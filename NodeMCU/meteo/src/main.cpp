#include <Arduino.h>
#include <cmath>

#include "bme280/bme280.h"
#include "config.h"
#include "display/display.h"
#include "ds18b20/ds18b20.h"
#include "mhz19b/mhz19b.h"
#include "telemetry/telemetry.h"

void setup() {
  Serial.begin(9600);
  Serial.println("Start Serial Monitor");

#if METEO_ENABLE_DISPLAY
  // SPI назначает D6 входом MISO. Поэтому программный UART на D6 запускаем после дисплея.
  initDisplay();
#endif
#if METEO_ENABLE_MHZ19B
  mhz19b::begin();
#endif
#if METEO_ENABLE_BME280
  bme280::begin();
#endif
#if METEO_ENABLE_DS18B20
  ds18b20::begin();
#endif
#if METEO_ENABLE_WIFI
  telemetry::begin();
#endif
}

void loop() {
#if METEO_ENABLE_MHZ19B
  [[maybe_unused]] const auto co2 = mhz19b::read();
#elif METEO_ENABLE_DISPLAY || METEO_ENABLE_WIFI
  const mhz19b::Reading co2{0, false};
#endif
#if METEO_ENABLE_BME280
  [[maybe_unused]] const auto weather = bme280::read();
#elif METEO_ENABLE_DISPLAY || METEO_ENABLE_WIFI
  const bme280::Reading weather{NAN, NAN, NAN};
#endif
#if METEO_ENABLE_DS18B20
  [[maybe_unused]] const float dsTemperature = ds18b20::readTemperature();
#elif METEO_ENABLE_WIFI
  const float dsTemperature = NAN;
#endif
  Serial.println();

#if METEO_ENABLE_DISPLAY
  float temperature = weather.temperature;
#if METEO_ENABLE_DS18B20
  // Если BME280 недоступен, используем температуру DS18B20.
  if (!std::isfinite(temperature)) {
    temperature = dsTemperature;
  }
#endif

  // Для CO₂ доступность передаём флагом valid, для остальных показаний используем NaN.
  updateDisplay(co2.ppm, co2.valid,
                temperature, weather.humidity, weather.pressureMmHg);
#endif
#if METEO_ENABLE_WIFI
  telemetry::publish({weather.temperature, dsTemperature, weather.humidity,
                      weather.pressureMmHg, co2.ppm, co2.valid});
#endif
  delay(3000);
}
