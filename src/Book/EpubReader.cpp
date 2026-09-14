#include "EpubReader.h"
#include <Arduino.h>
#include <esp32-hal-psram.h>
#include "features/SdFat/SdFat.h"

namespace {
    SdFat* gZipSd = nullptr;

    void* zipOpenCb(const char* path, int32_t* pFileSize) {
        if (!gZipSd) return nullptr;
        FsFile* f = new (std::nothrow) FsFile;
        if (!f) return nullptr;
        if (!f->open(gZipSd, path, O_RDONLY)) {
            delete f;
            return nullptr;
        }
        *pFileSize = static_cast<int32_t>(f->fileSize());
        return f;
    }

    void zipCloseCb(void* pFile) {
        ZIPFILE* pzf = static_cast<ZIPFILE*>(pFile);
        if (!pzf || !pzf->fHandle) return;
        FsFile* f = static_cast<FsFile*>(pzf->fHandle);
        f->close();
        delete f;
        pzf->fHandle = nullptr;
    }

    int32_t zipReadCb(void* pFile, uint8_t* pBuf, int32_t iLen) {
        ZIPFILE* pzf = static_cast<ZIPFILE*>(pFile);
        if (!pzf || !pzf->fHandle || iLen <= 0) return 0;
        FsFile* f = static_cast<FsFile*>(pzf->fHandle);
        return static_cast<int32_t>(f->read(pBuf, static_cast<size_t>(iLen)));
    }

    int32_t zipSeekCb(void* pFile, int32_t iPosition, int iType) {
        ZIPFILE* pzf = static_cast<ZIPFILE*>(pFile);
        if (!pzf || !pzf->fHandle) return -1;
        FsFile* f = static_cast<FsFile*>(pzf->fHandle);
        bool ok = false;
        if (iType == SEEK_SET) {
            ok = f->seekSet(static_cast<uint64_t>(iPosition));
        } else if (iType == SEEK_CUR) {
            int64_t newPos = static_cast<int64_t>(f->curPosition()) + static_cast<int64_t>(iPosition);
            ok = newPos >= 0 && f->seekSet(static_cast<uint64_t>(newPos));
        } else if (iType == SEEK_END) {
            int64_t newPos = static_cast<int64_t>(f->fileSize()) + static_cast<int64_t>(iPosition);
            ok = newPos >= 0 && f->seekSet(static_cast<uint64_t>(newPos));
        }
        return ok ? 0 : -1;
    }

    // Case-insensitive find of a substring. Returns npos if not found.
    size_t findICase(const std::string& s, const std::string& needle, size_t pos = 0) {
        if (needle.empty() || s.size() < needle.size()) return std::string::npos;
        for (size_t i = pos; i + needle.size() <= s.size(); ++i) {
            bool match = true;
            for (size_t j = 0; j < needle.size(); ++j) {
                if (std::tolower(static_cast<unsigned char>(s[i + j])) !=
                    std::tolower(static_cast<unsigned char>(needle[j]))) {
                    match = false;
                    break;
                }
            }
            if (match) return i;
        }
        return std::string::npos;
    }

    // Trim leading/trailing whitespace.
    std::string trim(const std::string& s) {
        size_t a = 0;
        while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
        size_t b = s.size();
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
        return s.substr(a, b - a);
    }

    // Remove contents between <tag ...>...</tag> (case-insensitive).
    std::string removeBlocks(const std::string& s, const std::string& tag) {
        std::string out;
        out.reserve(s.size());
        size_t i = 0;
        const std::string openStart = "<" + tag;
        const std::string closeTag  = "</" + tag;
        while (i < s.size()) {
            size_t start = findICase(s, openStart, i);
            if (start == std::string::npos) {
                out.append(s.substr(i));
                break;
            }
            out.append(s.substr(i, start - i));
            size_t end = findICase(s, closeTag, start);
            if (end == std::string::npos) {
                // malformed: strip the rest
                i = s.size();
                break;
            }
            size_t closeEnd = s.find('>', end);
            if (closeEnd == std::string::npos) {
                i = s.size();
                break;
            }
            i = closeEnd + 1;
        }
        return out;
    }

