#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace eve {
namespace audio {

constexpr int kStreamBufferCount = 4;
constexpr int kStreamBufferFrames = 4096;

struct PcmChunk {
    std::vector<uint8_t> bytes;
};

}  // namespace audio
}  // namespace eve
