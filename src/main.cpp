#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>

#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <GxEPD2_BW.h>

#include "epd_config.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Missing include/secrets.h - copy include/secrets.example.h and fill in Wi-Fi credentials."
#endif

// Experiment 05 - Wi-Fi, NTP, calendar, todos and ChatGPT Plus quota.
//
// Todos are entered from a small web page served by the ESP32 and stored in
// NVS (Preferences). A local helper reads the Codex CLI usage endpoint and
// POSTs the 5-hour/weekly remaining quota to /quota; the token stays on the PC.
// Current fonts are ASCII-only; non-ASCII bytes are displayed as '?'.

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

constexpr int kMaxTodos = 3;
constexpr size_t kMaxTodoLen = 40;

// Layout: time/date, month calendar, today's todos, status line.
constexpr int16_t TIME_BASELINE = 44;
constexpr int16_t DATE_BASELINE = 70;
constexpr int16_t MONTH_BASELINE = 88;
constexpr int16_t WEEKDAY_BASELINE = 104;
constexpr int16_t GRID_X = 11;
constexpr int16_t GRID_Y = 108;
constexpr int16_t CELL_W = 35;
constexpr int16_t CELL_H = 18;
constexpr int16_t GRID_ROWS = 6;
constexpr int16_t TODOS_TITLE_BASELINE = 230;
constexpr int16_t TODO_BASELINES[kMaxTodos] = {246, 261, 276};
constexpr int16_t STATUS_BASELINE = 294;
constexpr int16_t TIME_PARTIAL_Y = 16;
constexpr int16_t TIME_PARTIAL_H = 40;

// ChatGPT quota panel to the right of the month grid.
constexpr int16_t QUOTA_SEPARATOR_X = 262;
constexpr int16_t QUOTA_TEXT_X = 268;
constexpr int16_t QUOTA_TITLE_Y = 122;
constexpr int16_t QUOTA_LINE1_Y = 142;
constexpr int16_t QUOTA_LINE2_Y = 160;
constexpr int16_t QUOTA_LINE3_Y = 178;
constexpr int16_t QUOTA_LINE4_Y = 196;
constexpr int16_t QUOTA_PARTIAL_X = 264;
constexpr int16_t QUOTA_PARTIAL_Y = 104;
constexpr int16_t QUOTA_PARTIAL_W = 128;
constexpr int16_t QUOTA_PARTIAL_H = 112;

const char* const MONTH_NAMES[] = {"January", "February", "March",     "April",
                                   "May",     "June",     "July",      "August",
                                   "September", "October", "November", "December"};
const char* const WEEKDAY_NAMES[] = {"Sunday",   "Monday", "Tuesday", "Wednesday",
                                     "Thursday", "Friday", "Saturday"};
const char* const WEEKDAY_SHORT[] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};

GxEPD2_BW<epd::Driver, epd::HEIGHT> display(
    epd::Driver(epd::PIN_CS, epd::PIN_DC, epd::PIN_RST, epd::PIN_BUSY));

WebServer server(80);
String todoText[kMaxTodos];
bool todoDone[kMaxTodos] = {false, false, false};
bool webServerStarted = false;
bool todosDirty = false;
bool quotaDirty = false;
bool mainScreenVisible = false;

String quotaPlan = "plus";
int quotaH5 = -1;
String quotaH5Reset;
int quotaWk = -1;
String quotaWkReset;
String quotaUpdated;
uint32_t quotaUpdatedEpoch = 0;

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

String htmlEscape(const String& input) {
  String out;
  out.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); ++i) {
    const char c = input[i];
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += c; break;
    }
  }
  return out;
}

void loadTodos() {
  Preferences prefs;
  prefs.begin("todos", true);
  for (int i = 0; i < kMaxTodos; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "t%d", i);
    todoText[i] = prefs.getString(key, "");
    snprintf(key, sizeof(key), "d%d", i);
    todoDone[i] = prefs.getBool(key, false);
  }
  prefs.end();
}

