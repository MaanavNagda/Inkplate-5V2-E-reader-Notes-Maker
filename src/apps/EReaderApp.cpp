#include "EReaderApp.h"
#include "../AppManager.h"
#include "Inkplate.h"
#include "Book/EpubReader.h"
#include "TextLayout.h"
#include "Fonts/FreeSans9pt7b.h"
#include "Fonts/FreeSans12pt7b.h"

#include <algorithm>
#include <cstdlib>

namespace {
    constexpr uint8_t FULL_REFRESH_EVERY = 50;
    constexpr uint8_t TEXT_SIZE = 1;           // native font scaling
    constexpr uint16_t CHAR_W = 13;              // FreeSans12pt7b average xAdvance
    constexpr uint16_t CHAR_H = 29;              // FreeSans12pt7b yAdvance (line height)
    constexpr uint16_t MARGIN_X = 20;
    constexpr uint16_t MARGIN_Y = 80;
    constexpr uint16_t EPUB_LEFT_MARGIN  = 40;
    constexpr uint16_t EPUB_RIGHT_MARGIN = 2 * MARGIN_X - EPUB_LEFT_MARGIN;

    constexpr int16_t LIST_X = 40;
    constexpr int16_t LIST_Y = 100;
    constexpr int16_t LIST_ITEM_H = 50;

    constexpr uint16_t FG_LIGHT = 7;
    constexpr uint16_t BG_LIGHT = 0;
    constexpr uint16_t FG_DARK = 0;
    constexpr uint16_t BG_DARK = 7;

    const GFXfont* const BodyFont    = &FreeSans12pt7b;  // smooth book text
    const GFXfont* const UIFont      = &FreeSans12pt7b;  // smooth UI labels
    const GFXfont* const TitleFont   = &FreeSans12pt7b;  // smooth titles
    const GFXfont* const PageNumFont = &FreeSans12pt7b;  // smooth page counter
}

void EReaderApp::applyColors(Inkplate& display) {
    display.setTextColor(darkMode_ ? FG_DARK : FG_LIGHT,
                         darkMode_ ? BG_DARK : BG_LIGHT);
}

void EReaderApp::clearBackground(Inkplate& display) {
    display.clearDisplay();
    display.fillScreen(darkMode_ ? BG_DARK : BG_LIGHT);
}

void EReaderApp::forceFullRefresh() {
    refreshCount_ = 0;
}

void EReaderApp::scanBooks(SdFat& sd) {
    books_.clear();
    bookNames_.clear();
    booksScanned_ = true;

    Serial.println("scanBooks: opening /books");
    FsFile dir = sd.open("/books");
    if (!dir) {
        Serial.println("scanBooks: cannot open /books");
        return;
    }
    if (!dir.isDirectory()) {
        Serial.println("scanBooks: /books is not a directory");
        return;
    }

    Serial.println("scanBooks: listing entries");
    FsFile entry;
    while (entry.openNext(&dir, O_RDONLY)) {
        if (entry.isDirectory()) {
            entry.close();
            continue;
        }
        char name[128] = {};
        size_t nameLen = entry.getName(name, sizeof(name) - 1);
        Serial.printf("scanBooks: entry name='%s' len=%u isDir=%d\n", name, nameLen, entry.isDirectory());
        if (!nameLen) {
            entry.close();
            continue;
        }
        std::string n(name);
        if (n.size() >= 5 && n.compare(n.size() - 5, 5, ".epub") == 0) {
            books_.push_back("/books/" + n);
            bookNames_.push_back(n.substr(0, n.size() - 5));
        }
        entry.close();
    }
    dir.close();

    Serial.printf("scanBooks: found %u .epub files\n", books_.size());
    if (selectedBook_ >= books_.size()) {
        selectedBook_ = 0;
    }
}

