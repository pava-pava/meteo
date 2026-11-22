#include "../config.h"

#if METEO_ENABLE_BME280
#include "bme280.h"

#include <Arduino.h>
#include <Wire.h>
#include <bosch/bme280.h>
#include <cmath>

namespace bme280 {
namespace {
constexpr uint8_t address = BME280_I2C_ADDR_PRIM;
constexpr uint32_t retryIntervalMs = 6000;
constexpr double pascalToMmHg = 0.00750062;
constexpr Reading unavailable{NAN, NAN, NAN};
bme280_dev sensor{};
bool ready = false;
uint32_t lastAttempt = 0;

int8_t readRegisters(uint8_t reg, uint8_t* data, uint32_t length, void*) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  // Читаем с повторным START после указателя регистра. Неполный ответ считаем ошибкой I²C.
  if (Wire.endTransmission(false) != 0 ||
      Wire.requestFrom(address, static_cast<size_t>(length), true) != length) {
    return -1;
  }
  for (uint32_t index = 0; index < length; ++index) {
    data[index] = static_cast<uint8_t>(Wire.read());
  }
  return BME280_INTF_RET_SUCCESS;
}

int8_t writeRegisters(uint8_t reg, const uint8_t* data, uint32_t length, void*) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  // endTransmission завершает передачу и сообщает, подтвердил ли датчик запись.
  const size_t written = Wire.write(data, length);
  const uint8_t result = Wire.endTransmission();
  return written == length && result == 0 ? BME280_INTF_RET_SUCCESS : -1;
}

bool startSensor() {
  lastAttempt = millis();
  const bme280_settings settings{
      BME280_OVERSAMPLING_16X, BME280_OVERSAMPLING_16X, BME280_OVERSAMPLING_16X,
      BME280_FILTER_COEFF_OFF, BME280_STANDBY_TIME_0_5_MS,
  };
  uint32_t firstSampleUs = 0;
  // Bosch SensorAPI проверяет ID, сбрасывает датчик и ограничивает ожидание калибровки.
  int8_t result = bme280_init(&sensor);
  if (result == BME280_OK) {
    result = bme280_set_sensor_settings(BME280_SEL_ALL_SETTINGS, &settings, &sensor);
  }
  if (result == BME280_OK) {
    result = bme280_cal_meas_delay(&firstSampleUs, &settings);
  }
  if (result == BME280_OK) {
    result = bme280_set_sensor_mode(BME280_POWERMODE_NORMAL, &sensor);
  }
  if (result != BME280_OK) {
#if METEO_ENABLE_SERIAL_LOG
    Serial.print(F("BME280 init error: "));
    Serial.println(static_cast<int>(result));
#endif
    return false;
  }

  // Первый отсчёт читаем после полного цикла измерения. Время округляем вверх до миллисекунд.
  delay((firstSampleUs + 999) / 1000);
  return true;
}
}  // namespace

void begin() {
  Wire.begin(D3, D4);  // SDA, SCL NodeMCU
  sensor.intf = BME280_I2C_INTF;
  sensor.read = readRegisters;
  sensor.write = writeRegisters;
  sensor.delay_us = [](uint32_t period, void*) { delayMicroseconds(period); };
  ready = startSensor();
}

Reading read() {
  // Пока датчик недоступен, повторяем запуск с паузой. Экран и опрос CO₂ продолжают работать.
  if (!ready && millis() - lastAttempt >= retryIntervalMs) {
    ready = startSensor();
  }
  if (!ready) {
    return unavailable;
  }

  // A sensor-only reset leaves the BME280 in sleep mode.
  uint8_t mode = BME280_POWERMODE_SLEEP;
  bme280_data data{};
  int8_t result = bme280_get_sensor_mode(&mode, &sensor);
  if (result == BME280_OK && mode == BME280_POWERMODE_NORMAL) {
    // Температуру, давление и влажность читаем одним блоком, чтобы получить один отсчёт.
    result = bme280_get_sensor_data(BME280_ALL, &data, &sensor);
  }
  if (result != BME280_OK || mode != BME280_POWERMODE_NORMAL) {
#if METEO_ENABLE_SERIAL_LOG
    if (result != BME280_OK) {
      Serial.print(F("BME280 read error: "));
      Serial.println(static_cast<int>(result));
    } else {
      Serial.println(F("BME280 left normal mode"));
    }
#endif
    // После сбоя следующее успешное подключение заново загрузит настройки и калибровку.
    ready = false;
    lastAttempt = millis();
    return unavailable;
  }

  const Reading reading{
      static_cast<float>(data.temperature),
      static_cast<float>(data.pressure * pascalToMmHg),
      static_cast<float>(data.humidity),
  };
#if METEO_ENABLE_SERIAL_LOG
  Serial.print(F("BME280 temperature (C): "));
  Serial.println(reading.temperature);
  Serial.print(F("BME280 pressure (mmHg): "));
  Serial.println(reading.pressureMmHg);
  Serial.print(F("BME280 humidity (%): "));
  Serial.println(reading.humidity);
#endif
  return reading;
}

}  // namespace bme280
#endif  // METEO_ENABLE_BME280
