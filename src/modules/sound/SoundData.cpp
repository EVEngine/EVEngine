#include "SoundData.h"

#include "common/Exception.h"

#include <utility>

namespace eve {
namespace sound {

SoundData::SoundData(std::vector<uint8_t> pcm, int sampleRate, int bitDepth, int channels)
    : Resource(""), pcm(std::move(pcm)), sampleRate(sampleRate), bitDepth(bitDepth), channels(channels) {
    if (sampleRate <= 0)
        throw eve::Exception("Invalid sample rate");
    if (!(bitDepth == 8 || bitDepth == 16))
        throw eve::Exception("Invalid bit depth (expected 8 or 16)");
    if (!(channels == 1 || channels == 2))
        throw eve::Exception("Invalid channel count (expected 1 or 2)");
}

SoundData::~SoundData() = default;

void SoundData::adopt(eve::Resource &replacement) {
    auto &other = static_cast<SoundData &>(replacement);
    std::swap(pcm, other.pcm);
    std::swap(sampleRate, other.sampleRate);
    std::swap(bitDepth, other.bitDepth);
    std::swap(channels, other.channels);
}

int SoundData::getSampleCount() const {
    if (bitDepth <= 0 || channels <= 0)
        return 0;
    return static_cast<int>(pcm.size() / (static_cast<size_t>(bitDepth / 8) * channels));
}

int SoundData::getSampleRate() const { return sampleRate; }
int SoundData::getBitDepth() const { return bitDepth; }
int SoundData::getChannelCount() const { return channels; }

double SoundData::getDuration() const {
    if (sampleRate <= 0)
        return 0.0;
    return static_cast<double>(getSampleCount()) / static_cast<double>(sampleRate);
}

void *SoundData::getData() const { return const_cast<uint8_t *>(pcm.data()); }
size_t SoundData::getSize() const { return pcm.size(); }

}  // namespace sound
}  // namespace eve