    // Replace inline <img ...> tags with a placeholder string before tags are stripped.
    std::string replaceTag(const std::string& s, const std::string& tag, const std::string& replacement) {
        std::string out;
        out.reserve(s.size());
        size_t i = 0;
        const std::string openStart = "<" + tag;
        while (i < s.size()) {
            size_t start = findICase(s, openStart, i);
            if (start == std::string::npos) {
                out.append(s.substr(i));
                break;
            }
            out.append(s.substr(i, start - i));
            size_t end = s.find('>', start);
            if (end == std::string::npos) {
                i = s.size();
                break;
            }
            out += replacement;
            i = end + 1;
        }
        return out;
    }

    // Collapse runs of whitespace/newlines into a single space, preserving explicit newlines.
    std::string collapseSpace(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        bool inSpace = true;
        for (char c : s) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (c == '\n') {
                    if (!out.empty() && out.back() != '\n') out += '\n';
                    inSpace = true;
                } else if (!inSpace) {
                    out += ' ';
                    inSpace = true;
                }
            } else {
                out += c;
                inSpace = false;
            }
        }
        return trim(out);
    }
}

EpubReader::EpubReader() {}

EpubReader::~EpubReader() {
    close();
}

bool EpubReader::loadFromFile(SdFat& sd, const char* path) {
    close();

    // Stream the ZIP directly from the SD card; do not load the whole EPUB into RAM.
    gZipSd = &sd;
    open_ = (zip_.openZIP(path, zipOpenCb, zipCloseCb, zipReadCb, zipSeekCb) == UNZ_OK);
    gZipSd = nullptr;

    if (!open_) {
        Serial.printf("EpubReader: cannot open %s\n", path);
        return false;
    }
    return true;
}

bool EpubReader::loadFromBuffer(const uint8_t* data, size_t len) {
    close();
    if (!data || len == 0) return false;

    open_ = (zip_.openZIP(const_cast<uint8_t*>(data), static_cast<int>(len)) == UNZ_OK);
    if (!open_) {
        Serial.println("EpubReader: not a valid ZIP/EPUB");
        return false;
    }

    fileBuffer_ = const_cast<uint8_t*>(data);
    fileSize_ = len;
    ownsBuffer_ = false;
    return true;
}

void EpubReader::close() {
    if (open_) {
        zip_.closeZIP();
        open_ = false;
    }
    if (fileBuffer_ && ownsBuffer_) {
        free(fileBuffer_);
    }
    fileBuffer_ = nullptr;
    fileSize_ = 0;
    ownsBuffer_ = false;
    contentDir_.clear();
    contentOpf_.clear();
    manifest_.clear();
    spine_.clear();
    parsed_ = false;
}

std::string EpubReader::title() {
    ensureParsed();
    if (contentOpf_.empty()) return "Unknown title";
    return extractBetween(contentOpf_, "<dc:title>", "</dc:title>");
}

size_t EpubReader::chapterCount() {
    ensureParsed();
    return spine_.size();
}

std::string EpubReader::chapterText(size_t index) {
    ensureParsed();
    if (index >= spine_.size()) return "";

    auto it = manifest_.find(spine_[index]);
    if (it == manifest_.end()) return "";

    std::string html = extractFile(it->second.c_str());
    return cleanHtml(html);
}

