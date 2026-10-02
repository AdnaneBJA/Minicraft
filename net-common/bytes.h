#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

// Turning numbers and strings into bytes and back, for network messages. Little-endian, so the bytes mean the same
// on every machine.
class ByteWriter {
public:
    void u8(std::uint8_t value) { bytes_.push_back(value); }
    void i32(std::int32_t value) {
        const auto bits = static_cast<std::uint32_t>(value);
        for (int i = 0; i < 4; ++i) bytes_.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
    }
    void u64(std::uint64_t value) {
        for (int i = 0; i < 8; ++i) bytes_.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
    void string(const std::string& text) {
        i32(static_cast<std::int32_t>(text.size()));
        bytes_.insert(bytes_.end(), text.begin(), text.end());
    }
    const std::vector<std::uint8_t>& bytes() const { return bytes_; }

private:
    std::vector<std::uint8_t> bytes_;
};

// Reads what a ByteWriter wrote. The bytes come from the network, so nothing is trusted: reading past the end, or
// a string or list longer than allowed, marks the reader as failed (ok() == false) and returns zeros from then on.
class ByteReader {
public:
    explicit ByteReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    std::uint8_t u8() { return has(1) ? bytes_[position_++] : 0; }
    std::int32_t i32() {
        if (!has(4)) return 0;
        std::uint32_t bits = 0;
        for (int i = 0; i < 4; ++i) bits |= static_cast<std::uint32_t>(bytes_[position_++]) << (8 * i);
        return static_cast<std::int32_t>(bits);
    }
    std::uint64_t u64() {
        if (!has(8)) return 0;
        std::uint64_t value = 0;
        for (int i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(bytes_[position_++]) << (8 * i);
        return value;
    }
    std::string string(int maxLength) {
        const int length = i32();
        if (length < 0 || length > maxLength || !has(static_cast<std::size_t>(length))) {
            ok_ = false;
            return {};
        }
        std::string text(reinterpret_cast<const char*>(bytes_.data() + position_), static_cast<std::size_t>(length));
        position_ += static_cast<std::size_t>(length);
        return text;
    }
    // A list length, checked against what the message allows.
    int count(int max) {
        const int value = i32();
        if (value < 0 || value > max) ok_ = false;
        return ok_ ? value : 0;
    }
    bool ok() const { return ok_; }
    bool atEnd() const { return position_ == bytes_.size(); }

private:
    bool has(std::size_t size) {
        if (!ok_ || bytes_.size() - position_ < size) ok_ = false;
        return ok_;
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t position_ = 0;
    bool ok_ = true;
};
