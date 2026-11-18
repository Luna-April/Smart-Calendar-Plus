#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <time.h>

#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <GxEPD2_BW.h>

#include "epd_config.h"
#include "ui_layout.h"

// Smart Calendar - Wi-Fi onboarding, NTP, calendar, todos and ChatGPT quota.
//
// First boot (or failed Wi-Fi) starts a setup AP + captive portal. Credentials
// are stored in NVS, so the compiled image has no hard-coded Wi-Fi secrets.
// A local helper can optionally POST the Codex 5h/week remaining quota to
// /quota; the token stays on the PC.

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
constexpr uint32_t WIFI_SETUP_FALLBACK_MS = 120000;  // 2 min offline -> setup
constexpr int PIN_BOOT_BUTTON = 9;  // ESP32-C3 SuperMini BOOT, active low

constexpr int kMaxTodos = 3;
constexpr size_t kMaxTodoChars = 40;

GxEPD2_BW<epd::Driver, epd::HEIGHT> display(
    epd::Driver(epd::PIN_CS, epd::PIN_DC, epd::PIN_RST, epd::PIN_BUSY));

UiLayout ui(display);

WebServer server(80);
WebServer setupServer(80);
DNSServer dnsServer;
bool setupMode = false;
bool setupHandlersRegistered = false;
String apSsid;
String apPassword = "smartcalendar";
String wifiSsid;
String wifiPassword;

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