void EpubReader::ensureParsed() {
    if (parsed_) return;
    parsed_ = true;
    if (!open_) return;

    std::string container = extractFile("META-INF/container.xml");
    if (container.empty()) {
        Serial.println("EpubReader: no container.xml");
        return;
    }

    // Use a start string that cannot match the enclosing <rootfiles> tag.
    std::string fullPath = getAttribute(extractBetween(container, "<rootfile ", ">"), "full-path");
    if (fullPath.empty()) {
        fullPath = getAttribute(extractBetween(container, "<rootfile", ">"), "full-path");
    }
    if (fullPath.empty()) {
        fullPath = "OEBPS/content.opf";  // common fallback
    }

    size_t slash = fullPath.find_last_of("/\\");
    contentDir_ = (slash == std::string::npos) ? "" : fullPath.substr(0, slash + 1);

    contentOpf_ = extractFile(fullPath.c_str());
    if (contentOpf_.empty()) {
        Serial.printf("EpubReader: cannot read %s\n", fullPath.c_str());
        return;
    }

    // Parse manifest: <item id="..." href="..." media-type="..."/>
    std::string manifestBlock = extractBetween(contentOpf_, "<manifest>", "</manifest>");
    size_t pos = 0;
    while (pos < manifestBlock.size()) {
        size_t tagStart = manifestBlock.find("<item ", pos);
        if (tagStart == std::string::npos) break;
        size_t tagEnd = manifestBlock.find(">", tagStart);
        if (tagEnd == std::string::npos) break;
        std::string tag = manifestBlock.substr(tagStart, tagEnd - tagStart + 1);
        std::string id = getAttribute(tag, "id");
        std::string href = getAttribute(tag, "href");
        if (!id.empty() && !href.empty()) {
            manifest_[id] = joinPath(contentDir_, href);
        }
        pos = tagEnd + 1;
    }

    // Parse spine: <itemref idref="..."/>
    std::string spineBlock = extractBetween(contentOpf_, "<spine", "</spine>");
    pos = 0;
    while (pos < spineBlock.size()) {
        size_t tagStart = spineBlock.find("<itemref ", pos);
        if (tagStart == std::string::npos) break;
        size_t tagEnd = spineBlock.find(">", tagStart);
        if (tagEnd == std::string::npos) break;
        std::string tag = spineBlock.substr(tagStart, tagEnd - tagStart + 1);
        std::string idref = getAttribute(tag, "idref");
        if (!idref.empty()) {
            spine_.push_back(idref);
        }
        pos = tagEnd + 1;
    }

    if (spine_.empty()) {
        // Fallback: if no spine, present all text/html manifest items in order.
        for (const auto& kv : manifest_) {
            if (kv.second.find(".htm") != std::string::npos ||
                kv.second.find(".xhtml") != std::string::npos ||
                kv.second.find(".html") != std::string::npos) {
                spine_.push_back(kv.first);
            }
        }
    }
}

std::string EpubReader::extractFile(const char* zipPath) {
    if (!open_) return "";

    if (zip_.locateFile(zipPath) != UNZ_OK) {
        Serial.printf("EpubReader: cannot locate %s\n", zipPath);
        return "";
    }

    if (zip_.openCurrentFile() != UNZ_OK) {
        Serial.printf("EpubReader: cannot open %s\n", zipPath);
        return "";
    }

    unz_file_info info = {};
    if (zip_.getFileInfo(&info, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK) {
        zip_.closeCurrentFile();
        return "";
    }

    size_t len = static_cast<size_t>(info.uncompressed_size);
    if (len == 0) {
        zip_.closeCurrentFile();
        return "";
    }

    uint8_t* p = (uint8_t*)ps_malloc(len + 1);
    if (!p) {
        zip_.closeCurrentFile();
        Serial.println("EpubReader: PSRAM alloc failed for extraction");
        return "";
    }

    int bytes = zip_.readCurrentFile(p, static_cast<int>(len));
    p[len] = '\0';
    zip_.closeCurrentFile();

    if (bytes < 0) {
        Serial.printf("EpubReader: read error %d for %s\n", bytes, zipPath);
        free(p);
        return "";
    }

    std::string s(reinterpret_cast<const char*>(p), static_cast<size_t>(bytes));
    free(p);
    return s;
}

std::string EpubReader::cleanHtml(const std::string& html) {
    std::string s = html;
    s = removeBlocks(s, "script");
    s = removeBlocks(s, "style");
    s = replaceTag(s, "img", " [no image] ");

    // Convert some block/line break tags into newlines.
    static const char* blockTags[] = {
        "</p>", "</div>", "</h1>", "</h2>", "</h3>", "</h4>",
        "</h5>", "</h6>", "</li>", "</td>", "</tr>", "</br>",
        "<br", "<br/", "<p", "<div", "<h1", "<h2", "<h3",
        "<h4", "<h5", "<h6", "<li", "<tr",
    };
    for (const char* tag : blockTags) {
        size_t pos = 0;
        while ((pos = findICase(s, tag, pos)) != std::string::npos) {
            size_t end = s.find(">", pos);
            if (end == std::string::npos) {
                s.erase(pos);
                break;
            }
            s.replace(pos, end - pos + 1, "\n");
            pos += 1;
        }
    }

    // Remove all remaining tags.
    std::string out;
    out.reserve(s.size());
    bool inTag = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '<') {
            inTag = true;
        } else if (c == '>') {
            inTag = false;
        } else if (!inTag) {
            out += c;
        }
    }

    out = decodeEntities(out);
    out = normalizeUtf8(out);
    return collapseSpace(out);
}