void saveTodos() {
  Preferences prefs;
  prefs.begin("todos", false);
  for (int i = 0; i < kMaxTodos; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "t%d", i);
    prefs.putString(key, todoText[i]);
    snprintf(key, sizeof(key), "d%d", i);
    prefs.putBool(key, todoDone[i]);
  }
  prefs.end();
}

String cleanShort(const String& input, size_t maxLen) {
  String out = input;
  out.replace("\r", " ");
  out.replace("\n", " ");
  out.trim();
  if (out.length() > maxLen) {
    out = out.substring(0, maxLen);
  }
  return out;
}

void loadQuota() {
  Preferences prefs;
  prefs.begin("quota", true);
  quotaPlan = prefs.getString("plan", "plus");
  quotaH5 = prefs.getInt("h5", -1);
  quotaH5Reset = prefs.getString("h5r", "");
  quotaWk = prefs.getInt("wk", -1);
  quotaWkReset = prefs.getString("wkr", "");
  quotaUpdated = prefs.getString("upd", "");
  quotaUpdatedEpoch = prefs.getULong("updts", 0);
  prefs.end();
}

void saveQuota() {
  Preferences prefs;
  prefs.begin("quota", false);
  prefs.putString("plan", quotaPlan);
  prefs.putInt("h5", quotaH5);
  prefs.putString("h5r", quotaH5Reset);
  prefs.putInt("wk", quotaWk);
  prefs.putString("wkr", quotaWkReset);
  prefs.putString("upd", quotaUpdated);
  prefs.putULong("updts", quotaUpdatedEpoch);
  prefs.end();
}

void handleRoot() {
  String page;
  page.reserve(2048);
  page += F("<!doctype html><html><head><meta charset='utf-8'>");
  page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>Smart Calendar</title>");
  page += F("<style>body{font-family:system-ui,sans-serif;margin:20px;max-width:560px;}"
            "h1{font-size:20px}h2{font-size:17px;margin-top:24px}"
            ".row{display:flex;align-items:center;gap:8px;"
            "margin:10px 0}input[type=text]{flex:1;min-width:0;padding:8px;"
            "font-size:16px}input[type=checkbox]{width:22px;height:22px}"
            "button{padding:10px 18px;font-size:16px;margin-top:8px}"
            ".note{color:#555;font-size:13px;line-height:1.4}</style></head><body>");
  page += F("<h1>Smart Calendar - Today</h1>");
  page += F("<p class='note'>E-paper text is ASCII only in this experiment; "
            "Chinese characters will show as '?'. Max 40 characters each.</p>");
  page += F("<form method='POST' action='/save'>");
  for (int i = 0; i < kMaxTodos; ++i) {
    char name[16];
    snprintf(name, sizeof(name), "todo%d", i);
    char doneName[16];
    snprintf(doneName, sizeof(doneName), "done%d", i);
    page += F("<div class='row'><input type='checkbox' name='");
    page += doneName;
    page += F("'");
    if (todoDone[i]) page += F(" checked");
    page += F("><input type='text' name='");
    page += name;
    page += F("' maxlength='40' value='");
    page += htmlEscape(todoText[i]);
    page += F("'></div>");
  }
  page += F("<button type='submit'>Save</button></form>");
  page += F("<h2>ChatGPT Plus quota</h2>");
  if (quotaH5 < 0 || quotaWk < 0) {
    page += F("<p>No quota data yet. Run <code>tools/codex_quota_push.py</code> "
              "on the computer.</p>");
  } else {
    const String h5r = htmlEscape(quotaH5Reset);
    const String wkr = htmlEscape(quotaWkReset);
    const String upd = htmlEscape(quotaUpdated);
    char qbuf[256];
    snprintf(qbuf, sizeof(qbuf),
             "<p>5h: %d%% (resets %s)<br>Week: %d%% (resets %s)<br>"
             "Updated: %s</p>",
             quotaH5, h5r.c_str(), quotaWk, wkr.c_str(), upd.c_str());
    page += qbuf;
  }
  page += F("<p class='note'>Auto-updated by the local Codex quota helper; "
            "the OpenAI token stays on the computer.</p></body></html>");
  server.send(200, "text/html; charset=utf-8", page);
}

