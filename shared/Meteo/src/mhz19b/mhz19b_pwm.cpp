#include <config.h>

#if METEO_ENABLE_MHZ19B && METEO_MHZ19B_USE_PWM
#include "mhz19b.h"
#include <Arduino.h>

namespace mhz19b {
namespace {
constexpr uint32_t warmupMs = 0;
// constexpr uint32_t warmupMs = 60 * 1000UL;
constexpr uint32_t cycleUs = 1004000;
constexpr uint32_t cycleToleranceUs = cycleUs / 20;
constexpr uint32_t edgeOffsetUs = 2000;
constexpr uint32_t staleUs = 3 * cycleUs;
constexpr int rangePpm = METEO_MHZ19B_PWM_RANGE_PPM;

static_assert(METEO_MHZ19B_PWM_PIN != 16, "ESP8266 GPIO16 does not support interrupts");
static_assert(!METEO_ENABLE_DS18B20 || METEO_MHZ19B_PWM_PIN != METEO_DS18B20_DATA_PIN,
              "MH-Z19B PWM and DS18B20 must use separate pins");
static_assert(rangePpm == 2000 || rangePpm == 5000 || rangePpm == 10000,
              "Select the PWM range of your MH-Z19B module");

uint32_t startedAt;
bool warmedUp;
uint32_t lastRiseUs;
uint32_t highUs;
bool haveRise;
bool haveFall;
volatile uint32_t sampleCycleUs;
volatile uint32_t sampleHighUs;
volatile uint32_t sampleAtUs;
#if METEO_ENABLE_SERIAL_LOG
volatile bool sawEdge;
volatile uint32_t lastEdgeUs;
volatile uint32_t observedCycleUs;
volatile uint32_t observedHighUs;
#endif

void IRAM_ATTR captureEdge() {
  const uint32_t now = micros();
#if METEO_ENABLE_SERIAL_LOG
  sawEdge = true;
  lastEdgeUs = now;
#endif
  if (digitalRead(METEO_MHZ19B_PWM_PIN)) {
    if (haveRise && haveFall) {
      const uint32_t period = now - lastRiseUs;
#if METEO_ENABLE_SERIAL_LOG
      observedCycleUs = period;
      observedHighUs = highUs;
#endif
      if (period >= cycleUs - cycleToleranceUs && period <= cycleUs + cycleToleranceUs &&
          highUs != 0 && highUs < period) {
        sampleHighUs = highUs;
        sampleCycleUs = period;
        sampleAtUs = now;
      }
    }
    lastRiseUs = now;
    haveRise = true;
    haveFall = false;
  } else if (haveRise) {
    highUs = now - lastRiseUs;
    haveFall = true;
  }
}
}  // namespace

void begin() {
  startedAt = millis();
  warmedUp = false;
  haveRise = false;
  haveFall = false;
  sampleCycleUs = 0;
  pinMode(METEO_MHZ19B_PWM_PIN, INPUT);
#if METEO_ENABLE_SERIAL_LOG
  sawEdge = false;
  observedCycleUs = 0;
#endif
  attachInterrupt(digitalPinToInterrupt(METEO_MHZ19B_PWM_PIN), captureEdge, CHANGE);
#if METEO_ENABLE_SERIAL_LOG
  Serial.print(F("MH-Z19B PWM active, GPIO"));
  Serial.println(METEO_MHZ19B_PWM_PIN);
#endif
}

Reading read() {
  if (!warmedUp) {
    if (millis() - startedAt < warmupMs) return {0, false};
    warmedUp = true;
  }

  // Копируем один завершённый период, пока обработчик не обновил его поля.
  noInterrupts();
  const uint32_t now = micros();
  if (now - sampleAtUs > staleUs) sampleCycleUs = 0;
  const uint32_t period = sampleCycleUs;
  const uint32_t high = sampleHighUs;
#if METEO_ENABLE_SERIAL_LOG
  if (now - lastEdgeUs > staleUs) {
    sawEdge = false;
    observedCycleUs = 0;
  }
  const bool recentEdges = sawEdge;
  const uint32_t measuredCycle = observedCycleUs;
  const uint32_t measuredHigh = observedHighUs;
#endif
  interrupts();
  if (period == 0) {
#if METEO_ENABLE_SERIAL_LOG
    if (!recentEdges) {
      Serial.print(F("MH-Z19B PWM: no edges, input="));
      Serial.println(digitalRead(METEO_MHZ19B_PWM_PIN));
    } else if (measuredCycle == 0) {
      Serial.println(F("MH-Z19B PWM: waiting for a complete pulse"));
    } else {
      Serial.print(F("MH-Z19B PWM: unexpected pulse, period (us)="));
      Serial.print(measuredCycle);
      Serial.print(F(", high (us)="));
      Serial.println(measuredHigh);
    }
#endif
    return {0, false};
  }

  // Winsen: ppm = range * (TH - 2 ms) / (TH + TL - 4 ms).
  const uint32_t usablePeriod = period - 2 * edgeOffsetUs;
  const uint32_t usableHigh = high > edgeOffsetUs ? high - edgeOffsetUs : 0;
  const uint32_t boundedHigh = usableHigh < usablePeriod ? usableHigh : usablePeriod;
  const int ppm = (static_cast<uint64_t>(boundedHigh) * rangePpm + usablePeriod / 2) / usablePeriod;
#if METEO_ENABLE_SERIAL_LOG
  Serial.print(F("CO2 (ppm, PWM): "));
  Serial.println(ppm);
#endif
  return {ppm, true};
}
}  // namespace mhz19b
#endif  // PWM
