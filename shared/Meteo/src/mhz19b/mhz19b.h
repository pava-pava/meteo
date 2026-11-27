#pragma once

namespace mhz19b {

struct Reading {
  int ppm;
  bool valid;
};

void begin();
Reading read();

}  // namespace mhz19b
