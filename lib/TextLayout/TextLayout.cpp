#include "TextLayout.h"
#include <cctype>
#include <sstream>

TextLayout::TextLayout(const std::string& text) {
    setText(text);
}

void TextLayout::setText(const std::string& text) {
    text_ = trim(text);
}

void TextLayout::setMetrics(uint16_t charW, uint16_t charH, uint16_t areaW, uint16_t areaH) {
    charW_ = charW;
    charH_ = charH;
    areaW_ = areaW;
    areaH_ = areaH;
}

std::vector<std::string> TextLayout::pages() const {
    std::vector<std::string> result;
    if (charW_ == 0 || charH_ == 0 || areaW_ == 0 || areaH_ == 0 || text_.empty()) {
        return result;
    }

    uint16_t charsPerLine = areaW_ / charW_;
    uint16_t linesPerPage = areaH_ / charH_;
    if (charsPerLine == 0 || linesPerPage == 0) {
        return result;
    }

    std::vector<std::string> lines = wrapWords(charsPerLine);
    std::ostringstream page;
    uint16_t lineCount = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lineCount > 0) page << '\n';
        page << lines[i];
        ++lineCount;
        if (lineCount == linesPerPage) {
            result.push_back(page.str());
            // Start the next page with the last line of this one.
            std::string carry = lines[i];
            page.str("");
            page.clear();
            page << carry;
            lineCount = 1;
        }
    }
    if (lineCount > 0 && (result.empty() || lineCount > 1)) {
        result.push_back(page.str());
    }
    return result;
}

std::vector<std::string> TextLayout::wrapWords(uint16_t charsPerLine) const {
    std::vector<std::string> out;
    std::string currentLine;
    size_t i = 0;
    const size_t n = text_.size();

    while (i < n) {
        // Treat explicit newlines as line breaks, but do not add blank lines.
        if (text_[i] == '\n') {
            if (!currentLine.empty()) {
                out.push_back(currentLine);
                currentLine.clear();
            }
            ++i;
            continue;
        }

        // Skip spaces.
        while (i < n && std::isspace(static_cast<unsigned char>(text_[i])) && text_[i] != '\n') ++i;
        if (i >= n) break;

        size_t start = i;
        while (i < n && !std::isspace(static_cast<unsigned char>(text_[i])) && text_[i] != '\n') ++i;
        std::string word = text_.substr(start, i - start);
        if (word.empty()) continue;

        if (word.length() > charsPerLine) {
            // Split a long word across lines.
            for (size_t p = 0; p < word.length(); p += charsPerLine) {
                std::string part = word.substr(p, charsPerLine);
                if (currentLine.empty()) {
                    currentLine = part;
                } else if (currentLine.length() + 1 + part.length() <= charsPerLine) {
                    currentLine += ' ';
                    currentLine += part;
                } else {
                    out.push_back(currentLine);
                    currentLine = part;
                }
            }
        } else if (currentLine.empty()) {
            currentLine = word;
        } else if (currentLine.length() + 1 + word.length() <= charsPerLine) {
            currentLine += ' ';
            currentLine += word;
        } else {
            out.push_back(currentLine);
            currentLine = word;
        }
    }
    if (!currentLine.empty()) out.push_back(currentLine);
    return out;
}

std::string TextLayout::trim(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}
