#pragma once

#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "epd_config.h"

struct UiTodo {
  String text;
  bool done = false;
};

struct UiData {
  int hour = 21;
  int minute = 28;
  int year = 2026;
  int month = 11;
  int day = 12;
  int weekday = 4;  // 0 = Sunday

  UiTodo todos[3];

  // ChatGPT Plus 5h/week usage, computed from the quota helper's remaining
  // percentages. hasQuota is false when no helper data exists.
  int h5Percent = 0;
  int weekPercent = 0;
  bool hasQuota = false;
  String aiSource = "ChatGPT Plus";

  String ip;
  bool wifiOk = true;
};

using SmartDisplay = GxEPD2_BW<epd::Driver, epd::HEIGHT>;

class UiLayout {
 public:
  explicit UiLayout(SmartDisplay& display);

  void begin();
  void drawMainUI(const UiData& data);
  void drawTimeCardPartial(const UiData& data);
  void drawAIUsageCardPartial(const UiData& data);
  void drawSetupScreen(const String& apSsid, const String& apPassword,
                       const String& apIp, const String& reason);

 private:
  SmartDisplay& display_;
  U8G2_FOR_ADAFRUIT_GFX u8g2_;

  void setFont16();
  void setFont42();
  void drawOuterFrame();
  void drawHeader(const UiData& data);
  void drawTimeCard(const UiData& data);
  void drawTodoCard(const UiData& data);
  void drawCalendarCard(const UiData& data);
  void drawAIUsageCard(const UiData& data);
  void drawCardFrame(int16_t x, int16_t y, int16_t w, int16_t h);
  void drawText16(int16_t x, int16_t baseline, const String& text);
  void drawCentered16(int16_t x, int16_t w, int16_t baseline, const String& text);
  void drawRight16(int16_t right, int16_t baseline, const String& text);
  void drawCentered42(int16_t x, int16_t w, int16_t baseline, const String& text);
  void drawRight9(int16_t right, int16_t baseline, const String& text);
  void drawProgressRow(int16_t baseline, const String& label, int percent,
                       bool hasData);
  String fit16(const String& text, int16_t maxWidth);
  int daysInMonth(int year, int month);
};
