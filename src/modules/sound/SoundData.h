#pragma once

#include "common/Resource.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace eve {
namespace sound {

/** @brief Raw PCM audio buffer with format metadata (samples/rate/bit depth/channels). */
class SoundData : public Resource {
public:
    /** @brief Takes ownership of the PCM bytes. */
    SoundData(std::vector<uint8_t> pcm, int sampleRate, int bitDepth, int channels);
    ~SoundData() override;

    /** @brief Format metadata. */
    int getSampleCount() const;
    int getSampleRate() const;
    int getBitDepth() const;
    int getChannelCount() const;
    /** @brief Duration in seconds. */
    double getDuration() const;
    /** @brief Raw PCM access. */
    void *getData() const;
    size_t getSize() const;

private:
    std::vector<uint8_t> pcm;
    int sampleRate = 0;
    int bitDepth = 0;
    int channels = 0;
};

}  // namespace sound
}  // namespace eve
