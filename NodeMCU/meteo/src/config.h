#pragma once

// 1 — включить модуль, 0 — выключить.
// Пины указаны по маркировке платы NodeMCU.

#ifndef METEO_ENABLE_SERIAL_LOG
#define METEO_ENABLE_SERIAL_LOG 1  // Диагностика через USB-UART, 9600 бод
#endif

#ifndef METEO_ENABLE_DISPLAY
#define METEO_ENABLE_DISPLAY 1  // ILI9341
#endif

#ifndef METEO_ENABLE_BME280
#define METEO_ENABLE_BME280 1   // I²C
#endif

#ifndef METEO_ENABLE_MHZ19B
#define METEO_ENABLE_MHZ19B 1   // PWM или UART
#endif

#ifndef METEO_ENABLE_DS18B20
#define METEO_ENABLE_DS18B20 1  // 1-Wire
#endif

#ifndef METEO_ENABLE_WIFI
#define METEO_ENABLE_WIFI 1  // Wi-Fi, параметры в telemetry/meteo_secrets.h
#endif

// Пины аппаратного SPI фиксированы. Для других SCK/MOSI выберите программный SPI.
#define METEO_TFT_HARDWARE_SPI 1
#define METEO_TFT_SCK_PIN D5
#define METEO_TFT_MOSI_PIN D7
#define METEO_TFT_CS_PIN D8
#define METEO_TFT_DC_PIN D1
#define METEO_TFT_RST_PIN -1  // RST дисплея не управляется GPIO
#define METEO_TFT_ROTATION 0
#define METEO_TFT_BUFFERED 1

#define METEO_BME280_SDA_PIN D3
#define METEO_BME280_SCL_PIN D4
#define METEO_BME280_I2C_ADDRESS 0x76

// 1 — читать PWM, 0 — использовать UART и настройку ABC ниже.
#ifndef METEO_MHZ19B_USE_PWM
#define METEO_MHZ19B_USE_PWM 0
#endif
#define METEO_MHZ19B_PWM_PIN D6  // GPIO12. SDO/MISO дисплея к D6 не подключать.
#define METEO_MHZ19B_PWM_RANGE_PPM 5000  // Верхний предел PWM вашего экземпляра.

// ABC: -1 — оставить настройку датчика, 0 — выключить, 1 — включить.
#ifndef METEO_MHZ19B_ABC
#define METEO_MHZ19B_ABC 1
#endif
#define METEO_MHZ19B_RX_PIN D6  // TX датчика
#define METEO_MHZ19B_TX_PIN D0  // RX датчика
#define METEO_DS18B20_DATA_PIN D2

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
