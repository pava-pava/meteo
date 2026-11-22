#pragma once

namespace telemetry {

struct Reading {
  float temperatureBme;
  float temperatureDs;
  float humidity;
  float pressureMmHg;
  int co2Ppm;
  bool co2Valid;
};

void begin();
void publish(const Reading& reading);

}  // namespace telemetry