String truncateUtf8(const String& input, size_t maxChars) {
  size_t chars = 0;
  size_t i = 0;
  while (i < input.length() && chars < maxChars) {
    const uint8_t c = input[i];
    size_t len = 1;
    if ((c & 0xE0) == 0xC0) {
      len = 2;
    } else if ((c & 0xF0) == 0xE0) {
      len = 3;
    } else if ((c & 0xF8) == 0xF0) {
      len = 4;
    }
    if (i + len > input.length()) {
      break;
    }
    i += len;
    ++chars;
  }
  return input.substring(0, i);
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

void loadWifiCredentials() {
  Preferences prefs;
  prefs.begin("wifi", true);
  wifiSsid = prefs.getString("ssid", "");
  wifiPassword = prefs.getString("pass", "");
  prefs.end();
}

void saveWifiCredentials(const String& ssid, const String& password) {
  Preferences prefs;
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", password);
  prefs.end();
  wifiSsid = ssid;
  wifiPassword = password;
}

void clearWifiCredentials() {
  Preferences prefs;
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.end();
  wifiSsid = "";
  wifiPassword = "";
}

void handleRoot() {
  String page;
  page.reserve(2560);
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
  page += F("<h1>Smart Calendar - 今日待办</h1>");
  page += F("<p class='note'>最多 3 条，每条 40 个字符；支持中文（GB2312），"
            "emoji 可能无法显示。</p>");
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
  page += F("<h2>ChatGPT Plus 额度</h2>");
  if (quotaH5 < 0 || quotaWk < 0) {
    page += F("<p>暂无额度数据，请在电脑上运行 "
              "<code>tools/codex_quota_push.py</code>。</p>");
  } else {
    const String h5r = htmlEscape(quotaH5Reset);
    const String wkr = htmlEscape(quotaWkReset);
    const String upd = htmlEscape(quotaUpdated);
    char qbuf[256];
    snprintf(qbuf, sizeof(qbuf),
             "<p>5 小时: %d%% (重置 %s)<br>本周: %d%% (重置 %s)<br>"
             "更新: %s</p>",
             quotaH5, h5r.c_str(), quotaWk, wkr.c_str(), upd.c_str());
    page += qbuf;
  }
  page += F("<p class='note'>由本机 Codex helper 自动更新；"
            "token 只留在电脑上。</p>");
  page += F("<h2>设备设置</h2>");
  page += F("<form method='POST' action='/resetwifi' "
            "onsubmit=\"return confirm('清除 Wi-Fi 并重启？')\">"
            "<button type='submit'>重新配置 Wi-Fi</button></form>");
  page += F("<p class='note'>也可以长按板载 BOOT 键 5 秒。</p></body></html>");
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
    todoText[i] = truncateUtf8(text, kMaxTodoChars);

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

void handleResetWifi() {
  server.send(200, "text/html; charset=utf-8",
              "<meta charset='utf-8'>Wi-Fi 已清除，设备正在重启...");
  delay(500);
  clearWifiCredentials();
  ESP.restart();
}

void startWebServer() {
  if (webServerStarted) return;
  setupServer.stop();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/quota", HTTP_POST, handleQuota);
  server.on("/resetwifi", HTTP_POST, handleResetWifi);
  server.begin();
  webServerStarted = true;
  Serial.printf("Web UI: http://%s/\n", WiFi.localIP().toString().c_str());
  Serial.println("Quota endpoint: POST /quota");
}

void handleSetupRoot() {
  Serial.println("Setup page requested");
  String page;
  page.reserve(8192);
  page += F("<!doctype html><html><head><meta charset='utf-8'>");
  page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>Smart Calendar 配网</title>");
  page += F("<style>body{font-family:system-ui,sans-serif;margin:20px;max-width:560px;}"
            "h1{font-size:20px}label{display:block;margin-top:14px;font-size:14px}"
            "select,input{width:100%;box-sizing:border-box;padding:8px;font-size:16px}"
            "button{padding:10px 18px;font-size:16px;margin-top:16px}"
            ".note{color:#555;font-size:13px;line-height:1.4}</style></head><body>");
  page += F("<h1>Smart Calendar 配网</h1>");
  page += F("<p class='note'>热点：");
  page += htmlEscape(apSsid);
  page += F("<br>热点密码：");
  page += htmlEscape(apPassword);
  page += F("<br>打开 <b>http://192.168.4.1</b>；仅支持 2.4 GHz Wi-Fi。</p>");
  page += F("<form method='POST' action='/connect'>");
  page += F("<label>选择 Wi-Fi</label><select name='ssid_sel'>"
            "<option value=''>-- 选择 --</option>");

  const int n = WiFi.scanNetworks();
  Serial.printf("Setup scan: %d networks\n", n);
  for (int i = 0; i < n; ++i) {
    const String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) {
      continue;
    }
    bool duplicate = false;
    for (int j = 0; j < i; ++j) {
      if (WiFi.SSID(j) == ssid) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      continue;
    }
    page += F("<option value='");
    page += htmlEscape(ssid);
    page += F("'>");
    page += htmlEscape(ssid);
    page += F(" (");
    page += String(WiFi.RSSI(i));
    page += F(" dBm)</option>");
  }

  page += F("</select>");
  page += F("<label>或手动输入 SSID</label>"
            "<input type='text' name='ssid' maxlength='32' "
            "placeholder='隐藏网络时使用'>");
  page += F("<label>密码</label>"
            "<input type='password' name='password' maxlength='64'>");
  page += F("<button type='submit'>连接</button></form>");
  page += F("<p><a href='/'>重新扫描</a></p>");
  page += F("<p class='note'>保存后设备会重启并连接；若失败，重新连接本热点并打开 "
            "http://192.168.4.1。</p></body></html>");
  setupServer.send(200, "text/html; charset=utf-8", page);
}

void handleSetupConnect() {
  String ssid = setupServer.arg("ssid");
  ssid.trim();
  if (ssid.length() == 0) {
    ssid = setupServer.arg("ssid_sel");
    ssid.trim();
  }
  String password = setupServer.arg("password");

  if (ssid.length() == 0) {
    setupServer.send(200, "text/html; charset=utf-8",
                     "<meta charset='utf-8'>未选择 Wi-Fi。"
                     "<a href='/'>返回</a>");
    return;
  }
  if (ssid.length() > 32) {
    ssid = ssid.substring(0, 32);
  }
  if (password.length() > 64) {
    password = password.substring(0, 64);
  }
  saveWifiCredentials(ssid, password);
  Serial.println("Setup credentials saved, restarting");

  String page;
  page.reserve(512);
  page += F("<!doctype html><html><head><meta charset='utf-8'>");
  page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>已保存</title></head>"
            "<body style='font-family:system-ui;margin:20px'>");
  page += F("<h1>已保存</h1><p>设备正在重启并连接 Wi-Fi...</p>");
  page += F("<p>如果 30 秒后仍未连接，请重新连接热点 <b>");
  page += htmlEscape(apSsid);
  page += F("</b>（密码 ");
  page += htmlEscape(apPassword);
  page += F("）并打开 <b>http://192.168.4.1</b>。</p></body></html>");
  setupServer.send(200, "text/html; charset=utf-8", page);
  delay(1500);
  ESP.restart();
}

void handleSetupNotFound() {
  setupServer.sendHeader("Location", "http://192.168.4.1/", true);
  setupServer.send(302, "text/plain", "");
}

String makeApSsid() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  const String suffix =
      mac.length() >= 4 ? mac.substring(mac.length() - 4) : String("0000");
  return "SmartCalendar-" + suffix;
}

