#ifndef EPUB_READER_H
#define EPUB_READER_H

#include <cstdint>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "unzipLIB.h"
#include "unzip.h"
#include "features/SdFat/SdFat.h"

/**
 * Minimal EPUB text reader.
 *
 * - Loads an EPUB from SD card or a memory buffer.
 * - Parses META-INF/container.xml and the root content.opf.
 * - Extracts the dc:title and the spine order.
 * - Returns plain text of the requested chapter (XHTML stripped).
 * - Image tags are removed; the renderer can show a placeholder if desired.
 */
class EpubReader {
public:
    EpubReader();
    ~EpubReader();

    // Load from an SD file path. The whole EPUB is copied into PSRAM.
    bool loadFromFile(SdFat& sd, const char* path);

    // Load from a memory buffer (caller must keep the buffer alive).
    bool loadFromBuffer(const uint8_t* data, size_t len);

    void close();

    bool isOpen() const { return open_; }

    std::string title();
    size_t chapterCount();
    std::string chapterText(size_t index);

private:
    uint8_t* fileBuffer_ = nullptr;
    size_t   fileSize_   = 0;
    bool     ownsBuffer_ = false;

    UNZIP zip_;
    bool open_ = false;

    std::string contentDir_;  // e.g. "OEBPS/"
    std::string contentOpf_;
    std::map<std::string, std::string> manifest_; // id -> absolute zip path
    std::vector<std::string> spine_;              // idref list
    bool parsed_ = false;

    void ensureParsed();
    std::string extractFile(const char* zipPath);
    static std::string cleanHtml(const std::string& html);
    static std::string decodeEntities(const std::string& text);
    // Fold raw UTF-8 punctuation/accented characters down to ASCII so the
    // GFX font (which only has 0x20-0x7E glyphs) can render them.
    static std::string normalizeUtf8(const std::string& text);
    static std::string extractBetween(const std::string& s, const std::string& start,
                                      const std::string& end);
    static std::string getAttribute(const std::string& tag, const std::string& attr);
    static std::string joinPath(const std::string& base, const std::string& rel);
};

#endif