void handleSave() {
  for (int i = 0; i < kMaxTodos; ++i) {
    char name[16];
    snprintf(name, sizeof(name), "todo%d", i);
    String text = server.arg(name);
    text.replace("\r", " ");
    text.replace("\n", " ");
    text.trim();
    if (text.length() > kMaxTodoLen) {
      text = text.substring(0, kMaxTodoLen);
    }
    todoText[i] = text;

    snprintf(name, sizeof(name), "done%d", i);
    todoDone[i] = server.hasArg(name);
  }
  saveTodos();
  todosDirty = true;
  server.sendHeader("Location", "/", true);
  server.send(303, "text/plain", "");
}

void handleQuota() {
  const String plan = cleanShort(server.arg("plan"), 12);
  if (plan.length() > 0) {
    quotaPlan = plan;
  }
  if (server.hasArg("h5")) {
    quotaH5 = constrain(server.arg("h5").toInt(), 0, 100);
  }
  if (server.hasArg("wk")) {
    quotaWk = constrain(server.arg("wk").toInt(), 0, 100);
  }
  if (server.hasArg("h5r")) {
    quotaH5Reset = cleanShort(server.arg("h5r"), 12);
  }
  if (server.hasArg("wkr")) {
    quotaWkReset = cleanShort(server.arg("wkr"), 12);
  }
  if (server.hasArg("upd")) {
    quotaUpdated = cleanShort(server.arg("upd"), 8);
  }
  if (server.hasArg("upd_ts")) {
    quotaUpdatedEpoch = static_cast<uint32_t>(
        strtoul(server.arg("upd_ts").c_str(), nullptr, 10));
  }
  saveQuota();
  quotaDirty = true;
  server.send(200, "text/plain", "OK");
}

void startWebServer() {
  if (webServerStarted) return;
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/quota", HTTP_POST, handleQuota);
  server.begin();
  webServerStarted = true;
  Serial.printf("Web UI: http://%s/\n", WiFi.localIP().toString().c_str());
  Serial.println("Quota endpoint: POST /quota");
}

void drawTime(const struct tm& t) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
  drawCentered(display, buf, TIME_BASELINE, &FreeSansBold18pt7b);
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
  first.tm_isdst = -1;
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
    display.setCursor(x + (CELL_W - tbw) / 2 - tbx, y + 14);
    display.print(dayBuf);
    display.setTextColor(GxEPD_BLACK);
  }
}

void drawTodos() {
  display.setFont(&FreeSansBold9pt7b);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(12, TODOS_TITLE_BASELINE);
  display.print("TODAY");

  display.setFont(&FreeSans9pt7b);
  int line = 0;
  for (int i = 0; i < kMaxTodos; ++i) {
    if (todoText[i].length() == 0) continue;
    const int y = TODO_BASELINES[line];
    const int box = 8;
    if (todoDone[i]) {
      display.fillRect(12, y - 9, box, box, GxEPD_BLACK);
    } else {
      display.drawRect(12, y - 9, box, box, GxEPD_BLACK);
    }

    String text = todoText[i];
    for (size_t j = 0; j < text.length(); ++j) {
      if (static_cast<uint8_t>(text[j]) >= 0x80) {
        text[j] = '?';
      }
    }
    display.setCursor(26, y);
    display.print(text);

    if (todoDone[i]) {
      int16_t tbx, tby;
      uint16_t tbw, tbh;
      display.getTextBounds(text, 0, 0, &tbx, &tby, &tbw, &tbh);
      display.drawLine(26, y - 4, 26 + tbw, y - 4, GxEPD_BLACK);
    }
    ++line;
  }

  if (line == 0) {
    display.setCursor(12, TODO_BASELINES[0]);
    display.print("No todos - open Web UI");
  }
}

