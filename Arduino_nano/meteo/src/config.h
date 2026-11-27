#pragma once

// Пины классической Arduino Nano с ATmega328P.

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
#define METEO_ENABLE_MHZ19B 1   // UART
#endif

#ifndef METEO_ENABLE_DS18B20
#define METEO_ENABLE_DS18B20 1  // 1-Wire
#endif

// Пины аппаратного SPI фиксированы. Для других SCK/MOSI выберите программный SPI.
#define METEO_TFT_HARDWARE_SPI 1
#define METEO_TFT_SCK_PIN 13
#define METEO_TFT_MOSI_PIN 11
#define METEO_TFT_CS_PIN 10
#define METEO_TFT_DC_PIN 9
#define METEO_TFT_RST_PIN 8
#define METEO_TFT_ROTATION 0
#define METEO_TFT_BUFFERED 0  // У Nano 2 КБ SRAM

#define METEO_BME280_SDA_PIN A4
#define METEO_BME280_SCL_PIN A5
#define METEO_BME280_I2C_ADDRESS 0x76

// ABC: -1 — оставить настройку датчика, 0 — выключить, 1 — включить.
#ifndef METEO_MHZ19B_ABC
#define METEO_MHZ19B_ABC 1
#endif
#define METEO_MHZ19B_USE_PWM 0  // Nano: UART
#define METEO_MHZ19B_RX_PIN 2  // TX датчика
#define METEO_MHZ19B_TX_PIN 3  // RX датчика
#define METEO_DS18B20_DATA_PIN 4

