#include "StatusRow.h"

#include <Arduino.h>
#include <ctime>

#include "Inkplate.h"
#include "Fonts/FreeSans12pt7b.h"

namespace {
    constexpr uint32_t STATUS_CHECK_MS  = 1000;            // poll RTC once a second
    constexpr uint32_t BATTERY_CHECK_MS = 30 * 1000;       // poll battery every 30 s
    constexpr uint32_t FULL_REFRESH_MS  = 10 * 60 * 1000;  // full refresh every 10 min
    constexpr int16_t  MARGIN_X = 20;

    const GFXfont* const RowFont = &FreeSans12pt7b;
}

bool StatusRow::poll(Inkplate& display) {
    uint32_t now = millis();
    if (!started_) {
        lastFullRefreshMs_ = now;
        started_ = true;
    }

    bool needRender = false;

    if (now - lastStatusCheckMs_ >= STATUS_CHECK_MS) {
        lastStatusCheckMs_ = now;
        // isSet() and getEpoch() each perform one I2C transaction.
        rtcOk_ = display.rtc.isSet();
        uint32_t epoch = display.rtc.getEpoch();
        time_t t = (time_t)epoch;
        struct tm* ti = localtime(&t);
        if (ti && (ti->tm_min != minute_ || ti->tm_mday != day_)) {
            hour_   = ti->tm_hour;
            minute_ = ti->tm_min;
            day_    = ti->tm_mday;
            month_  = ti->tm_mon + 1;
            year_   = ti->tm_year + 1900;
            needRender = true;
        }
    }

    if (now - lastBatteryCheckMs_ >= BATTERY_CHECK_MS) {
        lastBatteryCheckMs_ = now;
        double v = display.readBattery();
        int pct = (int)((v - 3.3) / (4.2 - 3.3) * 100.0);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        if (pct != batteryPct_) {
            batteryPct_ = pct;
            needRender = true;
        }
    }

    if (now - lastFullRefreshMs_ >= FULL_REFRESH_MS) {
        forceFull_ = true;
        needRender = true;
    }

    return needRender;
}

void StatusRow::noteFullRefresh() {
    lastFullRefreshMs_ = millis();
    forceFull_ = false;
}

void StatusRow::draw(Inkplate& display, bool darkMode, bool fillStrip) {
    // Colour conventions differ between modes: in 1-bit any non-zero value is
    // black and 0 is white; in 3-bit the value is the grey level (0=black,
    // 7=white).
    bool is3bit = (display.getDisplayMode() == 1);
    uint16_t black = is3bit ? 0 : 7;
    uint16_t white = is3bit ? 7 : 0;
    uint16_t fg = darkMode ? white : black;
    uint16_t bg = darkMode ? black : white;

    int16_t baseY = display.height() - 30;
    if (fillStrip) {
        display.fillRect(0, baseY - 22, display.width(), 40, bg);
    }

    display.setFont(RowFont);
    display.setTextSize(1);
    display.setTextColor(fg, bg);

    int16_t x = MARGIN_X;
    char buf[48];

    // Battery icon + level.
    int batt = batteryPct_ < 0 ? 0 : batteryPct_;
    drawBatteryIcon(display, x, baseY - 14, batt, fg);
    x += 30;
    snprintf(buf, sizeof(buf), "%d%%", batt);
    display.setCursor(x, baseY);
    display.print(buf);
    x = display.getCursorX() + 22;

    // Time (24h).
    if (rtcOk_) snprintf(buf, sizeof(buf), "%02d:%02d", hour_, minute_);
    else        snprintf(buf, sizeof(buf), "--:--");
    display.setCursor(x, baseY);
    display.print(buf);
    x = display.getCursorX() + 22;

    // Date.
    if (rtcOk_) snprintf(buf, sizeof(buf), "%02d/%02d/%04d", day_, month_, year_);
    else        snprintf(buf, sizeof(buf), "--/--/----");
    display.setCursor(x, baseY);
    display.print(buf);
}

void StatusRow::drawBatteryIcon(Inkplate& display, int16_t x, int16_t y, int pct, uint16_t fg) {
    int16_t w = 22, h = 12;
    display.drawRect(x, y, w, h, fg);                  // outline
    display.fillRect(x + w, y + 3, 2, h - 6, fg);      // nub
    int16_t fillW = (int16_t)((w - 4) * (int32_t)pct / 100);
    if (fillW > 0) display.fillRect(x + 2, y + 2, fillW, h - 4, fg);
}