void EReaderApp::loadSelectedBook(SdFat& sd) {
    bookLoaded_ = false;
    pages_.clear();
    currentPage_ = 0;
    currentChapter_ = 0;
    bookmarkIdx_ = 0;
    reader_.close();
    manager_->display().setFullUpdateThreshold(0);

    if (!manager_ || selectedBook_ >= books_.size()) {
        status_ = "No book selected";
        return;
    }

    const char* path = books_[selectedBook_].c_str();
    if (!reader_.loadFromFile(sd, path)) {
        status_ = "Failed to open " + books_[selectedBook_];
        return;
    }

    // Bookmarks are per book and persisted on SD; always (re)load them for the
    // selected book so a fresh boot / re-selection resumes at the saved page.
    loadedBook_ = books_[selectedBook_];
    Serial.printf("loadSelectedBook: opening %s\n", loadedBook_.c_str());
    loadBookmarksForBook(sd, loadedBook_);
    Serial.printf("loadSelectedBook: loaded %u bookmarks\n",
                  static_cast<unsigned>(bookmarks_.size()));

    // Portrait when actually reading.
    manager_->display().setRotation(3);

    // If we have a saved last-read position, resume there; otherwise use the
    // furthest bookmark.
    size_t resumeChapter = 0;
    size_t resumePage = 0;
    bool haveResume = loadLastRead(sd, loadedBook_, resumeChapter, resumePage);

    if (!haveResume && !bookmarks_.empty()) {
        auto furthest = std::max_element(bookmarks_.begin(), bookmarks_.end());
        resumeChapter = furthest->first;
        resumePage = furthest->second;
        haveResume = true;
        Serial.printf("loadSelectedBook: resuming at furthest bookmark chapter=%u page=%u\n",
                      static_cast<unsigned>(resumeChapter),
                      static_cast<unsigned>(resumePage));
    } else if (haveResume) {
        Serial.printf("loadSelectedBook: resuming at last-read chapter=%u page=%u\n",
                      static_cast<unsigned>(resumeChapter),
                      static_cast<unsigned>(resumePage));
    }

    if (haveResume && loadChapter(resumeChapter)) {
        if (resumePage < pages_.size()) {
            currentPage_ = resumePage;
        } else if (!pages_.empty()) {
            currentPage_ = pages_.size() - 1;
        }
        updateBookmarkIndex();
        forceFullRefresh();
        return;
    }

    // Open the first chapter that actually has text.
    bool found = false;
    for (size_t i = 0; i < reader_.chapterCount(); ++i) {
        if (loadChapter(i)) {
            found = true;
            break;
        }
    }

    if (!found) {
        status_ = reader_.title() + " (no text)";
        bookLoaded_ = false;
    }

    forceFullRefresh();
}

bool EReaderApp::loadChapter(size_t index) {
    if (!manager_ || index >= reader_.chapterCount()) return false;

    Inkplate& display = manager_->display();
    display.setRotation(3);

    std::string text = reader_.chapterText(index);
    if (text.empty()) return false;

    currentChapter_ = index;
    status_ = reader_.title();
    paginate(display, text);
    bookLoaded_ = true;
    forceFullRefresh();
    return true;
}

bool EReaderApp::nextChapter() {
    if (currentChapter_ + 1 >= reader_.chapterCount()) return false;
    for (size_t i = currentChapter_ + 1; i < reader_.chapterCount(); ++i) {
        if (loadChapter(i)) return true;
    }
    return false;
}

bool EReaderApp::prevChapter() {
    size_t i = currentChapter_;
    while (i > 0) {
        --i;
        if (loadChapter(i)) return true;
    }
    return false;
}

void EReaderApp::paginate(Inkplate& display, const std::string& text) {
    TextLayout layout(text);
    int16_t w = display.width();
    int16_t h = display.height();
    uint16_t areaW = (w > EPUB_LEFT_MARGIN + EPUB_RIGHT_MARGIN) ? (w - EPUB_LEFT_MARGIN - EPUB_RIGHT_MARGIN) : 0;
    uint16_t areaH = (h > MARGIN_Y + 30) ? (h - MARGIN_Y - 30) : 0;
    layout.setMetrics(CHAR_W, CHAR_H, areaW, areaH);
    pages_ = layout.pages();
    currentPage_ = 0;
    updateBookmarkIndex();
}

bool EReaderApp::hasBookmark(size_t chapter, size_t page) const {
    auto key = std::make_pair(chapter, page);
    return std::find(bookmarks_.begin(), bookmarks_.end(), key) != bookmarks_.end();
}

void EReaderApp::toggleBookmark() {
    auto key = std::make_pair(currentChapter_, currentPage_);
    auto it = std::find(bookmarks_.begin(), bookmarks_.end(), key);
    if (it != bookmarks_.end()) {
        bookmarks_.erase(it);
    } else {
        bookmarks_.push_back(key);
        std::sort(bookmarks_.begin(), bookmarks_.end());
    }
    updateBookmarkIndex();
    // Persist immediately so a bookmark survives an exit or power loss.
    saveBookmarks();
}

