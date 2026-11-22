#pragma once

namespace bme280 {

struct Reading {
  float temperature;
  float pressureMmHg;
  float humidity;
};

void begin();
Reading read();

}  // namespace bme280
