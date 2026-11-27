#pragma once

#include <Adafruit_GFX.h>

namespace {
// Шрифт 5×7 для значений Nano. Сам шрифт хранится во флеш-памяти.
static const uint8_t nanoDigitsBitmap[] PROGMEM = {
    0x58,  // ,
    0xE0,  // -
    0xF0,  // .
    0x74, 0x63, 0x18, 0xC5, 0xC0,  // 0
    0x23, 0x08, 0x42, 0x11, 0xC0,  // 1
    0x74, 0x42, 0x22, 0x23, 0xE0,  // 2
    0xF0, 0x42, 0xE0, 0x87, 0xC0,  // 3
    0x11, 0x95, 0x2F, 0x88, 0x40,  // 4
    0xFC, 0x21, 0xE0, 0x87, 0xC0,  // 5
    0x74, 0x21, 0xE8, 0xC5, 0xC0,  // 6
    0xF8, 0x44, 0x44, 0x21, 0x00,  // 7
    0x74, 0x62, 0xE8, 0xC5, 0xC0,  // 8
    0x74, 0x62, 0xF0, 0x85, 0xC0,  // 9
};
static const GFXglyph nanoDigitsGlyphs[] PROGMEM = {
    {0, 2, 3, 3, 0, -2},  // ,
    {1, 3, 1, 4, 0, -4},  // -
    {2, 2, 2, 3, 0, -2},  // .
    {3, 0, 0, 0, 0, 0},  // /
    {3, 5, 7, 6, 0, -7},  // 0
    {8, 5, 7, 6, 0, -7},  // 1
    {13, 5, 7, 6, 0, -7},  // 2
    {18, 5, 7, 6, 0, -7},  // 3
    {23, 5, 7, 6, 0, -7},  // 4
    {28, 5, 7, 6, 0, -7},  // 5
    {33, 5, 7, 6, 0, -7},  // 6
    {38, 5, 7, 6, 0, -7},  // 7
    {43, 5, 7, 6, 0, -7},  // 8
    {48, 5, 7, 6, 0, -7},  // 9
};
static const GFXfont nanoDigits PROGMEM = {
    const_cast<uint8_t*>(nanoDigitsBitmap), const_cast<GFXglyph*>(nanoDigitsGlyphs),
    ',', '9', 9};
}  // namespace
