#include <config.h>

#if METEO_ENABLE_DISPLAY
#include "display.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// TFT display libs
#include <Adafruit_GFX.h>       // include Adafruit graphics library
#include <Adafruit_ILI9341.h>   // include Adafruit ILI9341 TFT library
#include "display_fonts.h"
#if defined(ARDUINO_ARCH_AVR)
#include "display_fonts_nano.h"
#endif

namespace {
#if METEO_TFT_HARDWARE_SPI
static_assert(METEO_TFT_SCK_PIN == SCK && METEO_TFT_MOSI_PIN == MOSI,
              "Use the board SPI pins or set METEO_TFT_HARDWARE_SPI to 0");
Adafruit_ILI9341 tft(METEO_TFT_CS_PIN, METEO_TFT_DC_PIN, METEO_TFT_RST_PIN);
#else
Adafruit_ILI9341 tft(METEO_TFT_CS_PIN, METEO_TFT_DC_PIN, METEO_TFT_MOSI_PIN,
                    METEO_TFT_SCK_PIN, METEO_TFT_RST_PIN);
#endif

constexpr uint16_t backgroundColor = 0x10E4;
constexpr uint16_t labelColor = 0xA5D9;
constexpr uint16_t valueColor = 0xEFBE;
constexpr uint16_t co2Color = 0x7F19;
constexpr uint16_t dividerColor = 0x3229;
constexpr int16_t margin = 12;
constexpr int16_t columnGap = 7;
constexpr int16_t co2UnitWidth = 36;
constexpr int16_t co2UnitGap = 6;
constexpr int16_t co2FieldHeight = 70;
#if METEO_TFT_BUFFERED
constexpr int16_t maxScreenWidth = ILI9341_TFTWIDTH > ILI9341_TFTHEIGHT
                                      ? ILI9341_TFTWIDTH : ILI9341_TFTHEIGHT;
constexpr int16_t canvasWidth = maxScreenWidth - 2 * margin - co2UnitWidth - co2UnitGap;

// Все поля используют один монохромный буфер размером с самое большое поле.
GFXcanvas1 valueCanvas(canvasWidth, co2FieldHeight);
uint16_t valueRow[canvasWidth];
#endif

struct ValueStyle {
  const GFXfont* font;
  uint8_t scale;
};

#if defined(ARDUINO_ARCH_AVR)
constexpr ValueStyle co2Style{&nanoDigits, 6};
constexpr ValueStyle valueStyle{&nanoDigits, 3};
constexpr ValueStyle compactStyle{&nanoDigits, 2};
#else
constexpr ValueStyle co2Style{&interLarge, 1};
constexpr ValueStyle valueStyle{&interMedium, 1};
constexpr ValueStyle compactStyle{&interSmall, 1};
#endif

struct DisplayField {
  int16_t x;
  int16_t y;
  int16_t width;
  int16_t height;
  int16_t baseline;
  ValueStyle style;
  ValueStyle compactStyle;
  char previous[16];
};

DisplayField co2Field{};
DisplayField temperatureField{};
DisplayField humidityField{};
DisplayField pressureField{};

void drawCaption(int16_t x, int16_t y, const DisplayCaption& caption) {
  tft.drawBitmap(x, y, caption.pixels, caption.width, caption.height, labelColor);
}

void drawDisplayLayout() {
  // Геометрию рассчитываем по размерам драйвера. Подписи и разделитель рисуем при запуске.
  const int16_t dividerY = tft.height() / 2 + 10;
  const int16_t contentWidth = tft.width() - 2 * margin;
  const int16_t columnWidth = (contentWidth - 2 * columnGap) / 3;
  const int16_t co2Y = dividerY - 84;
  const int16_t labelsY = dividerY + 18;
  const int16_t valuesY = labelsY + 22;
  const int16_t unitsY = valuesY + 34;
  const int16_t valuesBaseline = valuesY + 26;
  const int16_t co2Baseline = co2Y + 58;
  const int16_t humidityX = margin + columnWidth + columnGap;
  const int16_t pressureX = humidityX + columnWidth + columnGap;
  const int16_t co2UnitX = tft.width() - margin - co2UnitWidth;
  const int16_t co2Width = co2UnitX - margin - co2UnitGap;

  co2Field = {margin, co2Y, co2Width, co2FieldHeight, co2Baseline, co2Style, valueStyle, {}};
  temperatureField = {margin, valuesY, columnWidth, 32, valuesBaseline,
                      valueStyle, compactStyle, {}};
  humidityField = {humidityX, valuesY, columnWidth, 32, valuesBaseline,
                   valueStyle, compactStyle, {}};
  pressureField = {pressureX, valuesY, columnWidth, 32, valuesBaseline,
                   valueStyle, compactStyle, {}};

  tft.fillScreen(backgroundColor);
  tft.setTextSize(1);
  tft.setTextWrap(false);
#if METEO_TFT_BUFFERED
  valueCanvas.setTextWrap(false);
#endif

  drawCaption(margin, co2Y - 22, captionCo2);
  drawCaption(co2UnitX, co2Baseline - 9, captionPpm);
  tft.drawFastHLine(margin, dividerY, contentWidth, dividerColor);
  drawCaption(margin, labelsY, captionTemperature.width <= columnWidth
                                  ? captionTemperature : captionTemperatureCompact);
  drawCaption(humidityX, labelsY, captionHumidity.width <= columnWidth
                                     ? captionHumidity : captionHumidityCompact);
  drawCaption(pressureX, labelsY, captionPressure);
  drawCaption(margin, unitsY, captionCelsius);
  drawCaption(humidityX, unitsY, captionPercent);
  drawCaption(pressureX, unitsY, captionMillimeters);
}

#if METEO_TFT_BUFFERED
void writeValueField(const DisplayField& field, uint16_t color) {
  constexpr int16_t canvasStride = (canvasWidth + 7) / 8;
  const uint8_t* row = valueCanvas.getBuffer();

  tft.startWrite();
  // В одном окне SPI отправляем и фон, и текст. Каждый бит буфера превращаем в пиксель RGB565.
  tft.setAddrWindow(field.x, field.y, field.width, field.height);
  for (int16_t y = 0; y < field.height; ++y, row += canvasStride) {
    for (int16_t x = 0; x < field.width; ++x) {
      const bool foreground = row[x / 8] & (0x80 >> (x % 8));
      valueRow[x] = foreground ? color : backgroundColor;
    }
    tft.writePixels(valueRow, field.width);
  }
  tft.endWrite();
}

#endif

void drawDisplayValue(DisplayField& field, const char* text, uint16_t color) {
  if (strcmp(field.previous, text) == 0) {
    return;
  }

  Adafruit_GFX* drawing = &tft;
#if METEO_TFT_BUFFERED
  if (valueCanvas.getBuffer()) {
    drawing = &valueCanvas;
  }
#endif
  int16_t left, top;
  uint16_t width, height;
  drawing->setFont(field.style.font);
  drawing->setTextSize(field.style.scale);
  drawing->getTextBounds(text, 0, 0, &left, &top, &width, &height);
  // Если число не помещается, пробуем компактный шрифт и только потом подставляем прочерк.
  if (width > static_cast<uint16_t>(field.width)) {
    drawing->setFont(field.compactStyle.font);
    drawing->setTextSize(field.compactStyle.scale);
    drawing->getTextBounds(text, 0, 0, &left, &top, &width, &height);
  }
  if (width == 0 || width > static_cast<uint16_t>(field.width)) {
    text = "--";
    drawing->getTextBounds(text, 0, 0, &left, &top, &width, &height);
  }
  if (strcmp(field.previous, text) == 0) {
    return;
  }

  // Готовим поле целиком в памяти. На экране не появляется пустой кадр между очисткой и текстом.
#if METEO_TFT_BUFFERED
  if (drawing == &valueCanvas) {
    valueCanvas.fillScreen(0);
    valueCanvas.setTextColor(1);
    valueCanvas.setCursor(-left, field.baseline - field.y);
    valueCanvas.print(text);
    writeValueField(field, color);
  } else
#endif
  {
    // На AVR очищаем и перерисовываем только поле, которое изменилось.
    tft.fillRect(field.x, field.y, field.width, field.height, backgroundColor);
    tft.setTextColor(color);
    tft.setCursor(field.x - left, field.baseline);
    tft.print(text);
  }
  // Сохраняем показанный текст. Следующий такой же результат не потребует записи по SPI.
  strcpy(field.previous, text);
}

void formatDisplayValue(char (&text)[16], float value, int decimals) {
  // NaN от BME280 превращаем в прочерк, дробные числа выводим с десятичной запятой.
  if (isfinite(value) && fabs(value) < 1000000) {
#if defined(ARDUINO_ARCH_AVR)
    // Обычный snprintf в AVR Libc не форматирует числа с плавающей точкой.
    dtostrf(value, 0, decimals, text);
    const size_t length = strlen(text);
#else
    const int length = snprintf(text, sizeof(text), "%.*f", decimals, value);
#endif
    if (length > 0 && length < static_cast<int>(sizeof(text))) {
      if (char* decimalPoint = strchr(text, '.')) {
        *decimalPoint = ',';
      }
      return;
    }
  }
  strcpy(text, "--");
}
}  // namespace

void initDisplay() {
  tft.begin();
  tft.setRotation(METEO_TFT_ROTATION);
  drawDisplayLayout();
}

void updateDisplay(int co2, bool co2Valid, float temperature, float humidity,
                   float pressureMmHg) {
  char text[16];
#if defined(ARDUINO_ARCH_AVR)
  itoa(co2, text, 10);
#else
  snprintf(text, sizeof(text), "%d", co2);
#endif
  drawDisplayValue(co2Field, co2Valid ? text : "--", co2Color);

  formatDisplayValue(text, temperature, 1);
  drawDisplayValue(temperatureField, text, valueColor);
  formatDisplayValue(text, humidity, 0);
  drawDisplayValue(humidityField, text, valueColor);
  formatDisplayValue(text, pressureMmHg, 0);
  drawDisplayValue(pressureField, text, valueColor);
}
#endif  // METEO_ENABLE_DISPLAY
