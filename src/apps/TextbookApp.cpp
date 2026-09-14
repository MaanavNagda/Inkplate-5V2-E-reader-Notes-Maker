#include "TextbookApp.h"
#include "TextbookData.h"
#include "../AppManager.h"
#include "Inkplate.h"
#include "Fonts/FreeSans12pt7b.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace {
    constexpr uint8_t FULL_REFRESH_EVERY = 10;
    constexpr uint16_t MARGIN_X = 20;
    constexpr uint16_t LIST_X = 40;
    constexpr uint16_t LIST_Y = 100;
    constexpr uint16_t LIST_ITEM_H = 50;
    constexpr uint16_t FG_LIGHT = 7;
    constexpr uint16_t BG_LIGHT = 0;
    constexpr uint16_t FG_DARK = 0;
    constexpr uint16_t BG_DARK = 7;
    const GFXfont* const UIFont    = &FreeSans12pt7b;
    const GFXfont* const TitleFont = &FreeSans12pt7b;
}

void TextbookApp::applyColors(Inkplate& display) {
    display.setTextColor(darkMode_ ? FG_DARK : FG_LIGHT,
                         darkMode_ ? BG_DARK : BG_LIGHT);
}

void TextbookApp::clearBackground(Inkplate& display) {
    display.clearDisplay();
    display.fillScreen(darkMode_ ? BG_DARK : BG_LIGHT);
}

void TextbookApp::forceFullRefresh() {
    refreshCount_ = 0;
}

void TextbookApp::loadTextbookList() {
    books_.clear();
    bookNames_.clear();
    booksScanned_ = true;

    for (size_t i = 0; i < textbook::entryCount; ++i) {
        const char* title = textbook::entries[i].title ? textbook::entries[i].title : "Untitled";
        bookNames_.push_back(title);
        books_.push_back(std::to_string(i));
    }

    if (selectedBook_ >= bookNames_.size()) {
        selectedBook_ = 0;
    }

    Serial.printf("loadTextbookList: found %u textbooks\n", static_cast<unsigned>(bookNames_.size()));
}

void TextbookApp::loadSelectedBook() {
    bookLoaded_ = false;
    currentPage_ = 0;
    currentChapter_ = 0;
    loadedBook_.clear();
    loadedBookIndex_ = 0;

    if (!manager_) return;
    manager_->display().setFullUpdateThreshold(0);

    if (selectedBook_ >= textbook::entryCount) {
        status_ = "No textbook selected";
        return;
    }

    const textbook::Entry& entry = textbook::entries[selectedBook_];
    loadedBook_ = entry.title ? entry.title : "Untitled";
    loadedBookIndex_ = selectedBook_;
    status_ = loadedBook_;

    Serial.printf("loadSelectedBook: opening %s, %u pages\n",
                  loadedBook_.c_str(), static_cast<unsigned>(entry.pageCount));

    size_t resumeChapter = 0;
    size_t resumePage = 0;
    if (loadLastRead(resumeChapter, resumePage)) {
        if (resumePage < entry.pageCount) {
            currentPage_ = resumePage;
        } else if (entry.pageCount > 0) {
            currentPage_ = entry.pageCount - 1;
        }
    }

    updateBookmarkIndex();
    bookLoaded_ = true;
    forceFullRefresh();
}

std::string TextbookApp::lastReadPathFor(size_t bookIndex) const {
    return "/bookmarks/textbook_" + std::to_string(bookIndex) + ".lr";
}