std::string EpubReader::decodeEntities(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '&') {
            size_t end = text.find(';', i);
            if (end == std::string::npos) {
                out += text[i++];
                continue;
            }
            std::string ent = text.substr(i + 1, end - i - 1);
            std::string lower;
            lower.reserve(ent.size());
            for (char c : ent) lower += static_cast<char>(std::tolower(c));

            if (lower == "amp") out += '&';
            else if (lower == "lt") out += '<';
            else if (lower == "gt") out += '>';
            else if (lower == "quot") out += '"';
            else if (lower == "apos") out += '\'';
            else if (lower == "nbsp") out += ' ';
            else if (lower == "mdash") out += "--";
            else if (lower == "ndash") out += "-";
            else if (lower == "ldquo" || lower == "rdquo" || lower == "quo") out += '"';
            else if (lower == "lsquo" || lower == "rsquo") out += '\'';
            else if (lower == "hellip") out += "...";
            else if (lower == "em" || lower == "en") out += ' ';
            else if (lower == "#x201c" || lower == "#x201d") out += '"';
            else if (lower == "#x2018" || lower == "#x2019") out += '\'';
            else if (lower == "#x2014") out += "--";
            else if (lower == "#x2013") out += "-";
            else if (lower == "#x2026") out += "...";
            else if (lower == "#xa0") out += ' ';
            else if (!ent.empty() && ent[0] == '#') {
                // numeric entity
                long cp = 0;
                if (ent.size() > 1 && ent[1] == 'x') {
                    cp = strtol(ent.c_str() + 2, nullptr, 16);
                } else {
                    cp = strtol(ent.c_str() + 1, nullptr, 10);
                }
                if (cp > 0 && cp < 128) {
                    out += static_cast<char>(cp);
                } else if (cp >= 32 && cp < 0x10000) {
                    // Basic replacement for common punctuation; otherwise skip.
                    switch (cp) {
                        case 0x2018: case 0x2019: out += '\''; break;
                        case 0x201C: case 0x201D: out += '"'; break;
                        case 0x2013: out += '-'; break;
                        case 0x2014: out += "--"; break;
                        case 0x2026: out += "..."; break;
                        case 0xA0: out += ' '; break;
                        default: out += ' '; break;
                    }
                }
            } else {
                out += '&';
                out += ent;
                out += ';';
            }
            i = end + 1;
        } else {
            out += text[i++];
        }
    }
    return out;
}

