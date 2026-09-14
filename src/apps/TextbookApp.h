#ifndef TEXTBOOK_APP_H
#define TEXTBOOK_APP_H

#include "../App.h"
#include "../StatusRow.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class TextbookApp : public App {
public:
    AppId id() const override { return AppId::TEXTBOOK; }
    const char* name() const override { return "Textbook"; }
    void begin(AppManager& manager) override;
    void update(uint32_t dtMs) override;
    void render(Inkplate& display) override;
    void onButton(ButtonAction action) override;
private:
    enum class State { LIBRARY, READER, MENU, PAGE_SELECTOR, EXITING };

    bool needsRender_ = true;
    bool sdOk_ = false;
    bool booksScanned_ = false;
    bool bookLoaded_ = false;
    bool darkMode_ = false;
    State state_ = State::LIBRARY;

    std::string status_;
    std::vector<std::string> books_;
    std::vector<std::string> bookNames_;
    size_t selectedBook_ = 0;
    std::string loadedBook_;
    size_t loadedBookIndex_ = 0;

    std::vector<std::string> pages_;
    size_t currentPage_ = 0;
    size_t currentChapter_ = 0;
    uint8_t pageSelectorDigits_[3] = {0, 0, 1};
    int8_t selectedDigit_ = 0;

    uint8_t refreshCount_ = 0;

    // Bottom status row (battery / time / date), shared with the other apps.
    StatusRow statusRow_;

    void applyColors(Inkplate& display);
    void clearBackground(Inkplate& display);
    void forceFullRefresh();

    void loadTextbookList();
    void loadSelectedBook();
    bool loadChapter(size_t index);
    bool nextChapter();
    bool prevChapter();

    void drawLibrary(Inkplate& display);
    void drawReader(Inkplate& display);
    void drawMenu(Inkplate& display);
    void drawPageSelector(Inkplate& display);
    void drawExiting(Inkplate& display);

    bool hasBookmark(size_t chapter, size_t page) const;
    void updateBookmarkIndex();

    std::string lastReadPathFor(size_t bookIndex) const;
    bool loadLastRead(size_t& chapter, size_t& page);
    void saveLastRead();
};

#endif