bool TextbookApp::loadLastRead(size_t& chapter, size_t& page) {
    chapter = 0;
    page = 0;
    if (!manager_) return false;
    if (manager_->display().sdCardInit() != 1) return false;

    SdFat& sd = manager_->display().getSdFat();
    std::string path = lastReadPathFor(loadedBookIndex_);
    Serial.printf("loadLastRead: reading %s\n", path.c_str());

    FsFile f;
    if (!f.open(&sd, path.c_str(), O_RDONLY)) {
        Serial.printf("loadLastRead: %s not found\n", path.c_str());
        return false;
    }

    char buf[64];
    int n = f.read(buf, sizeof(buf) - 1);
    f.close();
    if (n <= 0) return false;
    buf[n] = '\0';
    std::string content(buf);

    size_t comma = content.find(',');
    if (comma == std::string::npos) return false;
    chapter = static_cast<size_t>(strtoul(content.substr(0, comma).c_str(), nullptr, 10));
    page = static_cast<size_t>(strtoul(content.substr(comma + 1).c_str(), nullptr, 10));
    Serial.printf("loadLastRead: parsed page=%u\n", static_cast<unsigned>(page));
    return true;
}

void TextbookApp::saveLastRead() {
    if (!manager_ || loadedBook_.empty()) return;
    if (manager_->display().sdCardInit() != 1) return;

    SdFat& sd = manager_->display().getSdFat();
    if (!sd.exists("/bookmarks")) {
        Serial.println("saveLastRead: creating /bookmarks");
        sd.mkdir("/bookmarks");
    }

    std::string path = lastReadPathFor(loadedBookIndex_);
    Serial.printf("saveLastRead: writing %s\n", path.c_str());
    FsFile f;
    if (!f.open(&sd, path.c_str(), O_WRONLY | O_CREAT | O_TRUNC)) {
        Serial.printf("saveLastRead: cannot write %s\n", path.c_str());
        return;
    }

    char line[32];
    int len = snprintf(line, sizeof(line), "%u,%u\n",
                       static_cast<unsigned>(currentChapter_), static_cast<unsigned>(currentPage_));
    if (len > 0) {
        size_t written = f.write(reinterpret_cast<const uint8_t*>(line), static_cast<size_t>(len));
        if (written != static_cast<size_t>(len)) {
            Serial.printf("saveLastRead: write failed, wrote %u of %d\n",
                          static_cast<unsigned>(written), len);
        }
    }
    f.sync();
    f.close();
    Serial.println("saveLastRead: done");
}

bool TextbookApp::hasBookmark(size_t chapter, size_t page) const {
    (void)chapter;
    (void)page;
    return false;
}

void TextbookApp::updateBookmarkIndex() {
    saveLastRead();
}

void TextbookApp::begin(AppManager& manager) {
    App::begin(manager);
    state_ = State::LIBRARY;
    manager_->display().setFullUpdateThreshold(10);
    needsRender_ = true;
    sdOk_ = false;
    booksScanned_ = false;
    bookLoaded_ = false;
    darkMode_ = false;
    status_ = "Textbooks";
    books_.clear();
    bookNames_.clear();
    selectedBook_ = 0;
    loadedBook_.clear();
    loadedBookIndex_ = 0;
    pages_.clear();
    currentPage_ = 0;
    currentChapter_ = 0;
    refreshCount_ = 0;
}

void TextbookApp::update(uint32_t dtMs) {
    (void)dtMs;
    if (!manager_) return;

    if (state_ == State::READER && statusRow_.poll(manager_->display())) {
        needsRender_ = true;
    }

    if (needsRender_) {
        render(manager_->display());
    }
}

