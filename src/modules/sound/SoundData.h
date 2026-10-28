#pragma once
#include "common/Export.h"


#include "common/Resource.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace eve {
namespace sound {

/** @brief Raw PCM audio buffer with format metadata (samples/rate/bit depth/channels). */
class EVENGINE_API_PLATFORM SoundData : public Resource {
public:
    /** @brief Takes ownership of the PCM bytes. */
    SoundData(std::vector<uint8_t> pcm, int sampleRate, int bitDepth, int channels);
    /** @brief Sound data. */
    ~SoundData() override;

    /** @brief Replace this buffer with `replacement`'s (cache reload). */
    void adopt(eve::Resource &replacement) override;

    /** @brief Format metadata. */
    int getSampleCount() const;
    /** @brief Returns the sample rate. */
    int getSampleRate() const;
    /** @brief Returns the bit depth. */
    int getBitDepth() const;
    /** @brief Returns the channel count. */
    int getChannelCount() const;
    /** @brief Duration in seconds. */
    double getDuration() const;
    /** @brief Raw PCM access. */
    void *getData() const;
    /** @brief Returns the size. */
    size_t getSize() const;

private:
    std::vector<uint8_t> pcm;
    int sampleRate = 0;
    int bitDepth = 0;
    int channels = 0;
};

}  // namespace sound
}  // namespace eve
