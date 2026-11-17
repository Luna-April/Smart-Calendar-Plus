#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <time.h>

#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <GxEPD2_BW.h>

#include "epd_config.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Missing include/secrets.h - copy include/secrets.example.h and fill in Wi-Fi credentials."
#endif

// Experiment 03 - Wi-Fi, NTP and calendar.
//
// Refresh strategy:
//   - full refresh on boot, date change, Wi-Fi state change and every hour;
//   - partial refresh of the time area once per minute.
// This keeps the clock minute-accurate without a full-screen flash every minute.

namespace {

constexpr const char* TIMEZONE = "CST-8";  // China Standard Time (UTC+8)
constexpr const char* NTP_SERVER1 = "ntp.aliyun.com";
constexpr const char* NTP_SERVER2 = "cn.pool.ntp.org";
constexpr const char* NTP_SERVER3 = "pool.ntp.org";

constexpr uint32_t WIFI_TIMEOUT_MS = 20000;
constexpr uint32_t NTP_TIMEOUT_MS = 15000;
constexpr uint32_t WIFI_RETRY_MS = 30000;
constexpr uint32_t NTP_RETRY_MS = 30000;
constexpr uint32_t FULL_REFRESH_MS = 3600000UL;  // 1 hour

constexpr int16_t TIME_BASELINE = 70;
constexpr int16_t DATE_BASELINE = 100;
constexpr int16_t MONTH_BASELINE = 128;
constexpr int16_t WEEKDAY_BASELINE = 148;
constexpr int16_t GRID_X = 11;
constexpr int16_t GRID_Y = 155;
constexpr int16_t CELL_W = 54;
constexpr int16_t CELL_H = 20;
constexpr int16_t GRID_ROWS = 6;
constexpr int16_t STATUS_BASELINE = 292;

const char* const MONTH_NAMES[] = {"January", "February", "March",     "April",
                                   "May",     "June",     "July",      "August",
                                   "September", "October", "November", "December"};
const char* const WEEKDAY_NAMES[] = {"Sunday",   "Monday", "Tuesday", "Wednesday",
                                     "Thursday", "Friday", "Saturday"};
const char* const WEEKDAY_SHORT[] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};

GxEPD2_BW<epd::Driver, epd::HEIGHT> display(
    epd::Driver(epd::PIN_CS, epd::PIN_DC, epd::PIN_RST, epd::PIN_BUSY));

bool ntpSynced = false;
int lastMinute = -1;
int lastDay = -1;
uint32_t lastFullRefreshMs = 0;
uint32_t lastWifiRetryMs = 0;
uint32_t lastNtpRetryMs = 0;
bool lastWifiConnected = false;

void drawCentered(Adafruit_GFX& gfx, const char* text, int16_t baseline,
                  const GFXfont* font) {
  gfx.setFont(font);
  gfx.setTextColor(GxEPD_BLACK);

  int16_t tbx, tby;
  uint16_t tbw, tbh;
  gfx.getTextBounds(text, 0, 0, &tbx, &tby, &tbw, &tbh);
  gfx.setCursor((gfx.width() - tbw) / 2 - tbx, baseline);
  gfx.print(text);
}

int daysInMonth(int year, int month) {
  static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) {
    return 29;
  }
  return days[month];
}

void drawTime(const struct tm& t) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
  drawCentered(display, buf, TIME_BASELINE, &FreeSansBold24pt7b);
}

void drawDate(const struct tm& t) {
  char buf[40];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d  %s", t.tm_year + 1900,
           t.tm_mon + 1, t.tm_mday, WEEKDAY_NAMES[t.tm_wday]);
  drawCentered(display, buf, DATE_BASELINE, &FreeSans9pt7b);
}