void TextbookApp::render(Inkplate& display) {
    display.setTextWrap(false);

    if (!booksScanned_) {
        loadTextbookList();
        sdOk_ = (display.sdCardInit() == 1);
    }

    if (state_ == State::READER) {
        display.setRotation(3);
        display.selectDisplayMode(INKPLATE_3BIT);
        clearBackground(display);

        if (bookLoaded_) {
            drawReader(display);
        } else {
            display.setFont(UIFont);
            display.setTextSize(1);
            display.setCursor(MARGIN_X, 120);
            display.print(status_.empty() ? "No textbook loaded" : status_.c_str());
        }

        display.display();
        statusRow_.noteFullRefresh();
        needsRender_ = false;
        return;
    }

    display.setRotation(state_ == State::LIBRARY ? 0 : 3);
    display.selectDisplayMode(INKPLATE_1BIT);
    clearBackground(display);
    applyColors(display);

    switch (state_) {
        case State::LIBRARY:       drawLibrary(display); break;
        case State::MENU:          drawMenu(display); break;
        case State::PAGE_SELECTOR: drawPageSelector(display); break;
        case State::EXITING:       drawExiting(display); break;
        default: break;
    }

    bool full = (state_ != State::LIBRARY && state_ != State::PAGE_SELECTOR);
    if (full) {
        display.display();
    } else {
        display.partialUpdate(INKPLATE_FORCE_PARTIAL, false);
    }

    ++refreshCount_;
    if (refreshCount_ >= FULL_REFRESH_EVERY) refreshCount_ = 0;
    needsRender_ = false;
}

void TextbookApp::drawLibrary(Inkplate& display) {
    display.setFont(TitleFont);
    display.setTextSize(1);
    display.setCursor(MARGIN_X, 30);
    display.print("Textbooks");
    display.drawFastHLine(MARGIN_X, 55, display.width() - 2 * MARGIN_X,
                          darkMode_ ? FG_DARK : FG_LIGHT);

    if (bookNames_.empty()) {
        display.setFont(UIFont);
        display.setTextSize(1);
        display.setCursor(LIST_X, 120);
        display.print("No textbooks loaded");
        return;
    }

    constexpr size_t VISIBLE = 10;
    size_t start = 0;
    if (selectedBook_ >= VISIBLE) start = selectedBook_ - VISIBLE + 1;

    int16_t y = LIST_Y;
    for (size_t i = start; i < bookNames_.size() && i < start + VISIBLE; ++i) {
        if (i == selectedBook_) {
            uint16_t selFill = darkMode_ ? FG_DARK : BG_DARK;
            uint16_t selText = darkMode_ ? BG_DARK : FG_DARK;
            display.fillRect(LIST_X, y, display.width() - 2 * LIST_X, LIST_ITEM_H, selFill);
            display.setTextColor(selText, selFill);
        } else {
            display.setTextColor(darkMode_ ? FG_DARK : FG_LIGHT,
                                 darkMode_ ? BG_DARK : BG_LIGHT);
        }
        display.setFont(UIFont);
        display.setTextSize(1);
        display.setCursor(LIST_X + 10, y + 30);
        display.print(bookNames_[i].c_str());
        y += LIST_ITEM_H;
    }
    applyColors(display);

    display.setFont(UIFont);
    display.setTextSize(1);
    display.setCursor(MARGIN_X, display.height() - 30);
    display.print("tap=down  dbl=up  hold=open");
}

void TextbookApp::drawReader(Inkplate& display) {
    const textbook::Entry& entry = textbook::entries[loadedBookIndex_];
    if (currentPage_ >= entry.pageCount) {
        currentPage_ = entry.pageCount ? entry.pageCount - 1 : 0;
    }

    char path[128];
    snprintf(path, sizeof(path), "%s%03zu.jpg", entry.pathPrefix, currentPage_ + 1);
    Serial.printf("drawReader: %s\n", path);

    bool ok = display.image.draw(path, Image::JPG, Image::TopLeft, true, false);
    if (!ok) {
        display.setFont(UIFont);
        display.setTextSize(1);
        display.setCursor(MARGIN_X, 120);
        display.print("Could not draw page");
    }

    // Status row over the bottom of the page (light mode; fill a strip so the
    // text stays readable over the image).
    statusRow_.draw(display, false, true);
}

