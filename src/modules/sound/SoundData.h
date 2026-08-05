#pragma once

#include "common/Resource.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace eve {
namespace sound {

class SoundData : public Resource {
public:
    SoundData(std::vector<uint8_t> pcm, int sampleRate, int bitDepth, int channels);
    ~SoundData() override;

    int getSampleCount() const;
    int getSampleRate() const;
    int getBitDepth() const;
    int getChannelCount() const;
    double getDuration() const;
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
