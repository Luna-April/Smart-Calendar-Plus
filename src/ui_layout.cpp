#include "ui_layout.h"

#include <Fonts/FreeSans9pt7b.h>
#include <time.h>

namespace {

constexpr int16_t OUTER_X = 2;
constexpr int16_t OUTER_Y = 2;
constexpr int16_t OUTER_W = 396;
constexpr int16_t OUTER_H = 296;
constexpr int16_t HEADER_LINE_Y = 30;

constexpr int16_t LEFT_X = 8;
constexpr int16_t LEFT_W = 182;
constexpr int16_t RIGHT_X = 198;
constexpr int16_t RIGHT_W = 194;

constexpr int16_t TIME_Y = 34;
constexpr int16_t TIME_H = 78;
constexpr int16_t TODO_Y = 118;
constexpr int16_t TODO_H = 110;
constexpr int16_t CAL_Y = 34;
constexpr int16_t CAL_H = 194;
constexpr int16_t AI_Y = 234;
constexpr int16_t AI_H = 60;

constexpr int16_t TIME_PARTIAL_X = 8;
constexpr int16_t TIME_PARTIAL_Y = 32;
constexpr int16_t TIME_PARTIAL_W = 184;
constexpr int16_t TIME_PARTIAL_H = 80;

const char* const WEEKDAY_SHORT[] = {"周日", "周一", "周二", "周三",
                                     "周四", "周五", "周六"};
const char* const WEEKDAY_HEADER[] = {"一", "二", "三", "四", "五", "六", "日"};

}  // namespace

UiLayout::UiLayout(SmartDisplay& display) : display_(display) {}

void UiLayout::begin() {
  u8g2_.begin(display_);
  u8g2_.setFontMode(1);  // transparent background
  u8g2_.setFontDirection(0);
  u8g2_.setForegroundColor(GxEPD_BLACK);
  u8g2_.setBackgroundColor(GxEPD_WHITE);
}

void UiLayout::setFont16() { u8g2_.setFont(u8g2_font_wqy16_t_gb2312); }
void UiLayout::setFont42() { u8g2_.setFont(u8g2_font_fub42_tn); }

void UiLayout::drawText16(int16_t x, int16_t baseline, const String& text) {
  setFont16();
  u8g2_.setForegroundColor(GxEPD_BLACK);
  u8g2_.drawUTF8(x, baseline, text.c_str());
}

void UiLayout::drawCentered16(int16_t x, int16_t w, int16_t baseline,
                              const String& text) {
  setFont16();
  const int16_t tw = static_cast<int16_t>(u8g2_.getUTF8Width(text.c_str()));
  u8g2_.setForegroundColor(GxEPD_BLACK);
  u8g2_.drawUTF8(x + (w - tw) / 2, baseline, text.c_str());
}

void UiLayout::drawRight16(int16_t right, int16_t baseline, const String& text) {
  setFont16();
  const int16_t tw = static_cast<int16_t>(u8g2_.getUTF8Width(text.c_str()));
  u8g2_.setForegroundColor(GxEPD_BLACK);
  u8g2_.drawUTF8(right - tw, baseline, text.c_str());
}

void UiLayout::drawCentered42(int16_t x, int16_t w, int16_t baseline,
                              const String& text) {
  setFont42();
  const int16_t tw = static_cast<int16_t>(u8g2_.getUTF8Width(text.c_str()));
  u8g2_.setForegroundColor(GxEPD_BLACK);
  u8g2_.drawUTF8(x + (w - tw) / 2, baseline, text.c_str());
}

void UiLayout::drawRight9(int16_t right, int16_t baseline, const String& text) {
  display_.setFont(&FreeSans9pt7b);
  display_.setTextColor(GxEPD_BLACK);
  int16_t tbx, tby;
  uint16_t tbw, tbh;
  display_.getTextBounds(text, 0, 0, &tbx, &tby, &tbw, &tbh);
  display_.setCursor(right - static_cast<int16_t>(tbw), baseline);
  display_.print(text);
}

String UiLayout::fit16(const String& text, int16_t maxWidth) {
  setFont16();
  if (u8g2_.getUTF8Width(text.c_str()) <= maxWidth) {
    return text;
  }

  String out;
  for (size_t i = 0; i < text.length();) {
    const uint8_t c = text[i];
    size_t len = 1;
    if ((c & 0xE0) == 0xC0) {
      len = 2;
    } else if ((c & 0xF0) == 0xE0) {
      len = 3;
    } else if ((c & 0xF8) == 0xF0) {
      len = 4;
    }
    if (i + len > text.length()) {
      break;
    }
    String candidate = out;
    candidate += text.substring(i, i + len);
    if (u8g2_.getUTF8Width((candidate + "..").c_str()) > maxWidth) {
      break;
    }
    out = candidate;
    i += len;
  }
  return out + "..";
}