void TextbookApp::drawMenu(Inkplate& display) {
    display.setFont(TitleFont);
    display.setTextSize(1);
    display.setCursor(MARGIN_X, 30);
    display.print("Menu");
    display.drawFastHLine(MARGIN_X, 55, display.width() - 2 * MARGIN_X,
                          darkMode_ ? FG_DARK : FG_LIGHT);

    display.setFont(UIFont);
    display.setTextSize(1);
    int16_t y = 120;
    display.setCursor(MARGIN_X, y);
    display.print("tap:  Page selector");
    y += 60;
    display.setCursor(MARGIN_X, y);
    display.print("dbl:  Exit");
    y += 60;
    display.setCursor(MARGIN_X, y);
    display.print("hold: Full refresh");
}

void TextbookApp::drawPageSelector(Inkplate& display) {
    display.setFont(TitleFont);
    display.setTextSize(1);
    display.setCursor(MARGIN_X, 30);
    display.print("Page selector");
    display.drawFastHLine(MARGIN_X, 55, display.width() - 2 * MARGIN_X, FG_LIGHT);

    const textbook::Entry& entry = textbook::entries[loadedBookIndex_];

    display.setFont(UIFont);
    display.setTextSize(3);
    for (int i = 0; i < 3; ++i) {
        int16_t x = MARGIN_X + i * 70;
        if (i == selectedDigit_) {
            display.fillRect(x - 5, 150 - 42, 60, 60, BG_DARK);
            display.setTextColor(FG_DARK, BG_DARK);
        } else {
            display.setTextColor(FG_LIGHT, BG_LIGHT);
        }
        display.setCursor(x, 150);
        display.print(static_cast<unsigned>(pageSelectorDigits_[i]));
    }

    display.setFont(UIFont);
    display.setTextSize(1);
    display.setTextColor(FG_LIGHT, BG_LIGHT);
    display.setCursor(MARGIN_X + 240, 150);
    display.print("of ");
    display.print(static_cast<unsigned>(entry.pageCount));

    display.setCursor(MARGIN_X, display.height() - 30);
    display.print("tap=+  dbl=prev  hold=next/select");
}

void TextbookApp::drawExiting(Inkplate& display) {
    display.setFont(TitleFont);
    display.setTextSize(1);
    display.setCursor(MARGIN_X, 30);
    display.print("Exit");
    display.drawFastHLine(MARGIN_X, 55, display.width() - 2 * MARGIN_X,
                          darkMode_ ? FG_DARK : FG_LIGHT);

    display.setFont(UIFont);
    display.setTextSize(1);
    int16_t y = 120;
    display.setCursor(MARGIN_X, y);
    display.print("tap:  Back to menu");
    y += 60;
    display.setCursor(MARGIN_X, y);
    display.print("dbl:  Back to page");
    y += 60;
    display.setCursor(MARGIN_X, y);
    display.print("hold: Home");
}