void startSetupPortal(const String& reason) {
  if (setupMode) {
    return;
  }
  setupMode = true;
  webServerStarted = false;
  mainScreenVisible = false;
  server.stop();

  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP_STA);
  // This board is unstable at 19.5 dBm; 11 dBm is the known-good TX power.
  WiFi.setTxPower(WIFI_POWER_11dBm);
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  apSsid = makeApSsid();
  const bool apStarted =
      WiFi.softAP(apSsid.c_str(), apPassword.c_str(), 6, 0, 4);
  Serial.printf("softAP: started=%d, SSID=%s, IP=%s, mode=%d\n", apStarted,
                apSsid.c_str(), WiFi.softAPIP().toString().c_str(),
                static_cast<int>(WiFi.getMode()));
  dnsServer.start(53, "*", apIP);

  if (!setupHandlersRegistered) {
    setupServer.on("/", HTTP_GET, handleSetupRoot);
    setupServer.on("/connect", HTTP_POST, handleSetupConnect);
    setupServer.onNotFound(handleSetupNotFound);
    setupHandlersRegistered = true;
  }
  setupServer.begin();

  ui.drawSetupScreen(apSsid, apPassword, "192.168.4.1", reason);
  Serial.printf("Setup AP: %s / http://192.168.4.1\n", apSsid.c_str());
  Serial.printf("Setup reason: %s\n", reason.c_str());
}

void checkBootButton() {
  static uint32_t pressedAt = 0;
  if (digitalRead(PIN_BOOT_BUTTON) == LOW) {
    if (pressedAt == 0) {
      pressedAt = millis();
    }
    if (millis() - pressedAt > 5000) {
      clearWifiCredentials();
      Serial.println("Wi-Fi reset by BOOT long press");
      ESP.restart();
    }
  } else {
    pressedAt = 0;
  }
}

UiData makeUiData(const struct tm& t) {
  UiData data;
  data.hour = t.tm_hour;
  data.minute = t.tm_min;
  data.year = t.tm_year + 1900;
  data.month = t.tm_mon + 1;
  data.day = t.tm_mday;
  data.weekday = t.tm_wday;

  for (int i = 0; i < kMaxTodos; ++i) {
    data.todos[i].text = todoText[i];
    data.todos[i].done = todoDone[i];
  }

  data.hasQuota = (quotaH5 >= 0 && quotaWk >= 0);
  // The quota helper already reports remaining percentages.
  data.h5Percent = data.hasQuota ? quotaH5 : 0;
  data.weekPercent = data.hasQuota ? quotaWk : 0;

  String plan = quotaPlan;
  plan.toUpperCase();
  data.aiSource =
      data.hasQuota
          ? (plan.length() > 0 ? "ChatGPT " + plan : String("ChatGPT"))
          : String("ChatGPT");

  data.wifiOk = (WiFi.status() == WL_CONNECTED);
  data.ip = data.wifiOk ? WiFi.localIP().toString() : String();
  return data;
}

void drawFullScreen(const struct tm& t) {
  ui.drawMainUI(makeUiData(t));
  lastMinute = t.tm_min;
  lastDay = t.tm_mday;
  lastFullRefreshMs = millis();
  mainScreenVisible = true;
  quotaDirty = false;
}

void drawTimePartial(const struct tm& t) {
  if (!mainScreenVisible) {
    return;
  }
  ui.drawTimeCardPartial(makeUiData(t));
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
  if (wifiSsid.length() == 0) {
    Serial.println("Wi-Fi: no credentials");
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
  const wl_status_t beginResult =
      WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
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
  pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);
  ui.begin();

  loadTodos();
  loadQuota();
  loadWifiCredentials();

  if (wifiSsid.length() == 0) {
    startSetupPortal("首次使用，请配置 Wi-Fi");
    return;
  }

  drawConnectingScreen();

  if (!connectWiFi()) {
    startSetupPortal("Wi-Fi 连接失败，请重新配置");
    return;
  }

  startWebServer();
  syncNtp();

  struct tm t;
  if (getLocalTime(&t, 0)) {
    drawFullScreen(t);
  } else {
    drawNoTimeScreen(true);
    lastMinute = -1;
    lastDay = -1;
  }

  lastWifiConnected = (WiFi.status() == WL_CONNECTED);
  lastWifiRetryMs = millis();
  lastNtpRetryMs = millis();
}

void loop() {
  checkBootButton();

  if (setupMode) {
    dnsServer.processNextRequest();
    setupServer.handleClient();
    delay(10);
    return;
  }

  if (webServerStarted) {
    server.handleClient();
  }

  const uint32_t now = millis();
  static uint32_t wifiDownSince = 0;

  // Wi-Fi reconnect.
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiDownSince == 0) {
      wifiDownSince = now;
    }
    if (now - wifiDownSince > WIFI_SETUP_FALLBACK_MS) {
      startSetupPortal("Wi-Fi 连接失败，请重新配置");
      return;
    }

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
        wifiDownSince = 0;
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
  wifiDownSince = 0;

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
    ui.drawAIUsageCardPartial(makeUiData(t));
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
