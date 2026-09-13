#include "SerialFileReceiver.h"

#include <Arduino.h>
#include "features/SdFat/SdFat.h"
#include "Inkplate.h"

namespace {
    constexpr size_t CHUNK_SIZE = 2048;
    uint8_t chunkBuf[CHUNK_SIZE];
}

SerialFileReceiver::SerialFileReceiver() = default;

bool SerialFileReceiver::poll(Inkplate& display) {
    if (state_ == State::IDLE || state_ == State::WAIT_CHUNK) {
        while (Serial.available()) {
            int c = Serial.read();
            if (c < 0) break;
            if (c == '\n' || c == '\r') {
                if (!lineBuf_.empty()) {
                    bool transitioned = false;
                    if (state_ == State::IDLE) {
                        transitioned = processCommand(display);
                    } else {
                        transitioned = processChunkHeader();
                    }
                    lineBuf_.clear();
                    if (transitioned) break;
                }
            } else if (lineBuf_.size() < 160) {
                lineBuf_ += static_cast<char>(c);
            } else {
                lineBuf_.clear();
            }
        }
    }
    if (state_ == State::READ_CHUNK) {
        readChunkData();
    }
    return state_ == State::WAIT_CHUNK || state_ == State::READ_CHUNK;
}

bool SerialFileReceiver::busy() const {
    return state_ == State::WAIT_CHUNK || state_ == State::READ_CHUNK;
}

uint32_t SerialFileReceiver::received() const { return received_; }
uint32_t SerialFileReceiver::totalSize() const { return totalSize_; }

void SerialFileReceiver::reset() {
    state_ = State::IDLE;
    path_.clear();
    totalSize_ = 0;
    received_ = 0;
    chunkNeeded_ = 0;
    lineBuf_.clear();
    error_.clear();
    if (file_.isOpen()) {
        file_.close();
    }
}

bool SerialFileReceiver::pathIsSafe(const std::string& path) const {
    if (path.empty() || path[0] != '/') return false;
    if (path.find("..") != std::string::npos) return false;
    return true;
}

void SerialFileReceiver::fail(const char* msg) {
    state_ = State::ERROR;
    error_ = msg;
    Serial.print("ERROR ");
    Serial.println(msg);
    if (file_.isOpen()) {
        file_.close();
    }
}

bool SerialFileReceiver::processCommand(Inkplate& display) {
    if (lineBuf_.size() >= 4 && lineBuf_.compare(0, 4, "PING") == 0) {
        Serial.println("PONG");
        return false;
    }

    if (lineBuf_.size() >= 7 && lineBuf_.compare(0, 7, "SETTIME") == 0) {
        if (lineBuf_.size() <= 8 || lineBuf_[7] != ' ') {
            fail("usage: SETTIME <epoch>");
            return false;
        }
        uint32_t epoch = static_cast<uint32_t>(strtoul(lineBuf_.c_str() + 8, nullptr, 10));
        display.rtc.setEpoch(epoch);
        Serial.print("TIME SET ");
        Serial.println(epoch);
        return false;
    }

    if (lineBuf_.size() < 6 || lineBuf_.compare(0, 6, "UPLOAD") != 0 ||
        (lineBuf_.size() > 6 && lineBuf_[6] != ' ')) {
        return false;
    }

    size_t pathStart = 7;
    if (lineBuf_.size() <= pathStart) {
        fail("missing path");
        return false;
    }
    size_t lastSpace = lineBuf_.rfind(' ');
    if (lastSpace == std::string::npos || lastSpace <= pathStart) {
        fail("missing size");
        return false;
    }
    std::string path = lineBuf_.substr(pathStart, lastSpace - pathStart);
    std::string sizeStr = lineBuf_.substr(lastSpace + 1);

    if (!pathIsSafe(path)) {
        fail("unsafe path");
        return false;
    }
    uint32_t size = static_cast<uint32_t>(atol(sizeStr.c_str()));
    if (size == 0) {
        fail("invalid size");
        return false;
    }

    if (display.sdCardInit() != 1) {
        fail("no SD card");
        return false;
    }

    SdFat& sd = display.getSdFat();

    // Create any missing parent directories for the target path.
    std::string dir = path;
    size_t slash = dir.find_last_of('/');
    if (slash != std::string::npos) {
        dir = dir.substr(0, slash);
        if (!dir.empty()) {
            sd.mkdir(dir.c_str(), true);
        }
    }

    if (file_.isOpen()) {
        file_.close();
    }
    if (!file_.open(path.c_str(), O_WRITE | O_CREAT | O_TRUNC)) {
        fail("open failed");
        return false;
    }

    path_ = path;
    totalSize_ = size;
    received_ = 0;
    chunkNeeded_ = 0;
    state_ = State::WAIT_CHUNK;
    Serial.println("READY");
    return true;
}

bool SerialFileReceiver::processChunkHeader() {
    if (lineBuf_.size() < 6 || lineBuf_.compare(0, 6, "CHUNK ") != 0) {
        fail("expected CHUNK");
        return false;
    }
    uint32_t n = static_cast<uint32_t>(atol(lineBuf_.c_str() + 6));
    if (n == 0 || n > CHUNK_SIZE) {
        fail("bad chunk size");
        return false;
    }
    if (received_ + n > totalSize_) {
        fail("chunk exceeds total");
        return false;
    }
    chunkNeeded_ = n;
    state_ = State::READ_CHUNK;
    return true;
}

void SerialFileReceiver::readChunkData() {
    if (!file_.isOpen()) {
        fail("no file");
        return;
    }

    size_t n = 0;
    while (n < chunkNeeded_ && Serial.available()) {
        int c = Serial.read();
        if (c < 0) break;
        chunkBuf[n++] = static_cast<uint8_t>(c);
        if ((n & 63) == 0) {
            yield();
        }
    }

    if (n == 0) return;

    size_t written = file_.write(chunkBuf, n);
    if (written != n) {
        fail("write failed");
        return;
    }

    received_ += n;
    chunkNeeded_ -= n;

    if (chunkNeeded_ == 0) {
        Serial.println("ACK");
        if (received_ >= totalSize_) {
            file_.close();
            Serial.print("OK ");
            Serial.println(received_);
            reset();
        } else {
            state_ = State::WAIT_CHUNK;
        }
    }
}