void TextbookApp::onButton(ButtonAction action) {
    if (!manager_) return;

    switch (state_) {
        case State::LIBRARY:
            switch (action) {
                case ButtonAction::IO_SHORT:
                    if (!bookNames_.empty()) {
                        selectedBook_ = (selectedBook_ + 1) % bookNames_.size();
                        needsRender_ = true;
                    }
                    break;
                case ButtonAction::IO_DOUBLE:
                    if (!bookNames_.empty()) {
                        selectedBook_ = (selectedBook_ + bookNames_.size() - 1) % bookNames_.size();
                        needsRender_ = true;
                    }
                    break;
                case ButtonAction::IO_LONG:
                    if (bookNames_.empty()) {
                        manager_->switchTo(AppId::SELECTOR);
                    } else {
                        loadSelectedBook();
                        state_ = State::READER;
                        needsRender_ = true;
                    }
                    break;
                default: break;
            }
            break;

        case State::READER:
            switch (action) {
                case ButtonAction::IO_SHORT:
                    if (currentPage_ + 1 < textbook::entries[loadedBookIndex_].pageCount) {
                        ++currentPage_;
                        updateBookmarkIndex();
                        needsRender_ = true;
                    } else {
                        saveLastRead();
                        status_ = loadedBook_ + " (Finished)";
                        needsRender_ = true;
                    }
                    break;
                case ButtonAction::IO_DOUBLE:
                    if (currentPage_ > 0) {
                        --currentPage_;
                        updateBookmarkIndex();
                        needsRender_ = true;
                    }
                    break;
                case ButtonAction::IO_LONG:
                    state_ = State::MENU;
                    forceFullRefresh();
                    needsRender_ = true;
                    break;
                default: break;
            }
            break;

        case State::MENU:
            switch (action) {
                case ButtonAction::IO_SHORT:
                    {
                        const textbook::Entry& entry = textbook::entries[loadedBookIndex_];
                        uint32_t page1 = currentPage_ + 1;
                        if (page1 > entry.pageCount) page1 = static_cast<uint32_t>(entry.pageCount);
                        if (page1 < 1) page1 = 1;
                        pageSelectorDigits_[2] = static_cast<uint8_t>(page1 % 10);
                        pageSelectorDigits_[1] = static_cast<uint8_t>((page1 / 10) % 10);
                        pageSelectorDigits_[0] = static_cast<uint8_t>((page1 / 100) % 10);
                        selectedDigit_ = 0;
                        state_ = State::PAGE_SELECTOR;
                        needsRender_ = true;
                    }
                    break;
                case ButtonAction::IO_DOUBLE:
                    state_ = State::EXITING;
                    forceFullRefresh();
                    needsRender_ = true;
                    break;
                case ButtonAction::IO_LONG:
                    state_ = State::READER;
                    manager_->display().setFullUpdateThreshold(0);
                    forceFullRefresh();
                    needsRender_ = true;
                    break;
                default: break;
            }
            break;

        case State::PAGE_SELECTOR:
            switch (action) {
                case ButtonAction::IO_SHORT:
                    {
                        const textbook::Entry& entry = textbook::entries[loadedBookIndex_];
                        uint32_t pageCount = static_cast<uint32_t>(entry.pageCount);
                        uint8_t firstMax = (pageCount >= 900) ? 9 : static_cast<uint8_t>(pageCount / 100);
                        uint8_t max = 9;
                        if (selectedDigit_ == 0) max = firstMax;
                        pageSelectorDigits_[selectedDigit_] = (pageSelectorDigits_[selectedDigit_] + 1) % (max + 1);
                        needsRender_ = true;
                    }
                    break;
                case ButtonAction::IO_DOUBLE:
                    selectedDigit_ = (selectedDigit_ + 2) % 3;
                    needsRender_ = true;
                    break;
                case ButtonAction::IO_LONG:
                    if (selectedDigit_ == 2) {
                        const textbook::Entry& entry = textbook::entries[loadedBookIndex_];
                        uint32_t pageCount = static_cast<uint32_t>(entry.pageCount);
                        uint32_t target = pageSelectorDigits_[0] * 100U + pageSelectorDigits_[1] * 10U + pageSelectorDigits_[2];
                        if (target < 1) target = 1;
                        if (target > pageCount) target = pageCount;
                        if (target > 0) {
                            currentPage_ = target - 1;
                            updateBookmarkIndex();
                        }
                        state_ = State::READER;
                    } else {
                        ++selectedDigit_;
                    }
                    needsRender_ = true;
                    break;
                default: break;
            }
            break;

        case State::EXITING:
            switch (action) {
                case ButtonAction::IO_SHORT:
                    state_ = State::MENU;
                    forceFullRefresh();
                    needsRender_ = true;
                    break;
                case ButtonAction::IO_DOUBLE:
                    state_ = State::READER;
                    manager_->display().setFullUpdateThreshold(0);
                    forceFullRefresh();
                    needsRender_ = true;
                    break;
                case ButtonAction::IO_LONG:
                    manager_->switchTo(AppId::SELECTOR);
                    break;
                default: break;
            }
            break;
    }
}