void drawCalendar(const struct tm& t) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%s %04d", MONTH_NAMES[t.tm_mon], t.tm_year + 1900);
  drawCentered(display, buf, MONTH_BASELINE, &FreeSans9pt7b);

  display.setFont(&FreeSans9pt7b);
  display.setTextColor(GxEPD_BLACK);
  for (int col = 0; col < 7; ++col) {
    const char* label = WEEKDAY_SHORT[col];
    int16_t tbx, tby;
    uint16_t tbw, tbh;
    display.getTextBounds(label, 0, 0, &tbx, &tby, &tbw, &tbh);
    display.setCursor(GRID_X + col * CELL_W + (CELL_W - tbw) / 2 - tbx,
                      WEEKDAY_BASELINE);
    display.print(label);
  }

  // Grid lines first, day numbers on top.
  display.drawRect(GRID_X, GRID_Y, 7 * CELL_W, GRID_ROWS * CELL_H, GxEPD_BLACK);
  for (int col = 1; col < 7; ++col) {
    display.drawLine(GRID_X + col * CELL_W, GRID_Y,
                     GRID_X + col * CELL_W, GRID_Y + GRID_ROWS * CELL_H,
                     GxEPD_BLACK);
  }
  for (int row = 1; row < GRID_ROWS; ++row) {
    display.drawLine(GRID_X, GRID_Y + row * CELL_H, GRID_X + 7 * CELL_W,
                     GRID_Y + row * CELL_H, GxEPD_BLACK);
  }

  struct tm first = t;
  first.tm_mday = 1;
  first.tm_hour = 12;
  first.tm_min = 0;
  first.tm_sec = 0;
  first.tm_isdst = -1;  // let mktime determine DST for the first of the month
  const time_t firstTime = mktime(&first);
  localtime_r(&firstTime, &first);
  const int startCol = (first.tm_wday + 6) % 7;  // Monday = 0
  const int days = daysInMonth(t.tm_year + 1900, t.tm_mon);

  for (int day = 1; day <= days; ++day) {
    const int index = startCol + day - 1;
    const int col = index % 7;
    const int row = index / 7;
    const int x = GRID_X + col * CELL_W;
    const int y = GRID_Y + row * CELL_H;

    char dayBuf[3];
    snprintf(dayBuf, sizeof(dayBuf), "%d", day);
    int16_t tbx, tby;
    uint16_t tbw, tbh;
    display.getTextBounds(dayBuf, 0, 0, &tbx, &tby, &tbw, &tbh);

    if (day == t.tm_mday) {
      display.fillRect(x + 2, y + 1, CELL_W - 4, CELL_H - 2, GxEPD_BLACK);
      display.setTextColor(GxEPD_WHITE);
    } else {
      display.setTextColor(GxEPD_BLACK);
    }
    display.setCursor(x + (CELL_W - tbw) / 2 - tbx, y + 15);
    display.print(dayBuf);
    display.setTextColor(GxEPD_BLACK);
  }
}

void drawStatus() {
  char buf[48];
  if (WiFi.status() == WL_CONNECTED) {
    snprintf(buf, sizeof(buf), "Wi-Fi OK | %s",
             ntpSynced ? "NTP synced" : "NTP waiting");
  } else {
    snprintf(buf, sizeof(buf), "Wi-Fi offline | retrying");
  }
  drawCentered(display, buf, STATUS_BASELINE, &FreeSans9pt7b);
}

void drawFullScreen(const struct tm& t) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawTime(t);
    drawDate(t);
    drawCalendar(t);
    drawStatus();
  } while (display.nextPage());

  lastMinute = t.tm_min;
  lastDay = t.tm_mday;
  lastFullRefreshMs = millis();
}

void drawTimePartial(const struct tm& t) {
  // y=16 and h=64 keep the partial window aligned to the controller's 8-pixel
  // RAM pages. The time glyphs occupy roughly y=36..74.
  display.setPartialWindow(0, 16, epd::WIDTH, 64);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawTime(t);
  } while (display.nextPage());

  lastMinute = t.tm_min;
}

const char* wifiStatusName(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED: return "SCAN_COMPLETED";
    case WL_CONNECTED: return "CONNECTED";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    default: return "UNKNOWN";
  }
}

bool connectWiFi() {
  if (strcmp(WIFI_SSID, "YOUR_WIFI_SSID") == 0) {
    Serial.println("Wi-Fi: edit include/secrets.h first");
    return false;
  }

  // Match the known-good ESP32-S3 project's station setup.
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.setSleep(false);

  // This ESP32-C3 SuperMini fails WPA2 authentication at the default 19.5 dBm
  // (disconnect reason 2/202) but connects reliably at 11 dBm. Keep 11 dBm.
  WiFi.setTxPower(WIFI_POWER_11dBm);

  Serial.println("Wi-Fi: connecting...");
  const wl_status_t beginResult = WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Wi-Fi: begin() -> %s (%d)\n", wifiStatusName(beginResult),
                beginResult);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Wi-Fi: connected, IP %s\n",
                  WiFi.localIP().toString().c_str());
    return true;
  }

  Serial.printf("Wi-Fi: connection timed out, status=%s (%d)\n",
                wifiStatusName(WiFi.status()), WiFi.status());
  return false;
}

