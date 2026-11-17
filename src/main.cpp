#include <Arduino.h>
#include <SPI.h>

#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <GxEPD2_BW.h>

#include "epd_config.h"

// Experiment 02 - clean Hello World build.
//
// Wiring (V1.5 driver board, seller's ESP32C3 diagram):
//   SCK=GPIO4, SDA=GPIO6, CS1=GPIO7, DC=GPIO3, RST=GPIO2, BUSY=GPIO10.
// Power: 3.3 V only. RESE: 3 ohm.
//
// One full refresh, then hibernate. No periodic refresh and no diagnostic
// BUSY probing.

GxEPD2_BW<epd::Driver, epd::HEIGHT> display(
    epd::Driver(epd::PIN_CS, epd::PIN_DC, epd::PIN_RST, epd::PIN_BUSY));

static void drawCentered(Adafruit_GFX& gfx, const char* text, int16_t baseline,
                         const GFXfont* font) {
  gfx.setFont(font);
  gfx.setTextColor(GxEPD_BLACK);

  int16_t tbx, tby;
  uint16_t tbw, tbh;
  gfx.getTextBounds(text, 0, 0, &tbx, &tby, &tbw, &tbh);
  gfx.setCursor((gfx.width() - tbw) / 2 - tbx, baseline);
  gfx.print(text);
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("==========================================");
  Serial.println(" Smart Calendar - Experiment 02");
  Serial.println(" GDEY042T81 / SSD1683 - Hello World");
  Serial.println("==========================================");
  Serial.printf("SPI  : SCK=%d MOSI=%d CS=%d\n", epd::PIN_SCK, epd::PIN_MOSI,
                epd::PIN_CS);
  Serial.printf("Ctrl : DC=%d RST=%d BUSY=%d\n", epd::PIN_DC, epd::PIN_RST,
                epd::PIN_BUSY);
  Serial.println("Power: 3.3 V only, RESE = 3 ohm");
  Serial.println("------------------------------------------");

  // Configure the ESP32-C3 SPI bus explicitly before GxEPD2 calls SPI.begin().
  // The second begin() inside the library is a no-op once the bus is running.
  SPI.begin(epd::PIN_SCK, epd::PIN_MISO, epd::PIN_MOSI, epd::PIN_CS);

  display.init(115200, true, 50, false);
  display.setRotation(0);

  // Keep BUSY pulled up. Some V1.5 board revisions do not populate an external
  // pull-up; harmless if they do. GxEPD2 otherwise configures the pin as INPUT.
  pinMode(epd::PIN_BUSY, INPUT_PULLUP);

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.drawRect(8, 8, epd::WIDTH - 16, epd::HEIGHT - 16, GxEPD_BLACK);
    drawCentered(display, "Hello World", 140, &FreeSansBold24pt7b);
    drawCentered(display, "Smart Calendar", 195, &FreeSans9pt7b);
    drawCentered(display, "Experiment 02 - DONE", 235, &FreeSans9pt7b);
    drawCentered(display, "400 x 300, black / white", 265, &FreeSans9pt7b);
  } while (display.nextPage());

  display.hibernate();
  Serial.println("Full refresh complete; panel hibernated.");
  Serial.println("Experiment 02 done. No further refresh.");
}

void loop() {
  static uint32_t last_report = 0;
  const uint32_t now = millis();

  if (now - last_report >= 10000) {
    last_report = now;
    Serial.printf("[%lu s] alive; panel hibernated\n",
                  static_cast<unsigned long>(now / 1000));
  }

  delay(100);
}
