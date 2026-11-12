#include <Arduino.h>
#include <SPI.h>

#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <GxEPD2_BW.h>

#include "epd_config.h"

// Experiment 02: first light test for the GDEY042T81 (SSD1683, 400x300) panel.
// Compile-only until the wiring and electrical compatibility are confirmed.
// One full refresh, then hibernate; no periodic refresh.

GxEPD2_BW<epd::Driver, epd::HEIGHT> display(
    epd::Driver(epd::PIN_CS, epd::PIN_DC, epd::PIN_RST, epd::PIN_BUSY));

static void drawCentered(const char* text, int16_t baseline,
                         const GFXfont* font) {
  display.setFont(font);
  display.setTextColor(GxEPD_BLACK);

  int16_t tbx, tby;
  uint16_t tbw, tbh;
  display.getTextBounds(text, 0, 0, &tbx, &tby, &tbw, &tbh);
  display.setCursor((display.width() - tbw) / 2 - tbx, baseline);
  display.print(text);
}

static void drawHelloWorld() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.drawRect(8, 8, display.width() - 16, display.height() - 16,
                     GxEPD_BLACK);

    drawCentered("Hello World", 140, &FreeSansBold24pt7b);
    drawCentered("Smart Calendar", 195, &FreeSans9pt7b);
    drawCentered("Experiment 02 - GDEY042T81 / SSD1683", 235,
                 &FreeSans9pt7b);
    drawCentered("400 x 300, black / white", 265, &FreeSans9pt7b);
  } while (display.nextPage());
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("==========================================");
  Serial.println(" Smart Calendar - Experiment 02");
  Serial.println(" GDEY042T81 / 042BN-T81-D2 - SSD1683");
  Serial.println("==========================================");
  Serial.printf("SPI  : SCK=%d MISO=%d MOSI=%d CS=%d\n", epd::PIN_SCK,
                epd::PIN_MISO, epd::PIN_MOSI, epd::PIN_CS);
  Serial.printf("Ctrl : DC=%d RST=%d BUSY=%d\n", epd::PIN_DC, epd::PIN_RST,
                epd::PIN_BUSY);
  Serial.println("Power: 3.3 V only, RESE switch must be on 3 ohm");
  Serial.println("------------------------------------------");

  // Configure the ESP32-C3 SPI bus explicitly before GxEPD2 calls SPI.begin().
  // The second begin() inside the library is a no-op once the bus is running.
  SPI.begin(epd::PIN_SCK, epd::PIN_MISO, epd::PIN_MOSI, epd::PIN_CS);

  // 10 ms reset pulse for a bare panel on a universal driver board.
  display.init(115200, true, 10, false);
  display.setRotation(0);

  Serial.printf("Panel: %d x %d\n", display.width(), display.height());
  Serial.println("Drawing Hello World (one full refresh)...");

  drawHelloWorld();
  display.hibernate();

  Serial.println("Full refresh complete; panel hibernated.");
  Serial.println("No further refresh will be performed.");
}

void loop() {
  static uint32_t last_report = 0;
  const uint32_t now = millis();

  if (now - last_report >= 5000) {
    last_report = now;
    Serial.printf("[%lu s] alive; panel hibernated\n",
                  static_cast<unsigned long>(now / 1000));
  }

  delay(100);
}
