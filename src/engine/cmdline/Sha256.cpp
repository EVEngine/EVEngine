#include "cmdline/Sha256.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace eve::cmd::sdk::detail {

namespace {

constexpr std::array<uint32_t, 64> kConstants = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

constexpr uint32_t rotateRight(uint32_t value, unsigned count) { return (value >> count) | (value << (32u - count)); }

}  // namespace

void Sha256::transform(const uint8_t* block) {
    std::array<uint32_t, 64> words{};
    for (std::size_t i = 0; i < 16; ++i) {
        const std::size_t offset = i * 4;
        words[i] = (static_cast<uint32_t>(block[offset]) << 24u) | (static_cast<uint32_t>(block[offset + 1]) << 16u) |
                   (static_cast<uint32_t>(block[offset + 2]) << 8u) | static_cast<uint32_t>(block[offset + 3]);
    }
    for (std::size_t i = 16; i < words.size(); ++i) {
        const uint32_t s0 = rotateRight(words[i - 15], 7) ^ rotateRight(words[i - 15], 18) ^ (words[i - 15] >> 3u);
        const uint32_t s1 = rotateRight(words[i - 2], 17) ^ rotateRight(words[i - 2], 19) ^ (words[i - 2] >> 10u);
        words[i]          = words[i - 16] + s0 + words[i - 7] + s1;
    }

    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];
    uint32_t e = state_[4];
    uint32_t f = state_[5];
    uint32_t g = state_[6];
    uint32_t h = state_[7];
    for (std::size_t i = 0; i < words.size(); ++i) {
        const uint32_t sum1     = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
        const uint32_t choice   = (e & f) ^ (~e & g);
        const uint32_t temp1    = h + sum1 + choice + kConstants[i] + words[i];
        const uint32_t sum0     = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2    = sum0 + majority;
        h                       = g;
        g                       = f;
        f                       = e;
        e                       = d + temp1;
        d                       = c;
        c                       = b;
        b                       = a;
        a                       = temp1 + temp2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(const char* data, std::size_t size) {
    totalBytes_ += size;
    while (size > 0) {
        const std::size_t count = std::min(size, buffer_.size() - buffered_);
        std::memcpy(buffer_.data() + buffered_, data, count);
        buffered_ += count;
        data += count;
        size -= count;
        if (buffered_ == buffer_.size()) {
            transform(buffer_.data());
            buffered_ = 0;
        }
    }
}

std::string Sha256::finish() {
    const uint64_t totalBits = totalBytes_ * 8u;
    buffer_[buffered_++]     = 0x80u;
    if (buffered_ > 56) {
        std::fill(buffer_.begin() + buffered_, buffer_.end(), 0);
        transform(buffer_.data());
        buffered_ = 0;
    }
    std::fill(buffer_.begin() + buffered_, buffer_.begin() + 56, 0);
    for (unsigned i = 0; i < 8; ++i) buffer_[63 - i] = static_cast<uint8_t>(totalBits >> (i * 8u));
    transform(buffer_.data());

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const uint32_t value : state_) out << std::setw(8) << value;
    return out.str();
}

}  // namespace eve::cmd::sdk::detail
