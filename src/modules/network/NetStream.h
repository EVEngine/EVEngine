#pragma once

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
class NetWriter {
public:
    NetWriter() = default;

    void writeU8(uint8_t v);
    void writeI8(int8_t v);
    void writeU16(uint16_t v);
    void writeI16(int16_t v);
    void writeU32(uint32_t v);
    void writeI32(int32_t v);
    void writeU64(uint64_t v);
    void writeI64(int64_t v);
    void writeF32(float v);
    void writeF64(double v);
    void writeBool(bool v);
    void writeString(const std::string& s);
    void writeBytes(const void* d, size_t n);

    size_t size() const { return buf_.size(); }
    const char* data() const { return buf_.data(); }
    const std::vector<char>& buffer() const { return buf_; }
    std::string toString() const {
        return std::string(buf_.data(), buf_.size());
    }

private:
    void put(const void* d, size_t n);

    std::vector<char> buf_;
};

class NetReader {
public:
    NetReader() = default;
    NetReader(const void* d, size_t n);
    explicit NetReader(const std::vector<char>& d);

    /** Returns false when the buffer is null/empty-invalid; ok() tracks state. */
    bool init(const void* d, size_t n);
    bool setBytes(const std::string& s) { return init(s.data(), s.size()); }

    uint8_t  u8();
    int8_t   i8();
    uint16_t u16();
    int16_t  i16();
    uint32_t u32();
    int32_t  i32();
    uint64_t u64();
    int64_t  i64();
    float    f32();
    double   f64();
    bool     b();
    std::string str();
    std::vector<char> bytes(size_t n);

    size_t remaining() const;
    size_t pos() const { return pos_; }
    bool ok() const { return ok_; }

private:
    bool take(void* out, size_t n);

    const char* data_ = nullptr;
    size_t size_ = 0;
    size_t pos_ = 0;
    bool ok_ = false;
};

}  // namespace eve::network
