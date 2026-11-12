#pragma once

#include <GxEPD2_BW.h>

// Hardware configuration for the 4.2" panel.
//
// Panel : GDEY042T81 (physical label 042BN-T81-D2), 400x300, black/white
// Driver: SSD1683, 24-pin 0.5 mm FPC
//
// ESP32-C3 SuperMini pin choice:
//   - GPIO4/6 are the default FSPI SCK/MOSI on ESP32-C3.
//   - GPIO5 is left as the (unused) MISO so SPI.begin() does not log an error.
//   - GPIO2, GPIO8, GPIO9 are strapping pins and are avoided.
//   - GPIO18/19 are USB, GPIO20/21 are UART0; all avoided.
//   - GPIO3, GPIO7, GPIO10 and GPIO0 are free and safe for control signals.
//
// The GPIO numbers below are the only source of truth for wiring. They do not
// rely on the lolin_c3_mini variant defaults (which use different pins).

namespace epd {

constexpr int8_t PIN_SCK = 4;   // -> driver board SCK
constexpr int8_t PIN_MISO = 5;  // not connected; kept for SPI bus init only
constexpr int8_t PIN_MOSI = 6;  // -> driver board SDA
constexpr int8_t PIN_CS = 7;    // -> driver board CS1
constexpr int8_t PIN_DC = 3;    // -> driver board DC
constexpr int8_t PIN_RST = 10;  // -> driver board RST
constexpr int8_t PIN_BUSY = 0;  // <- driver board BUSY

constexpr int16_t WIDTH = 400;
constexpr int16_t HEIGHT = 300;

using Driver = GxEPD2_420_GDEY042T81;

}  // namespace epd