int UiLayout::daysInMonth(int year, int month) {
  static const uint8_t days[] = {31, 28, 31, 30, 31, 30,
                                 31, 31, 30, 31, 30, 31};
  if (month == 2 &&
      ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) {
    return 29;
  }
  return days[month - 1];
}

void UiLayout::drawCardFrame(int16_t x, int16_t y, int16_t w, int16_t h) {
  display_.drawRect(x, y, w, h, GxEPD_BLACK);
}

void UiLayout::drawOuterFrame() {
  display_.drawRect(OUTER_X, OUTER_Y, OUTER_W, OUTER_H, GxEPD_BLACK);
}

void UiLayout::drawHeader(const UiData& data) {
  display_.drawLine(OUTER_X, HEADER_LINE_Y, OUTER_X + OUTER_W, HEADER_LINE_Y,
                    GxEPD_BLACK);

  // Simple line-art calendar icon.
  display_.drawRect(10, 8, 16, 14, GxEPD_BLACK);
  display_.drawLine(10, 12, 26, 12, GxEPD_BLACK);
  display_.drawLine(13, 5, 13, 9, GxEPD_BLACK);
  display_.drawLine(23, 5, 23, 9, GxEPD_BLACK);

  drawText16(32, 23, "Smart Calendar");

  String status = data.wifiOk ? data.ip : String("Wi-Fi offline");
  if (status.length() == 0) {
    status = data.wifiOk ? String("Wi-Fi OK") : String("Wi-Fi offline");
  }
  drawRight9(388, 22, status);
}

void UiLayout::drawTimeCard(const UiData& data) {
  drawCardFrame(LEFT_X, TIME_Y, LEFT_W, TIME_H);

  char timeBuf[8];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", data.hour, data.minute);
  drawCentered42(LEFT_X, LEFT_W, 82, timeBuf);

  display_.drawLine(LEFT_X + 4, 90, LEFT_X + LEFT_W - 4, 90, GxEPD_BLACK);

  char dateBuf[64];
  snprintf(dateBuf, sizeof(dateBuf), "%04d年%d月%d日 %s", data.year, data.month,
           data.day, WEEKDAY_SHORT[data.weekday]);
  drawCentered16(LEFT_X, LEFT_W, 106, dateBuf);
}

void UiLayout::drawTodoCard(const UiData& data) {
  drawCardFrame(LEFT_X, TODO_Y, LEFT_W, TODO_H);

  // Simple checklist icon next to the card title.
  display_.drawRect(12, 122, 12, 12, GxEPD_BLACK);
  display_.drawLine(14, 128, 17, 131, GxEPD_BLACK);
  display_.drawLine(17, 131, 22, 125, GxEPD_BLACK);

  drawText16(30, 136, "今日待办");
  display_.drawLine(12, 140, LEFT_X + LEFT_W - 4, 140, GxEPD_BLACK);

  int line = 0;
  for (int i = 0; i < 3; ++i) {
    if (data.todos[i].text.length() == 0) {
      continue;
    }
    const int16_t y = 162 + line * 24;
    display_.drawRect(12, y - 12, 10, 10, GxEPD_BLACK);
    if (data.todos[i].done) {
      display_.fillRect(13, y - 11, 8, 8, GxEPD_BLACK);
    }
    drawText16(28, y, fit16(data.todos[i].text, 150));
    ++line;
  }

  if (line == 0) {
    drawText16(28, 162, "暂无待办");
  }
}

