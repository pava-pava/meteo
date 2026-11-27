#include <config.h>

#if METEO_ENABLE_MHZ19B && !METEO_MHZ19B_USE_PWM
#include "mhz19b.h"

#include <Arduino.h>
#include <limits.h>
#include <SoftwareSerial.h>

namespace mhz19b {
namespace {
// constexpr uint32_t warmupMs = 0;
constexpr uint32_t warmupMs = 1 * 60 * 1000UL;
constexpr uint32_t responseTimeoutMs = 500;
constexpr uint8_t readCommand[] = {0xFF, 0x01, 0x86, 0, 0, 0, 0, 0, 0x79};

static_assert(METEO_MHZ19B_ABC == -1 || METEO_MHZ19B_ABC == 0 || METEO_MHZ19B_ABC == 1,
              "METEO_MHZ19B_ABC must be -1, 0 or 1");
#if METEO_MHZ19B_ABC != -1
// Winsen: команда 0x79 не возвращает подтверждения.
constexpr uint8_t abcMode = METEO_MHZ19B_ABC == 1 ? 0xA0 : 0x00;
constexpr uint8_t abcCommand[] = {
    0xFF, 0x01, 0x79, abcMode, 0, 0, 0, 0, static_cast<uint8_t>(-(0x01 + 0x79 + abcMode)),
};
bool abcCommandSent = false;
#endif
#if defined(ARDUINO_ARCH_AVR)
SoftwareSerial co2Serial(METEO_MHZ19B_RX_PIN, METEO_MHZ19B_TX_PIN);
#else
SoftwareSerial co2Serial;
#endif
uint32_t startedAt = 0;
bool warmedUp = false;
}  // namespace

void begin() {
#if defined(ARDUINO_ARCH_AVR)
  co2Serial.begin(9600);
#else
  co2Serial.begin(9600, SWSERIAL_8N1, METEO_MHZ19B_RX_PIN, METEO_MHZ19B_TX_PIN);
#endif
  startedAt = millis();
  warmedUp = false;
#if METEO_MHZ19B_ABC != -1
  abcCommandSent = false;
#endif
#if METEO_ENABLE_SERIAL_LOG
  Serial.println(F("MH-Z19B warming up (3 min)"));
#endif
}

Reading read() {
  // Прогрев отсчитываем без delay, поэтому в это время BME280 и дисплей продолжают работать.
  if (!warmedUp) {
    if (millis() - startedAt < warmupMs) {
      return {0, false};
    }
    warmedUp = true;
  }

  // Отбрасываем остаток старого ответа и запрашиваем один отсчёт командой 0x86.
  while (co2Serial.available()) {
    co2Serial.read();
  }
  co2Serial.write(readCommand, sizeof(readCommand));

  uint8_t response[sizeof(readCommand)];
  const uint32_t sentAt = millis();
  while (co2Serial.available() < static_cast<int>(sizeof(response))) {
    if (millis() - sentAt >= responseTimeoutMs) {
#if METEO_ENABLE_SERIAL_LOG
      Serial.println(F("MH-Z19B response timeout"));
#endif
      return {0, false};
    }
    yield();
  }

  for (auto& byte : response) {
    byte = static_cast<uint8_t>(co2Serial.read());
  }
  // Сумма байтов с номера команды до контрольного байта должна дать 0 по модулю 256.
  uint8_t checksum = 0;
  for (size_t i = 1; i < sizeof(response); ++i) {
    checksum += response[i];
  }
  const uint16_t raw = (static_cast<uint16_t>(response[2]) << 8) | response[3];
  if (response[0] != 0xFF || response[1] != 0x86 || checksum != 0 || raw == 0 || raw > INT_MAX) {
#if METEO_ENABLE_SERIAL_LOG
    Serial.println(F("MH-Z19B invalid response"));
#endif
    return {0, false};
  }
  const int ppm = static_cast<int>(raw);

#if METEO_MHZ19B_ABC != -1
  // Отправляем настройку один раз, когда датчик уже ответил корректным измерением.
  if (!abcCommandSent) {
    abcCommandSent = co2Serial.write(abcCommand, sizeof(abcCommand)) == sizeof(abcCommand);
  }
#endif

#if METEO_ENABLE_SERIAL_LOG
  Serial.print(F("CO2 (ppm): "));
  Serial.println(ppm);
#endif
  return {ppm, true};
}

}  // namespace mhz19b
#endif  // UART