bool syncNtp() {
  Serial.println("NTP: syncing...");
  configTzTime(TIMEZONE, NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);

  struct tm t;
  const uint32_t start = millis();
  while (millis() - start < NTP_TIMEOUT_MS) {
    if (getLocalTime(&t, 1000)) {
      Serial.printf("NTP: %04d-%02d-%02d %02d:%02d:%02d\n", t.tm_year + 1900,
                    t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
      ntpSynced = true;
      return true;
    }
    Serial.print('.');
  }
  Serial.println();
  Serial.println("NTP: sync timed out");
  ntpSynced = false;
  return false;
}

void drawConnectingScreen() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawCentered(display, "Smart Calendar", 120, &FreeSansBold24pt7b);
    drawCentered(display, "Connecting to Wi-Fi...", 180, &FreeSans9pt7b);
    drawCentered(display, "Please wait", 215, &FreeSans9pt7b);
  } while (display.nextPage());
}

void drawNoTimeScreen(bool wifiConnected) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawCentered(display, "No time yet", 130, &FreeSansBold24pt7b);
    drawCentered(display,
                 wifiConnected ? "NTP sync failed - retrying"
                               : "Wi-Fi offline - retrying",
                 190, &FreeSans9pt7b);
    drawCentered(display,
                 wifiConnected ? "Check router / NTP" : "Check Wi-Fi / router",
                 225, &FreeSans9pt7b);
  } while (display.nextPage());
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("==========================================");
  Serial.println(" Smart Calendar - Experiment 03");
  Serial.println(" Wi-Fi + NTP + calendar");
  Serial.println("==========================================");
  Serial.printf("SPI  : SCK=%d MOSI=%d CS=%d\n", epd::PIN_SCK, epd::PIN_MOSI,
                epd::PIN_CS);
  Serial.printf("Ctrl : DC=%d RST=%d BUSY=%d\n", epd::PIN_DC, epd::PIN_RST,
                epd::PIN_BUSY);
  Serial.println("Power: 3.3 V only, RESE = 3 ohm");
  Serial.println("------------------------------------------");

  SPI.begin(epd::PIN_SCK, epd::PIN_MISO, epd::PIN_MOSI, epd::PIN_CS);
  display.init(115200, true, 50, false);
  display.setRotation(0);
  pinMode(epd::PIN_BUSY, INPUT_PULLUP);

  drawConnectingScreen();

  const bool wifiConnected = connectWiFi();
  if (wifiConnected) {
    syncNtp();
  }

  struct tm t;
  if (getLocalTime(&t, 0)) {
    drawFullScreen(t);
  } else {
    drawNoTimeScreen(wifiConnected);
    lastMinute = -1;
    lastDay = -1;
  }

  lastWifiConnected = (WiFi.status() == WL_CONNECTED);
  lastWifiRetryMs = millis();
  lastNtpRetryMs = millis();
}

void loop() {
  const uint32_t now = millis();

  // Wi-Fi reconnect.
  if (WiFi.status() != WL_CONNECTED) {
    if (lastWifiConnected) {
      lastWifiConnected = false;
      struct tm t;
      if (getLocalTime(&t, 0)) {
        drawFullScreen(t);
      }
    }

    if (now - lastWifiRetryMs >= WIFI_RETRY_MS) {
      lastWifiRetryMs = now;
      Serial.println("Wi-Fi: retrying...");
      if (connectWiFi()) {
        lastWifiConnected = true;
        syncNtp();
        struct tm t;
        if (getLocalTime(&t, 0)) {
          drawFullScreen(t);
        }
      }
    }
    delay(200);
    return;
  }

  // Wi-Fi is up but NTP has not synced yet: retry periodically.
  if (!ntpSynced) {
    if (now - lastNtpRetryMs >= NTP_RETRY_MS) {
      lastNtpRetryMs = now;
      if (syncNtp()) {
        struct tm t;
        if (getLocalTime(&t, 0)) {
          drawFullScreen(t);
        }
      }
    }
  }

  struct tm t;
  if (!getLocalTime(&t, 0)) {
    delay(200);
    return;
  }

  if (t.tm_mday != lastDay || now - lastFullRefreshMs >= FULL_REFRESH_MS) {
    drawFullScreen(t);
  } else if (t.tm_min != lastMinute) {
    drawTimePartial(t);
  }

  static uint32_t lastReport = 0;
  if (now - lastReport >= 30000) {
    lastReport = now;
    Serial.printf("[%lu s] %04d-%02d-%02d %02d:%02d Wi-Fi=%d NTP=%d\n",
                  static_cast<unsigned long>(now / 1000), t.tm_year + 1900,
                  t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min,
                  WiFi.status() == WL_CONNECTED ? 1 : 0, ntpSynced ? 1 : 0);
  }

  delay(200);
}
