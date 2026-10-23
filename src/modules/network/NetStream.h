#pragma once
#include "common/Export.h"


#include <cstdint>
#include <string>
#include <vector>

namespace eve::network {

/**
 * Typed little-endian reader/writer for network payloads.
 *
 * Length-prefixed values (writeString / bytes) use a uint32 little-endian
 * length followed by the raw bytes. All fixed-size types are little-endian.
 * NetReader failures are sticky: after any out-of-bounds or malformed read,
 * ok() returns false and subsequent reads safely return defaults.
 */
/** @brief EVENGINE_API_PLATFORM public API. */
class EVENGINE_API_PLATFORM NetWriter {
public:
    /** @brief Net writer. */
    NetWriter() = default;

    /** @brief Writes u 8. */
    void writeU8(uint8_t v);
    /** @brief Writes i 8. */
    void writeI8(int8_t v);
    /** @brief Writes u 16. */
    void writeU16(uint16_t v);
    /** @brief Writes i 16. */
    void writeI16(int16_t v);
    /** @brief Writes u 32. */
    void writeU32(uint32_t v);
    /** @brief Writes i 32. */
    void writeI32(int32_t v);
    /** @brief Writes u 64. */
    void writeU64(uint64_t v);
    /** @brief Writes i 64. */
    void writeI64(int64_t v);
    /** @brief Writes f 32. */
    void writeF32(float v);
    /** @brief Writes f 64. */
    void writeF64(double v);
    /** @brief Writes bool. */
    void writeBool(bool v);
    /** @brief Writes string. */
    void writeString(const std::string& s);
    /** @brief Writes bytes. */
    void writeBytes(const void* d, size_t n);

    /** @brief Returns the size of . */
    size_t size() const { return buf_.size(); }
    /** @brief Data. */
    const char* data() const { return buf_.data(); }
    /** @brief Buffer. */
    const std::vector<char>& buffer() const { return buf_; }
    /** @brief To string. */
    std::string toString() const {
        /** @brief String. */
        return std::string(buf_.data(), buf_.size());
    }

private:
    void put(const void* d, size_t n);

    std::vector<char> buf_;
};

/** @brief EVENGINE_API_PLATFORM public API. */
class EVENGINE_API_PLATFORM NetReader {
public:
    /** @brief Net reader. */
    NetReader() = default;
    /** @brief Net reader. */
    NetReader(const void* d, size_t n);
    /** @brief Net reader. */
    explicit NetReader(const std::vector<char>& d);

    /** Returns false when the buffer is null/empty-invalid; ok() tracks state. */
    /** @brief Initializes . */
    bool init(const void* d, size_t n);
    /** @brief Sets the bytes. */
    bool setBytes(const std::string& s) { return init(s.data(), s.size()); }

    /** @brief U 8. */
    uint8_t  u8();
    /** @brief I 8. */
    int8_t   i8();
    /** @brief U 16. */
    uint16_t u16();
    /** @brief I 16. */
    int16_t  i16();
    /** @brief U 32. */
    uint32_t u32();
    /** @brief I 32. */
    int32_t  i32();
    /** @brief U 64. */
    uint64_t u64();
    /** @brief I 64. */
    int64_t  i64();
    /** @brief F 32. */
    float    f32();
    /** @brief F 64. */
    double   f64();
    /** @brief B. */
    bool     b();
    /** @brief Str. */
    std::string str();
    /** @brief Bytes. */
    std::vector<char> bytes(size_t n);

    /** @brief Remaining. */
    size_t remaining() const;
    /** @brief Pos. */
    size_t pos() const { return pos_; }
    /** @brief Ok. */
    bool ok() const { return ok_; }

private:
    bool take(void* out, size_t n);

    const char* data_ = nullptr;
    size_t size_ = 0;
    size_t pos_ = 0;
    bool ok_ = false;
};

}  // namespace eve::network