// Map a non-ASCII code point to an ASCII string the GFX font can render, or
// nullptr if there is no reasonable equivalent (the character is dropped).
static const char* cpToAscii(uint32_t cp) {
    switch (cp) {
        // Single quotes / apostrophes / primes.
        case 0x2018: case 0x2019: case 0x201A: case 0x201B:
        case 0x2032: case 0x02BC: case 0x0060: case 0x00B4:
            return "'";
        // Double quotes / double primes.
        case 0x201C: case 0x201D: case 0x201E: case 0x201F:
        case 0x2033: case 0x00AB: case 0x00BB:
            return "\"";
        // Dashes and minus signs.
        case 0x2012: case 0x2013: case 0x2015: case 0x2212:
            return "-";
        case 0x2014:
            return "--";
        // Ellipsis.
        case 0x2026:
            return "...";
        // Spaces.
        case 0x00A0: case 0x2000: case 0x2001: case 0x2002: case 0x2003:
        case 0x2004: case 0x2005: case 0x2006: case 0x2007: case 0x2008:
        case 0x2009: case 0x200A: case 0x202F: case 0x205F: case 0x3000:
            return " ";
        // Bullets / middots.
        case 0x2022: case 0x00B7: case 0x2027: case 0x2219:
            return "*";
        // Ligatures.
        case 0xFB00: return "ff"; case 0xFB01: return "fi";
        case 0xFB02: return "fl"; case 0xFB03: return "ffi";
        case 0xFB04: return "ffl";
        // Copyright / registered / trademark.
        case 0x00A9: return "(c)"; case 0x00AE: return "(R)";
        case 0x2122: return "TM";
        // Currency / maths that have rough ASCII forms.
        case 0x00A3: return "GBP"; case 0x20AC: return "EUR";
        case 0x00A5: return "YEN"; case 0x00A2: return "c";
        case 0x00D7: return "x";  case 0x00F7: return "/";
        case 0x00B0: return "deg"; case 0x00B1: return "+/-";
        case 0x00BC: return "1/4"; case 0x00BD: return "1/2";
        case 0x00BE: return "3/4";
        default: break;
    }

    // Accented Latin letters -> base letter (Latin-1 Supplement + a few
    // Latin Extended-A characters common in European languages).
    switch (cp) {
        case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: case 0x00C4:
        case 0x00C5: case 0x0100: case 0x0102: case 0x0104: return "A";
        case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x00E4:
        case 0x00E5: case 0x0101: case 0x0103: case 0x0105: return "a";
        case 0x00C6: return "AE"; case 0x00E6: return "ae";
        case 0x00C7: case 0x0106: case 0x0108: case 0x010A: case 0x010C: return "C";
        case 0x00E7: case 0x0107: case 0x0109: case 0x010B: case 0x010D: return "c";
        case 0x00D0: case 0x010E: case 0x0110: return "D";
        case 0x00F0: case 0x010F: case 0x0111: return "d";
        case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB:
        case 0x0112: case 0x0114: case 0x0116: case 0x0118: case 0x011A: return "E";
        case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB:
        case 0x0113: case 0x0115: case 0x0117: case 0x0119: case 0x011B: return "e";
        case 0x011C: case 0x011E: case 0x0120: case 0x0122: return "G";
        case 0x011D: case 0x011F: case 0x0121: case 0x0123: return "g";
        case 0x0124: case 0x0126: return "H"; case 0x0125: case 0x0127: return "h";
        case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF:
        case 0x0128: case 0x012A: case 0x012C: case 0x012E: case 0x0130: return "I";
        case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF:
        case 0x0129: case 0x012B: case 0x012D: case 0x012F: case 0x0131: return "i";
        case 0x0134: return "J"; case 0x0135: return "j";
        case 0x0136: return "K"; case 0x0137: return "k";
        case 0x0139: case 0x013B: case 0x013D: case 0x013F: case 0x0141: return "L";
        case 0x013A: case 0x013C: case 0x013E: case 0x0140: case 0x0142: return "l";
        case 0x00D1: case 0x0143: case 0x0145: case 0x0147: return "N";
        case 0x00F1: case 0x0144: case 0x0146: case 0x0148: return "n";
        case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: case 0x00D6:
        case 0x00D8: case 0x014C: case 0x014E: case 0x0150: return "O";
        case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x00F6:
        case 0x00F8: case 0x014D: case 0x014F: case 0x0151: return "o";
        case 0x0152: return "OE"; case 0x0153: return "oe";
        case 0x0154: case 0x0156: case 0x0158: return "R";
        case 0x0155: case 0x0157: case 0x0159: return "r";
        case 0x015A: case 0x015C: case 0x015E: case 0x0160: return "S";
        case 0x015B: case 0x015D: case 0x015F: case 0x0161: return "s";
        case 0x00DE: return "TH"; case 0x00FE: return "th";
        case 0x0162: case 0x0164: case 0x0166: return "T";
        case 0x0163: case 0x0165: case 0x0167: return "t";
        case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC:
        case 0x0168: case 0x016A: case 0x016C: case 0x016E: case 0x0170:
        case 0x0172: return "U";
        case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC:
        case 0x0169: case 0x016B: case 0x016D: case 0x016F: case 0x0171:
        case 0x0173: return "u";
        case 0x0174: return "W"; case 0x0175: return "w";
        case 0x00DD: case 0x0176: case 0x0178: return "Y";
        case 0x00FD: case 0x00FF: case 0x0177: return "y";
        case 0x0179: case 0x017B: case 0x017D: return "Z";
        case 0x017A: case 0x017C: case 0x017E: return "z";
        case 0x00DF: return "ss";
        default: return nullptr;
    }
}