void drawQuotaPanel() {
  display.setFont(&FreeSansBold9pt7b);
  display.setTextColor(GxEPD_BLACK);
  String title = "CHATGPT";
  if (quotaPlan.length() > 0) {
    String plan = quotaPlan;
    plan.toUpperCase();
    title += " ";
    title += plan;
  }
  display.setCursor(QUOTA_TEXT_X, QUOTA_TITLE_Y);
  display.print(title);

  display.setFont(&FreeSans9pt7b);
  if (quotaH5 < 0 || quotaWk < 0) {
    display.setCursor(QUOTA_TEXT_X, QUOTA_LINE1_Y);
    display.print("no data");
    display.setCursor(QUOTA_TEXT_X, QUOTA_LINE2_Y);
    display.print("run helper");
    return;
  }

  char buf[32];
  snprintf(buf, sizeof(buf), "5h %3d%% %s", quotaH5, quotaH5Reset.c_str());
  display.setCursor(QUOTA_TEXT_X, QUOTA_LINE1_Y);
  display.print(buf);

  snprintf(buf, sizeof(buf), "wk %3d%% %s", quotaWk, quotaWkReset.c_str());
  display.setCursor(QUOTA_TEXT_X, QUOTA_LINE2_Y);
  display.print(buf);

  snprintf(buf, sizeof(buf), "upd %s", quotaUpdated.c_str());
  display.setCursor(QUOTA_TEXT_X, QUOTA_LINE3_Y);
  display.print(buf);

  if (quotaUpdatedEpoch > 0 &&
      time(nullptr) - static_cast<time_t>(quotaUpdatedEpoch) > 6 * 3600) {
    display.setCursor(QUOTA_TEXT_X, QUOTA_LINE4_Y);
    display.print("stale");
  }
}

void drawStatus() {
  char buf[48];
  if (WiFi.status() == WL_CONNECTED) {
    snprintf(buf, sizeof(buf), "Web UI: %s",
             WiFi.localIP().toString().c_str());
  } else {
    snprintf(buf, sizeof(buf), "Wi-Fi offline - retrying");
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
    drawQuotaPanel();
    display.drawLine(QUOTA_SEPARATOR_X, GRID_Y, QUOTA_SEPARATOR_X,
                     GRID_Y + GRID_ROWS * CELL_H, GxEPD_BLACK);
    drawTodos();
    drawStatus();
  } while (display.nextPage());

  lastMinute = t.tm_min;
  lastDay = t.tm_mday;
  lastFullRefreshMs = millis();
  mainScreenVisible = true;
  quotaDirty = false;
}

void drawTimePartial(const struct tm& t) {
  display.setPartialWindow(0, TIME_PARTIAL_Y, epd::WIDTH, TIME_PARTIAL_H);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawTime(t);
  } while (display.nextPage());

  lastMinute = t.tm_min;
}

void drawQuotaPartial() {
  display.setPartialWindow(QUOTA_PARTIAL_X, QUOTA_PARTIAL_Y, QUOTA_PARTIAL_W,
                           QUOTA_PARTIAL_H);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawQuotaPanel();
  } while (display.nextPage());
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
  mainScreenVisible = false;
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
  mainScreenVisible = false;
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
    if (wifiConnected) {
      char ipbuf[40];
      snprintf(ipbuf, sizeof(ipbuf), "Web UI: %s",
               WiFi.localIP().toString().c_str());
      drawCentered(display, ipbuf, 260, &FreeSans9pt7b);
    }
  } while (display.nextPage());
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("==========================================");
  Serial.println(" Smart Calendar - Experiment 05");
  Serial.println(" Wi-Fi + NTP + calendar + todos + ChatGPT quota");
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

  loadTodos();
  loadQuota();
  drawConnectingScreen();

  const bool wifiConnected = connectWiFi();
  if (wifiConnected) {
    startWebServer();
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
  if (webServerStarted) {
    server.handleClient();
  }

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
        startWebServer();
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

  if (todosDirty) {
    todosDirty = false;
    drawFullScreen(t);
  } else if (quotaDirty && mainScreenVisible) {
    quotaDirty = false;
    drawQuotaPartial();
  } else if (t.tm_mday != lastDay ||
             now - lastFullRefreshMs >= FULL_REFRESH_MS) {
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
