#pragma once

// 1 — включить модуль, 0 — выключить.
// Пины указаны по маркировке платы NodeMCU.

#ifndef METEO_ENABLE_SERIAL_LOG
#define METEO_ENABLE_SERIAL_LOG 0  // Диагностика через USB-UART, 9600 бод
#endif

#ifndef METEO_ENABLE_DISPLAY
#define METEO_ENABLE_DISPLAY 1  // ILI9341, SPI D5/D7, CS D8, DC D1
#endif

#ifndef METEO_ENABLE_BME280
#define METEO_ENABLE_BME280 1   // I²C, SDA D3, SCL D4
#endif

#ifndef METEO_ENABLE_MHZ19B
#define METEO_ENABLE_MHZ19B 1   // UART, RX D6, TX D0
#endif

#ifndef METEO_ENABLE_DS18B20
#define METEO_ENABLE_DS18B20 0  // 1-Wire, D2
#endif

#ifndef METEO_ENABLE_WIFI
#define METEO_ENABLE_WIFI 1  // Wi-Fi, параметры в telemetry/meteo_secrets.h
#endif

#ifndef METEO_STATION_ID
#define METEO_STATION_ID "meteo-1"
#endif

static_assert(METEO_ENABLE_SERIAL_LOG == 0 || METEO_ENABLE_SERIAL_LOG == 1,
              "METEO_ENABLE_SERIAL_LOG must be 0 or 1");
static_assert(METEO_ENABLE_WIFI == 0 || METEO_ENABLE_WIFI == 1,
              "METEO_ENABLE_WIFI must be 0 or 1");
static_assert(METEO_ENABLE_DISPLAY == 0 || METEO_ENABLE_DISPLAY == 1,
              "METEO_ENABLE_DISPLAY must be 0 or 1");
static_assert(METEO_ENABLE_BME280 == 0 || METEO_ENABLE_BME280 == 1,
              "METEO_ENABLE_BME280 must be 0 or 1");
static_assert(METEO_ENABLE_MHZ19B == 0 || METEO_ENABLE_MHZ19B == 1,
              "METEO_ENABLE_MHZ19B must be 0 or 1");
static_assert(METEO_ENABLE_DS18B20 == 0 || METEO_ENABLE_DS18B20 == 1,
              "METEO_ENABLE_DS18B20 must be 0 or 1");