void EReaderApp::jumpToNextBookmark() {
    if (bookmarks_.empty() || !manager_) return;
    auto key = std::make_pair(currentChapter_, currentPage_);
    auto it = std::upper_bound(bookmarks_.begin(), bookmarks_.end(), key);
    if (it == bookmarks_.end()) {
        it = bookmarks_.begin();
    }
    if (loadChapter(it->first)) {
        if (it->second < pages_.size()) {
            currentPage_ = it->second;
        } else if (!pages_.empty()) {
            currentPage_ = pages_.size() - 1;
        }
        updateBookmarkIndex();
        forceFullRefresh();
    }
}

std::string EReaderApp::bookmarkPathFor(const std::string& bookPath) const {
    std::string name = bookPath;
    size_t slash = name.find_last_of('/');
    if (slash != std::string::npos) name = name.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    return "/bookmarks/" + name + ".bm";
}

void EReaderApp::saveBookmarks() {
    if (!manager_ || loadedBook_.empty()) return;
    SdFat& sd = manager_->display().getSdFat();

    if (!sd.exists("/bookmarks")) {
        Serial.println("saveBookmarks: creating /bookmarks");
        sd.mkdir("/bookmarks");
    }

    std::string path = bookmarkPathFor(loadedBook_);
    Serial.printf("saveBookmarks: writing %s\n", path.c_str());
    FsFile f;
    if (!f.open(&sd, path.c_str(), O_WRONLY | O_CREAT | O_TRUNC)) {
        Serial.printf("saveBookmarks: cannot write %s\n", path.c_str());
        return;
    }
    for (const auto& bm : bookmarks_) {
        char line[32];
        int len = snprintf(line, sizeof(line), "%u,%u\n",
                           static_cast<unsigned>(bm.first), static_cast<unsigned>(bm.second));
        if (len > 0) {
            size_t written = f.write(reinterpret_cast<const uint8_t*>(line), static_cast<size_t>(len));
            if (written != static_cast<size_t>(len)) {
                Serial.printf("saveBookmarks: write failed, wrote %u of %d\n",
                              static_cast<unsigned>(written), len);
            }
        }
    }
    f.sync();
    f.close();
    Serial.println("saveBookmarks: done");
}

void EReaderApp::loadBookmarksForBook(SdFat& sd, const std::string& bookPath) {
    bookmarks_.clear();
    bookmarkIdx_ = 0;

    std::string path = bookmarkPathFor(bookPath);
    Serial.printf("loadBookmarksForBook: reading %s\n", path.c_str());
    FsFile f;
    if (!f.open(&sd, path.c_str(), O_RDONLY)) {
        Serial.printf("loadBookmarksForBook: %s not found\n", path.c_str());
        return;
    }

    std::string content;
    char buf[128];
    int n;
    while ((n = f.read(buf, sizeof(buf))) > 0) {
        content.append(buf, static_cast<size_t>(n));
    }
    f.close();

    size_t pos = 0;
    while (pos < content.size()) {
        size_t nl = content.find('\n', pos);
        if (nl == std::string::npos) nl = content.size();
        std::string line = content.substr(pos, nl - pos);
        size_t comma = line.find(',');
        if (comma != std::string::npos) {
            size_t chapter = static_cast<size_t>(strtoul(line.substr(0, comma).c_str(), nullptr, 10));
            size_t page = static_cast<size_t>(strtoul(line.substr(comma + 1).c_str(), nullptr, 10));
            bookmarks_.push_back(std::make_pair(chapter, page));
            Serial.printf("loadBookmarksForBook: parsed chapter=%u page=%u\n",
                          static_cast<unsigned>(chapter), static_cast<unsigned>(page));
        }
        pos = nl + 1;
    }
    std::sort(bookmarks_.begin(), bookmarks_.end());
}

std::string EReaderApp::lastReadPathFor(const std::string& bookPath) const {
    std::string name = bookPath;
    size_t slash = name.find_last_of('/');
    if (slash != std::string::npos) name = name.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    return "/bookmarks/" + name + ".lr";
}

