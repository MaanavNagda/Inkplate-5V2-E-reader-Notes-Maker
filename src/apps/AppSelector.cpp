#include "AppSelector.h"
#include "../AppManager.h"
#include "Inkplate.h"
#include "Fonts/FreeSans12pt7b.h"
#include <cstring>

namespace {
    constexpr int16_t BOX_SIZE = 300;
    constexpr int16_t GAP = 20;
    // Landscape (setRotation(1)): width=1280, height=720
    constexpr int16_t GRID_X = (1280 - (4 * BOX_SIZE + 3 * GAP)) / 2;
    constexpr int16_t GRID_Y = (720 - BOX_SIZE) / 2;
    constexpr int16_t TITLE_Y = 80;
    constexpr uint16_t APP_BLACK = 7;
    constexpr uint16_t APP_WHITE = 0;

    const GFXfont* const UIFont = &FreeSans12pt7b;

    int16_t textCenterX(Inkplate& d, const char* text, int16_t boxX) {
        int16_t x1, y1;
        uint16_t w, h;
        d.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
        return boxX + (BOX_SIZE - static_cast<int16_t>(w)) / 2 - x1;
    }

    void drawBookIcon(Inkplate& d, int16_t x, int16_t y) {
        int16_t ix = x + (BOX_SIZE - 100) / 2;
        int16_t iy = y + 45;
        d.drawRect(ix, iy, 100, 80, APP_BLACK);
        d.drawFastVLine(ix + 50, iy, 80, APP_BLACK);
    }

    void drawTextbookIcon(Inkplate& d, int16_t x, int16_t y) {
        int16_t ix = x + (BOX_SIZE - 100) / 2;
        int16_t iy = y + 45;
        d.drawRect(ix, iy, 100, 80, APP_BLACK);
        d.drawFastVLine(ix + 50, iy, 80, APP_BLACK);
        for (int i = 0; i < 4; ++i) {
            d.drawFastHLine(ix + 58, iy + 12 + i * 14, 32, APP_BLACK);
        }
    }

    void drawNotesIcon(Inkplate& d, int16_t x, int16_t y) {
        int16_t ix = x + (BOX_SIZE - 40) / 2;
        int16_t iy = y + 35;
        d.fillRect(ix, iy + 20, 40, 90, APP_BLACK);
        d.fillRect(ix, iy, 40, 20, APP_BLACK);
        d.fillTriangle(ix, iy + 110, ix + 40, iy + 110, ix + 20, iy + 140, APP_BLACK);
    }

    void drawSplitIcon(Inkplate& d, int16_t x, int16_t y) {
        int16_t cx = x + BOX_SIZE / 2;
        d.drawFastVLine(cx, y + 35, 120, APP_BLACK);

        // left small pencil
        d.fillRect(cx - 45, y + 55, 18, 45, APP_BLACK);
        d.fillRect(cx - 45, y + 38, 18, 17, APP_BLACK);
        d.fillTriangle(cx - 45, y + 100, cx - 27, y + 100, cx - 36, y + 118, APP_BLACK);

        // right small open book
        d.drawRect(cx + 22, y + 40, 55, 75, APP_BLACK);
        d.drawFastVLine(cx + 49, y + 40, 75, APP_BLACK);
    }

    void drawBox(Inkplate& d, int16_t x, int16_t y, const char* title,
                 const char* sub, void (*icon)(Inkplate&, int16_t, int16_t)) {
        d.drawRect(x, y, BOX_SIZE, BOX_SIZE, APP_BLACK);
        icon(d, x, y);
        d.setFont(UIFont);
        d.setTextSize(1);
        d.setCursor(textCenterX(d, title, x), y + 180);
        d.print(title);
        d.setCursor(textCenterX(d, sub, x), y + 230);
        d.print(sub);
    }
}

void AppSelector::begin(AppManager& manager) {
    App::begin(manager);
    needsRender_ = true;
}

void AppSelector::update(uint32_t dtMs) {
    (void)dtMs;
    if (!manager_) return;

    if (statusRow_.poll(manager_->display())) {
        needsRender_ = true;
        statusUpdate_ = true;
    }

    if (needsRender_) {
        render(manager_->display());
    }
}

void AppSelector::render(Inkplate& display) {
    display.setRotation(0);      // Landscape home screen
    display.selectDisplayMode(INKPLATE_1BIT);
    display.clearDisplay();
    display.fillScreen(APP_WHITE);
    display.setTextColor(APP_BLACK, APP_WHITE);
    display.setTextWrap(false);

    // Title with underline
    display.setFont(UIFont);
    display.setTextSize(1);
    const char* title = "Inkplate Home";
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
    int16_t titleX = (display.width() - static_cast<int16_t>(w)) / 2 - x1;
    display.setCursor(titleX, TITLE_Y);
    display.print(title);
    display.drawFastHLine(titleX, TITLE_Y + 28, w, APP_BLACK);

    // four boxes in a horizontal row
    const int16_t step = BOX_SIZE + GAP;
    drawBox(display, GRID_X, GRID_Y, "E-reader", "IO tap", drawBookIcon);
    drawBox(display, GRID_X + step, GRID_Y, "Textbook", "IO dbl", drawTextbookIcon);
    drawBox(display, GRID_X + 2 * step, GRID_Y, "Notes", "keyboard", drawNotesIcon);
    drawBox(display, GRID_X + 3 * step, GRID_Y, "Notes + Text", "keyboard", drawSplitIcon);

    // Bottom status row (battery / time / date), light mode.
    statusRow_.draw(display, false, false);

    // Status changes partial-refresh; first draw, the 10-minute timer, and the
    // first update after a light-sleep wake go full.
    bool full = !statusUpdate_ || statusRow_.fullRefreshDue() ||
                manager_->consumePostSleepFullRefresh();
    if (full) {
        display.display();
        statusRow_.noteFullRefresh();
    } else {
        display.partialUpdate(INKPLATE_FORCE_PARTIAL, false);
    }
    statusUpdate_ = false;
    needsRender_ = false;
}

void AppSelector::onButton(ButtonAction action) {
    if (!manager_) return;
    switch (action) {
        case ButtonAction::IO_SHORT:  manager_->switchTo(AppId::EREADER); break;
        case ButtonAction::IO_DOUBLE: manager_->switchTo(AppId::TEXTBOOK); break;
        case ButtonAction::IO_LONG:   manager_->enterDeepSleep(); break;
        default: break;
    }
}
