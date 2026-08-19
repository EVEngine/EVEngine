#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace eve {
namespace audio {

/** @brief 流式 Source 的 OpenAL 缓冲数量。 */
constexpr int kStreamBufferCount = 4;
/** @brief 每个流式缓冲的采样帧数。 */
constexpr int kStreamBufferFrames = 4096;

/** @brief 解码后待排队的一帧 PCM 数据。 */
struct PcmChunk {
    std::vector<uint8_t> bytes;
};

}  // namespace audio
}  // namespace eve
