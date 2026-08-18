#include "network/NetStream.h"

#include <cstring>

namespace eve::network {

namespace {

void putLe16(std::vector<char>& buf, uint16_t v) {
    buf.push_back(static_cast<char>(v & 0xff));
    buf.push_back(static_cast<char>((v >> 8) & 0xff));
}

void putLe32(std::vector<char>& buf, uint32_t v) {
    buf.push_back(static_cast<char>(v & 0xff));
    buf.push_back(static_cast<char>((v >> 8) & 0xff));
    buf.push_back(static_cast<char>((v >> 16) & 0xff));
    buf.push_back(static_cast<char>((v >> 24) & 0xff));
}

void putLe64(std::vector<char>& buf, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        buf.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
    }
}

uint16_t getLe16(const char* p) {
    return static_cast<uint16_t>(uint8_t(p[0])) |
           (static_cast<uint16_t>(uint8_t(p[1])) << 8);
}

uint32_t getLe32(const char* p) {
    return static_cast<uint32_t>(uint8_t(p[0])) |
           (static_cast<uint32_t>(uint8_t(p[1])) << 8) |
           (static_cast<uint32_t>(uint8_t(p[2])) << 16) |
           (static_cast<uint32_t>(uint8_t(p[3])) << 24);
}

uint64_t getLe64(const char* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= (uint64_t(uint8_t(p[i])) << (8 * i));
    }
    return v;
}

}  // namespace

void NetWriter::put(const void* d, size_t n) {
    if (!d || n == 0) return;
    const char* p = static_cast<const char*>(d);
    buf_.insert(buf_.end(), p, p + n);
}

void NetWriter::writeU8(uint8_t v) {
    buf_.push_back(static_cast<char>(v));
}

void NetWriter::writeI8(int8_t v) {
    buf_.push_back(static_cast<char>(v));
}

void NetWriter::writeU16(uint16_t v) {
    putLe16(buf_, v);
}

void NetWriter::writeI16(int16_t v) {
    putLe16(buf_, static_cast<uint16_t>(v));
}

void NetWriter::writeU32(uint32_t v) {
    putLe32(buf_, v);
}

void NetWriter::writeI32(int32_t v) {
    putLe32(buf_, static_cast<uint32_t>(v));
}

void NetWriter::writeU64(uint64_t v) {
    putLe64(buf_, v);
}

void NetWriter::writeI64(int64_t v) {
    putLe64(buf_, static_cast<uint64_t>(v));
}

void NetWriter::writeF32(float v) {
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(v), "float must be 32-bit");
    std::memcpy(&bits, &v, sizeof(bits));
    putLe32(buf_, bits);
}

void NetWriter::writeF64(double v) {
    uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(v), "double must be 64-bit");
    std::memcpy(&bits, &v, sizeof(bits));
    putLe64(buf_, bits);
}

void NetWriter::writeBool(bool v) {
    buf_.push_back(v ? 1 : 0);
}

void NetWriter::writeString(const std::string& s) {
    putLe32(buf_, static_cast<uint32_t>(s.size()));
    put(s.data(), s.size());
}

void NetWriter::writeBytes(const void* d, size_t n) {
    putLe32(buf_, static_cast<uint32_t>(n));
    put(d, n);
}

NetReader::NetReader(const void* d, size_t n) {
    init(d, n);
}

NetReader::NetReader(const std::vector<char>& d) {
    init(d.data(), d.size());
}

bool NetReader::init(const void* d, size_t n) {
    if (d == nullptr && n != 0) {
        ok_ = false;
        return false;
    }
    data_ = static_cast<const char*>(d);
    size_ = n;
    pos_ = 0;
    ok_ = true;
    return true;
}

bool NetReader::take(void* out, size_t n) {
    if (!ok_) return false;
    if (n > size_ - pos_) {
        ok_ = false;
        return false;
    }
    if (out && n > 0) std::memcpy(out, data_ + pos_, n);
    pos_ += n;
    return true;
}

size_t NetReader::remaining() const {
    if (!ok_) return 0;
    return size_ >= pos_ ? size_ - pos_ : 0;
}

uint8_t NetReader::u8() {
    uint8_t v = 0;
    take(&v, 1);
    return v;
}

int8_t NetReader::i8() {
    int8_t v = 0;
    take(&v, 1);
    return v;
}

uint16_t NetReader::u16() {
    char tmp[2] = {0, 0};
    if (!take(tmp, 2)) return 0;
    return getLe16(tmp);
}

int16_t NetReader::i16() {
    return static_cast<int16_t>(u16());
}

uint32_t NetReader::u32() {
    char tmp[4] = {0, 0, 0, 0};
    if (!take(tmp, 4)) return 0;
    return getLe32(tmp);
}

int32_t NetReader::i32() {
    return static_cast<int32_t>(u32());
}

uint64_t NetReader::u64() {
    char tmp[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    if (!take(tmp, 8)) return 0;
    return getLe64(tmp);
}

int64_t NetReader::i64() {
    return static_cast<int64_t>(u64());
}

float NetReader::f32() {
    uint32_t bits = u32();
    float v = 0.f;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

double NetReader::f64() {
    uint64_t bits = u64();
    double v = 0.0;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

bool NetReader::b() {
    return u8() != 0;
}

std::string NetReader::str() {
    uint32_t n = u32();
    if (!ok_) return {};
    std::string out;
    out.resize(n);
    if (!take(out.data(), n)) {
        out.clear();
        return out;
    }
    return out;
}

std::vector<char> NetReader::bytes(size_t n) {
    std::vector<char> out;
    uint32_t len = u32();
    if (!ok_ || len != n || n > remaining()) {
        ok_ = false;
        return out;
    }
    out.resize(n);
    if (!take(out.data(), n)) out.clear();
    return out;
}

}  // namespace eve::network
