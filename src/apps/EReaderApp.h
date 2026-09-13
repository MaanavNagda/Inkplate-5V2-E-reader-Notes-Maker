#ifndef EREADER_APP_H
#define EREADER_APP_H

#include "../App.h"
#include "Book/EpubReader.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class EReaderApp : public App {
public:
    AppId id() const override { return AppId::EREADER; }
    const char* name() const override { return "E-reader"; }
    void begin(AppManager& manager) override;
    void update(uint32_t dtMs) override;
    void render(Inkplate& display) override;
    void onButton(ButtonAction action) override;
private:
    enum class State { LIBRARY, READER, MENU, EXITING };

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

    EpubReader reader_;
    std::vector<std::string> pages_;
    size_t currentPage_ = 0;
    size_t currentChapter_ = 0;

    std::vector<std::pair<size_t, size_t>> bookmarks_;
    size_t bookmarkIdx_ = 0;

    uint8_t refreshCount_ = 0;

    // Bottom status row (battery / time / date) tracking.
    uint32_t lastStatusCheckMs_ = 0;
    uint32_t lastBatteryCheckMs_ = 0;
    uint32_t lastFullRefreshMs_ = 0;
    int statusHour_ = -1;
    int statusMinute_ = -1;
    int statusDay_ = -1;
    int statusMonth_ = -1;
    int statusYear_ = -1;
    int lastBatteryPct_ = -1;
    bool rtcOk_ = false;
    bool statusForceFull_ = false;

    void applyColors(Inkplate& display);
    void clearBackground(Inkplate& display);
    void forceFullRefresh();

    void scanBooks(SdFat& sd);
    void loadSelectedBook(SdFat& sd);
    bool loadChapter(size_t index);
    bool nextChapter();
    bool prevChapter();
    void paginate(Inkplate& display, const std::string& text);

    void drawLibrary(Inkplate& display);
    void drawReader(Inkplate& display);
    void drawMenu(Inkplate& display);
    void drawBookmarks(Inkplate& display);
    void drawExiting(Inkplate& display);
    void drawStatusRow(Inkplate& display);
    void drawBatteryIcon(Inkplate& display, int16_t x, int16_t y, int pct);
    void checkStatusRow();

    bool hasBookmark(size_t chapter, size_t page) const;
    void toggleBookmark();
    void jumpToNextBookmark();
    void updateBookmarkIndex();

    std::string bookmarkPathFor(const std::string& bookPath) const;
    void saveBookmarks();
    void loadBookmarksForBook(SdFat& sd, const std::string& bookPath);

    std::string lastReadPathFor(const std::string& bookPath) const;
    bool loadLastRead(SdFat& sd, const std::string& bookPath, size_t& chapter, size_t& page) const;
    void saveLastRead();
};

#endif
