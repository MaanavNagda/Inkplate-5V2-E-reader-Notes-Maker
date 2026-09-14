#ifndef SLEEP_MANAGER_H
#define SLEEP_MANAGER_H

#include <cstdint>
#include "screensaver.h"

class Inkplate;
class AppManager;

class SleepManager {
public:
    static constexpr uint32_t LIGHT_SLEEP_MS = 2UL * 60UL * 1000UL;
    static constexpr uint32_t DEEP_SLEEP_MS = 15UL * 60UL * 1000UL;
    // While light-sleeping, wake this often so the on-screen clock can refresh.
    static constexpr uint64_t LIGHT_SLEEP_WAKE_US = 30ULL * 1000ULL * 1000ULL;

    SleepManager(Inkplate& display, AppManager& appManager);

    void begin();
    void update(uint32_t nowMs, bool activity);

    // Manual deep-sleep trigger (can be bound to a menu item later).
    void enterDeepSleep();

    // Deep-sleep state, persisted in RTC slow memory.
    static bool wasDeepSleep();
    static uint32_t lastAppIdRaw(); // cast to AppId in the caller

private:
    void showScreensaverAndDeepSleep();
    void enterLightSleep();
    bool ensureScreensaverFile();

    Inkplate& display_;
    AppManager& appManager_;
    uint32_t lastActivityMs_ = 0;
    bool inLightSleep_ = false;
};

#endif