std::string EpubReader::normalizeUtf8(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {                 // ASCII already
            out += static_cast<char>(c);
            ++i;
            continue;
        }
        // Decode one UTF-8 code point.
        uint32_t cp = 0;
        size_t len = 0;
        if      ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else { ++i; continue; }        // stray continuation byte — drop it
        if (i + len > text.size()) break;
        bool ok = true;
        for (size_t k = 1; k < len; ++k) {
            unsigned char cc = static_cast<unsigned char>(text[i + k]);
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) { ++i; continue; }
        i += len;

        if (const char* ascii = cpToAscii(cp)) {
            out += ascii;
        }
        // else: no ASCII equivalent — drop the character.
    }
    return out;
}

std::string EpubReader::extractBetween(const std::string& s, const std::string& start,
                                       const std::string& end) {
    size_t a = s.find(start);
    if (a == std::string::npos) return "";
    a += start.size();
    size_t b = s.find(end, a);
    if (b == std::string::npos) b = s.size();
    return trim(s.substr(a, b - a));
}

std::string EpubReader::getAttribute(const std::string& tag, const std::string& attr) {
    std::string key = attr + "=";
    size_t pos = findICase(tag, key);
    if (pos == std::string::npos) return "";
    pos += key.size();
    if (pos >= tag.size()) return "";

    char quote = tag[pos];
    if (quote != '"' && quote != '\'') {
        // Unquoted attribute; read until next space or end.
        size_t end = tag.find_first_of(" \t\r\n>", pos);
        if (end == std::string::npos) end = tag.size();
        return tag.substr(pos, end - pos);
    }
    ++pos;
    size_t end = tag.find(quote, pos);
    if (end == std::string::npos) end = tag.size();
    return tag.substr(pos, end - pos);
}

std::string EpubReader::joinPath(const std::string& base, const std::string& rel) {
    if (rel.empty()) return base;
    if (rel[0] == '/') {
        // Path relative to EPUB root, not package dir.
        std::string r = rel.substr(1);
        if (!base.empty() && !r.empty() && r.find('/') == std::string::npos) {
            // Try package dir as a reasonable default for flat EPUBs.
            return base + r;
        }
        return r;
    }
    if (rel.size() >= 2 && rel[0] == '.' && rel[1] == '/') {
        return base + rel.substr(2);
    }
    if (rel.substr(0, 3) == "../") {
        // Simplify: strip base trailing dir and append.
        std::string b = base;
        if (!b.empty() && (b.back() == '/' || b.back() == '\\')) b.pop_back();
        size_t slash = b.find_last_of("/\\");
        if (slash != std::string::npos) b = b.substr(0, slash + 1);
        std::string r = rel.substr(3);
        return b + r;
    }
    return base + rel;
}