void UiLayout::drawCalendarCard(const UiData& data) {
  drawCardFrame(RIGHT_X, CAL_Y, RIGHT_W, CAL_H);

  char monthBuf[24];
  snprintf(monthBuf, sizeof(monthBuf), "%04d年%d月", data.year, data.month);
  drawCentered16(RIGHT_X, RIGHT_W, 54, monthBuf);
  drawText16(RIGHT_X + 6, 54, "<");
  drawRight16(RIGHT_X + RIGHT_W - 6, 54, ">");
  display_.drawLine(RIGHT_X + 6, 58, RIGHT_X + RIGHT_W - 6, 58, GxEPD_BLACK);

  const int16_t gridX = RIGHT_X + 6;
  const int16_t cellW = 26;
  for (int col = 0; col < 7; ++col) {
    drawCentered16(gridX + col * cellW, cellW, 76, WEEKDAY_HEADER[col]);
  }
  display_.drawLine(gridX, 82, gridX + 7 * cellW, 82, GxEPD_BLACK);

  struct tm first = {};
  first.tm_year = data.year - 1900;
  first.tm_mon = data.month - 1;
  first.tm_mday = 1;
  first.tm_hour = 12;
  first.tm_isdst = -1;
  const time_t firstTime = mktime(&first);
  localtime_r(&firstTime, &first);
  const int startCol = (first.tm_wday + 6) % 7;
  const int days = daysInMonth(data.year, data.month);

  for (int day = 1; day <= days; ++day) {
    const int index = startCol + day - 1;
    const int row = index / 7;
    const int col = index % 7;
    if (row >= 6) {
      break;
    }
    const int16_t cellX = gridX + col * cellW;
    const int16_t baseline = 100 + row * 21;
    char dayBuf[4];
    snprintf(dayBuf, sizeof(dayBuf), "%d", day);

    if (day == data.day) {
      const int16_t centerX = cellX + cellW / 2;
      // Black outline + white interior keeps the number readable while
      // avoiding a large black block on e-paper.
      display_.fillRect(centerX - 10, baseline - 14, 20, 18, GxEPD_BLACK);
      display_.fillRect(centerX - 9, baseline - 13, 18, 16, GxEPD_WHITE);
      drawCentered16(cellX, cellW, baseline, dayBuf);
    } else {
      drawCentered16(cellX, cellW, baseline, dayBuf);
    }
  }
}

void UiLayout::drawProgressRow(int16_t baseline, const String& label,
                               int percent, bool hasData) {
  drawText16(12, baseline, label);

  const int16_t barX = 70;
  const int16_t barY = baseline - 10;
  const int16_t barW = 250;
  const int16_t barH = 8;
  display_.drawRect(barX, barY, barW, barH, GxEPD_BLACK);
  if (hasData) {
    percent = constrain(percent, 0, 100);
    const int16_t fillW = (barW - 2) * percent / 100;
    if (fillW > 0) {
      display_.fillRect(barX + 1, barY + 1, fillW, barH - 2, GxEPD_BLACK);
    }
  }

  char pctBuf[8];
  if (hasData) {
    snprintf(pctBuf, sizeof(pctBuf), "%d%%", percent);
  } else {
    snprintf(pctBuf, sizeof(pctBuf), "--");
  }
  drawRight16(386, baseline, pctBuf);
}

void UiLayout::drawAIUsageCard(const UiData& data) {
  drawCardFrame(8, AI_Y, 384, AI_H);

  drawText16(12, 250, "AI 剩余");
  drawRight16(386, 250, data.aiSource);
  display_.drawLine(12, 254, 388, 254, GxEPD_BLACK);

  drawProgressRow(270, "5 小时", data.h5Percent, data.hasQuota);
  drawProgressRow(288, "本周", data.weekPercent, data.hasQuota);
}

void UiLayout::drawMainUI(const UiData& data) {
  display_.setFullWindow();
  display_.firstPage();
  do {
    display_.fillScreen(GxEPD_WHITE);
    drawOuterFrame();
    drawHeader(data);
    drawTimeCard(data);
    drawTodoCard(data);
    drawCalendarCard(data);
    drawAIUsageCard(data);
  } while (display_.nextPage());
}

void UiLayout::drawTimeCardPartial(const UiData& data) {
  display_.setPartialWindow(TIME_PARTIAL_X, TIME_PARTIAL_Y, TIME_PARTIAL_W,
                            TIME_PARTIAL_H);
  display_.firstPage();
  do {
    display_.fillScreen(GxEPD_WHITE);
    drawTimeCard(data);
  } while (display_.nextPage());
}

void UiLayout::drawAIUsageCardPartial(const UiData& data) {
  display_.setPartialWindow(8, 232, 384, 64);
  display_.firstPage();
  do {
    display_.fillScreen(GxEPD_WHITE);
    drawAIUsageCard(data);
  } while (display_.nextPage());
}

void UiLayout::drawSetupScreen(const String& apSsid, const String& apPassword,
                               const String& apIp, const String& reason) {
  display_.setFullWindow();
  display_.firstPage();
  do {
    display_.fillScreen(GxEPD_WHITE);
    drawOuterFrame();
    UiData header;
    header.wifiOk = true;
    header.ip = apIp;
    drawHeader(header);

    drawText16(12, 62, "Wi-Fi 设置");
    drawText16(12, 92, reason);
    drawText16(12, 124, "1. 手机连接 Wi-Fi:");
    drawText16(30, 146, apSsid);
    drawText16(30, 166, "密码: " + apPassword);
    drawText16(12, 194, "2. 打开浏览器:");
    drawText16(30, 216, "http://" + apIp);
    drawText16(12, 244, "3. 选择 2.4G Wi-Fi 并输入密码");
    drawText16(12, 270, "仅支持 2.4 GHz；保存后自动重启");
  } while (display_.nextPage());
}