bool EReaderApp::loadLastRead(SdFat& sd, const std::string& bookPath, size_t& chapter, size_t& page) const {
    std::string path = lastReadPathFor(bookPath);
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
    Serial.printf("loadLastRead: parsed chapter=%u page=%u\n",
                  static_cast<unsigned>(chapter), static_cast<unsigned>(page));
    return true;
}

void EReaderApp::saveLastRead() {
    if (!manager_ || loadedBook_.empty()) return;
    SdFat& sd = manager_->display().getSdFat();

    if (!sd.exists("/bookmarks")) {
        Serial.println("saveLastRead: creating /bookmarks");
        sd.mkdir("/bookmarks");
    }

    std::string path = lastReadPathFor(loadedBook_);
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

void EReaderApp::updateBookmarkIndex() {
    auto key = std::make_pair(currentChapter_, currentPage_);
    auto it = std::lower_bound(bookmarks_.begin(), bookmarks_.end(), key);
    if (it != bookmarks_.end() && *it == key) {
        bookmarkIdx_ = static_cast<size_t>(it - bookmarks_.begin());
    } else if (!bookmarks_.empty()) {
        bookmarkIdx_ = 0;
    } else {
        bookmarkIdx_ = 0;
    }
    saveLastRead();
}

void EReaderApp::begin(AppManager& manager) {
    App::begin(manager);
    state_ = State::LIBRARY;
    manager_->display().setFullUpdateThreshold(10);
    needsRender_ = true;
    sdOk_ = false;
    booksScanned_ = false;
    bookLoaded_ = false;
    darkMode_ = false;
    status_ = "Library";
    books_.clear();
    bookNames_.clear();
    selectedBook_ = 0;
    loadedBook_.clear();
    pages_.clear();
    currentPage_ = 0;
    currentChapter_ = 0;
    bookmarks_.clear();
    bookmarkIdx_ = 0;
    refreshCount_ = 0;
    reader_.close();
}

void EReaderApp::update(uint32_t dtMs) {
    (void)dtMs;
    if (needsRender_ && manager_) {
        render(manager_->display());
    }
}

void EReaderApp::render(Inkplate& display) {
    display.setTextWrap(false);

    // Landscape library, portrait for reading/menus.
    display.setRotation(state_ == State::LIBRARY ? 0 : 3);

    // Partial refresh only works in black-and-white (1-bit) mode, so this app
    // always uses that. The screensaver is the only 3-bit view.
    display.selectDisplayMode(INKPLATE_1BIT);

    if (!sdOk_) {
        sdOk_ = (display.sdCardInit() == 1);
        if (sdOk_ && !booksScanned_) {
            scanBooks(display.getSdFat());
        }
    }

    clearBackground(display);
    applyColors(display);

    switch (state_) {
        case State::LIBRARY:   drawLibrary(display); break;
        case State::READER:    drawReader(display); break;
        case State::MENU:      drawMenu(display); break;
        case State::EXITING:   drawExiting(display); break;
    }

    bool full = (state_ == State::READER && refreshCount_ == 0) ||
                (state_ != State::READER && state_ != State::LIBRARY);
    if (full) {
        display.display();
    } else {
        display.partialUpdate(INKPLATE_FORCE_PARTIAL, false);
    }
    ++refreshCount_;
    if (refreshCount_ >= FULL_REFRESH_EVERY) refreshCount_ = 0;
    needsRender_ = false;
}

void EReaderApp::drawLibrary(Inkplate& display) {
    display.setFont(TitleFont);
    display.setTextSize(1);
    display.setCursor(MARGIN_X, 30);
    display.print("Library");
    display.drawFastHLine(MARGIN_X, 55, display.width() - 2 * MARGIN_X,
                          darkMode_ ? FG_DARK : FG_LIGHT);

    if (!sdOk_) {
        display.setFont(UIFont);
        display.setTextSize(1);
        display.setCursor(LIST_X, 120);
        display.print("Insert microSD with EPUBs in /books");
        return;
    }

    if (bookNames_.empty()) {
        display.setFont(UIFont);
        display.setTextSize(1);
        display.setCursor(LIST_X, 120);
        display.print("No .epub files in /books");
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

void EReaderApp::drawReader(Inkplate& display) {
    display.setFont(TitleFont);
    display.setTextSize(1);
    display.setCursor(MARGIN_X, 25);
    if (hasBookmark(currentChapter_, currentPage_)) {
        display.print("* ");
    }
    display.print(status_.empty() ? "E-reader" : status_.c_str());
    display.drawFastHLine(MARGIN_X, 55, display.width() - 2 * MARGIN_X,
                          darkMode_ ? FG_DARK : FG_LIGHT);

    if (pages_.empty()) {
        display.setFont(UIFont);
        display.setTextSize(1);
        display.setCursor(MARGIN_X, 120);
        if (status_.find("SD") == 0) {
            display.print("Insert microSD and add EPUBs to /books");
        } else if (status_.find("No") == 0) {
            display.print("No .epub files found in /books");
        } else if (status_.find("Failed") == 0) {
            display.print("Could not open the EPUB file");
        } else {
            display.print("No book loaded");
        }
        return;
    }

    if (currentPage_ >= pages_.size()) currentPage_ = pages_.size() - 1;

    display.setFont(BodyFont);
    display.setTextSize(TEXT_SIZE);
    display.setCursor(EPUB_LEFT_MARGIN, MARGIN_Y);

    const std::string& page = pages_[currentPage_];
    size_t i = 0;
    while (i < page.size()) {
        size_t end = page.find('\n', i);
        if (end == std::string::npos) end = page.size();
        display.setCursor(EPUB_LEFT_MARGIN, display.getCursorY());
        display.println(page.substr(i, end - i).c_str());
        i = end + 1;
        if (display.getCursorY() > display.height() - 30) break;
    }

    char counter[32];
    snprintf(counter, sizeof(counter), "%zu / %zu", currentPage_ + 1, pages_.size());
    display.setFont(PageNumFont);
    display.setTextSize(1);
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(counter, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(display.width() - MARGIN_X - w, display.height() - 30);
    display.print(counter);
}

void EReaderApp::drawMenu(Inkplate& display) {
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
    display.print("tap:  Light / Dark");
    y += 60;
    display.setCursor(MARGIN_X, y);
    display.print("dbl:  Exit");
    y += 60;
    display.setCursor(MARGIN_X, y);
    display.print("hold: Full refresh");
}

void EReaderApp::drawBookmarks(Inkplate& display) {
    display.setFont(TitleFont);
    display.setTextSize(1);
    display.setCursor(MARGIN_X, 30);
    display.print("Bookmarks");
    display.drawFastHLine(MARGIN_X, 55, display.width() - 2 * MARGIN_X,
                          darkMode_ ? FG_DARK : FG_LIGHT);

    display.setFont(UIFont);
    display.setTextSize(1);
    display.setCursor(MARGIN_X, 110);
    char header[64];
    snprintf(header, sizeof(header), "Page %zu", currentPage_ + 1);
    display.print(header);
    if (hasBookmark(currentChapter_, currentPage_)) {
        display.print("  (bookmarked)");
    } else {
        display.print("  (not bookmarked)");
    }

    int16_t y = 170;
    display.setCursor(MARGIN_X, y);
    display.print("tap:  add / remove");
    y += 50;
    display.setCursor(MARGIN_X, y);
    display.print("dbl:  jump to next");
    y += 50;
    display.setCursor(MARGIN_X, y);
    display.print("hold: exit menu");
}

void EReaderApp::drawExiting(Inkplate& display) {
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

void EReaderApp::onButton(ButtonAction action) {
    if (!manager_) return;

    switch (state_) {
        case State::LIBRARY:
            if (!sdOk_) {
                sdOk_ = false;
                needsRender_ = true;
                return;
            }
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
                        loadSelectedBook(manager_->display().getSdFat());
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
                    if (currentPage_ + 1 < pages_.size()) {
                        ++currentPage_;
                        updateBookmarkIndex();
                        needsRender_ = true;
                    } else if (nextChapter()) {
                        needsRender_ = true;
                    } else {
                        // End of the last chapter: clear all bookmarks and mark finished.
                        bookmarks_.clear();
                        bookmarkIdx_ = 0;
                        saveBookmarks();
                        status_ = reader_.title() + " (Finished)";
                        needsRender_ = true;
                    }
                    break;
                case ButtonAction::IO_DOUBLE:
                    if (currentPage_ > 0) {
                        --currentPage_;
                        updateBookmarkIndex();
                        needsRender_ = true;
                    } else if (prevChapter()) {
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
                    darkMode_ = !darkMode_;
                    state_ = State::READER;
                    manager_->display().setFullUpdateThreshold(0);
                    forceFullRefresh();
                    needsRender_ = true;
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
