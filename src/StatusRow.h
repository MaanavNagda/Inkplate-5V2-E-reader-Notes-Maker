#ifndef STATUS_ROW_H
#define STATUS_ROW_H

#include <cstdint>

class Inkplate;

// Bottom-left status row: battery icon + level, 24h time and date.
//
// Polls the RTC once a second and the battery every 30 s, and only asks for a
// re-render when something actually changed (minute/day rollover or a battery
// level change). Also enforces a full refresh every 10 minutes; the timer is
// reset whenever any full refresh happens.
class StatusRow {
public:
    // Call from update() while the row is on screen. Returns true when the
    // screen should be re-rendered; check fullRefreshDue() to tell a timed
    // full refresh apart from a content change.
    bool poll(Inkplate& display);

    // True while the 10-minute full refresh is pending.
    bool fullRefreshDue() const { return forceFull_; }

    // Call after every full display refresh (any reason) to reset the timer.
    void noteFullRefresh();

    // Draw battery icon + %, HH:MM and DD/MM/YYYY at the bottom-left.
    // fillStrip paints a background strip first so the row stays readable over
    // page content (e.g. the textbook page image).
    void draw(Inkplate& display, bool darkMode, bool fillStrip);

private:
    void drawBatteryIcon(Inkplate& display, int16_t x, int16_t y, int pct, uint16_t fg);

    uint32_t lastStatusCheckMs_ = 0;
    uint32_t lastBatteryCheckMs_ = 0;
    uint32_t lastFullRefreshMs_ = 0;
    bool started_ = false;
    int hour_ = -1, minute_ = -1, day_ = -1, month_ = -1, year_ = -1;
    int batteryPct_ = -1;
    bool rtcOk_ = false;
    bool forceFull_ = false;
};

#endif
