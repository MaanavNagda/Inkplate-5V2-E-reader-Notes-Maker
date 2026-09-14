#include "SleepManager.h"

#include <Arduino.h>
#include <esp_sleep.h>
#include <soc/i2s_struct.h>
#include "Inkplate.h"
#include "AppManager.h"
#include "App.h"

namespace {
    // These survive deep sleep but are not initialised by the bootloader.
    RTC_NOINIT_ATTR uint32_t sleepMagic = 0;
    RTC_NOINIT_ATTR uint32_t sleepAppId = 0;
    constexpr uint32_t SLEEP_MAGIC = 0xDEADBEEF;
}

SleepManager::SleepManager(Inkplate& display, AppManager& appManager)
    : display_(display), appManager_(appManager) {}

void SleepManager::begin() {
    lastActivityMs_ = millis();
    inLightSleep_ = false;
}

bool SleepManager::wasDeepSleep() {
    return sleepMagic == SLEEP_MAGIC;
}

uint32_t SleepManager::lastAppIdRaw() {
    return sleepAppId;
}

bool SleepManager::ensureScreensaverFile() {
    if (display_.sdCardInit() != 1) {
        Serial.println("SleepManager: SD not available");
        return false;
    }

    FsFile f = display_.getSdFat().open("/screensaver.jpg", O_RDONLY);
    if (f && f.fileSize() == static_cast<uint64_t>(screensaver_jpg_len)) {
        f.close();
        return true;
    }
    if (f) f.close();

    FsFile out = display_.getSdFat().open("/screensaver.jpg", O_CREAT | O_TRUNC | O_WRONLY);
    if (!out) {
        Serial.println("SleepManager: cannot create /screensaver.jpg");
        return false;
    }

    size_t written = out.write(screensaver_jpg, screensaver_jpg_len);
    out.close();

    if (written != screensaver_jpg_len) {
        Serial.printf("SleepManager: wrote %u of %u bytes\n", written, screensaver_jpg_len);
        return false;
    }

    Serial.println("SleepManager: /screensaver.jpg saved");
    return true;
}

void SleepManager::showScreensaverAndDeepSleep() {
    Serial.println("Entering deep sleep");

    display_.selectDisplayMode(INKPLATE_3BIT);
    display_.setRotation(0);  // Landscape 1280x720, matches the screensaver JPEG.
    display_.clearDisplay();
    display_.fillScreen(7);

    // The embedded JPEG is not loaded by the decoder from flash, so keep a copy
    // on the SD card and draw from there.
    bool drawn = false;
    if (ensureScreensaverFile()) {
        drawn = display_.image.drawJpegFromSd("/screensaver.jpg", 0, 0, true, false);
    }
    if (!drawn) {
        Serial.println("SleepManager: drawJpegFromSd failed, falling back to buffer");
        drawn = display_.image.drawJpegFromBuffer((uint8_t *)screensaver_jpg, (int32_t)screensaver_jpg_len,
                                                  0, 0, true, false);
    }
    display_.display();

    // Give the panel time to finish, then cut e-paper power.
    delay(100);
    display_.einkOff();

    // Remember where we were so we can resume on wake.
    sleepAppId = static_cast<uint32_t>(appManager_.currentAppId());
    sleepMagic = SLEEP_MAGIC;

    // Wake only on the WAKE button (GPIO 36, active low). Clear the periodic
    // timer / ext1 sources used by light sleep so they don't fire in deep sleep.
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_36, LOW);
    esp_deep_sleep_start();
}

void SleepManager::enterLightSleep() {
    Serial.println("Entering light sleep");

    // Wake on any button press, and periodically so the on-screen clock can
    // refresh while the device is idle.
    uint64_t mask = (1ULL << GPIO_NUM_36) | (1ULL << GPIO_NUM_0);
    esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ALL_LOW);
    esp_sleep_enable_timer_wakeup(LIGHT_SLEEP_WAKE_US);

    esp_light_sleep_start();

    // Light sleep resets the I2S peripheral that drives the panel, so re-init it
    // before the next update. The first update after a wake must also be a full
    // refresh — a partial update on a just-woken panel produces column artefacts.
    display_.I2SInit(&I2S1);
    appManager_.requestPostSleepFullRefresh();

    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1) {
        // Button press — real activity; stay awake and reset the inactivity clock.
        lastActivityMs_ = millis();
        inLightSleep_ = true;
    }
    // Timer wake: leave inLightSleep_ false and don't touch lastActivityMs_, so
    // the loop runs once (refreshing the status row) then re-enters light sleep,
    // and the deep-sleep countdown keeps running.
}

void SleepManager::update(uint32_t nowMs, bool activity) {
    if (activity) {
        lastActivityMs_ = nowMs;
        inLightSleep_ = false;
        return;
    }

    if (nowMs - lastActivityMs_ >= DEEP_SLEEP_MS) {
        showScreensaverAndDeepSleep();
    } else if (!inLightSleep_ && (nowMs - lastActivityMs_ >= LIGHT_SLEEP_MS)) {
        enterLightSleep();
    }
}

void SleepManager::enterDeepSleep() {
    showScreensaverAndDeepSleep();
}
