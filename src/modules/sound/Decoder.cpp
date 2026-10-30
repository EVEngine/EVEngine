#include "Decoder.h"

#include "common/Exception.h"
#if !defined(__EMSCRIPTEN__)
#include "medialoader/sound/Decoder.h"
#endif

#include <algorithm>
#include <cmath>

namespace eve {
namespace sound {

#if defined(__EMSCRIPTEN__)

Decoder::Decoder(std::shared_ptr<const std::vector<std::uint8_t>> pcm, int sampleRate, int channels, int bufferSize)
    : browserPcm(std::move(pcm)), browserSampleRate(sampleRate), browserChannels(channels) {
    if (!browserPcm || sampleRate <= 0 || (channels != 1 && channels != 2) || bufferSize <= 0)
        throw eve::Exception("Invalid browser sound decoder");
    const auto frameBytes = static_cast<std::size_t>(channels) * 2;
    browserChunkBytes     = std::max(frameBytes, static_cast<std::size_t>(bufferSize) / frameBytes * frameBytes);
    browserBuffer.reserve(browserChunkBytes);
}

Decoder::~Decoder() = default;
Decoder* Decoder::clone() const {
    return new Decoder(browserPcm, browserSampleRate, browserChannels, static_cast<int>(browserChunkBytes));
}
int Decoder::decode() {
    const auto count = std::min(browserChunkBytes, browserPcm->size() - browserOffset);
    browserBuffer.assign(browserPcm->begin() + browserOffset, browserPcm->begin() + browserOffset + count);
    browserOffset += count;
    return static_cast<int>(count);
}
int   Decoder::getSize() const { return static_cast<int>(browserBuffer.size()); }
void* Decoder::getBuffer() const { return const_cast<std::uint8_t*>(browserBuffer.data()); }
bool  Decoder::seek(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0 || seconds > getDuration()) return false;
    const auto frame = static_cast<std::size_t>(seconds * browserSampleRate);
    browserOffset    = std::min(frame * static_cast<std::size_t>(browserChannels) * 2, browserPcm->size());
    browserBuffer.clear();
    return true;
}
bool Decoder::rewind() {
    browserOffset = 0;
    browserBuffer.clear();
    return true;
}
bool   Decoder::isSeekable() { return true; }
bool   Decoder::isFinished() { return browserOffset >= browserPcm->size(); }
int    Decoder::getChannelCount() const { return browserChannels; }
int    Decoder::getBitDepth() const { return 16; }
int    Decoder::getSampleRate() const { return browserSampleRate; }
double Decoder::getDuration() {
    return static_cast<double>(browserPcm->size()) / (browserSampleRate * browserChannels * 2);
}

#else

Decoder::Decoder(std::unique_ptr<medialoader::Decoder> impl, std::vector<char> ownedData)
    : Decoder(std::move(impl), std::make_shared<const std::vector<char>>(std::move(ownedData))) {}

Decoder::Decoder(std::unique_ptr<medialoader::Decoder> impl, std::shared_ptr<const std::vector<char>> ownedData)
    : ownedData(std::move(ownedData)), impl(std::move(impl)) {
    if (!this->impl)
        throw eve::Exception("Invalid sound decoder");
}

Decoder::~Decoder() = default;

Decoder *Decoder::clone() const {
    auto cloned = std::unique_ptr<medialoader::Decoder>(impl->clone());
    if (!cloned)
        throw eve::Exception("Could not clone sound decoder");
    // Provider clones retain the original pointer rather than rebinding it to
    // a byte-for-byte copy. Keep that exact allocation alive for every clone.
    return new Decoder(std::move(cloned), ownedData);
}

int Decoder::decode() { return impl->decode(); }
int Decoder::getSize() const { return impl->getSize(); }
void *Decoder::getBuffer() const { return impl->getBuffer(); }
bool Decoder::seek(double seconds) { return impl->seek(seconds); }
bool Decoder::rewind() { return impl->rewind(); }
bool Decoder::isSeekable() { return impl->isSeekable(); }
bool Decoder::isFinished() { return impl->isFinished(); }
int Decoder::getChannelCount() const { return impl->getChannelCount(); }
int Decoder::getBitDepth() const { return impl->getBitDepth(); }
int Decoder::getSampleRate() const { return impl->getSampleRate(); }
double Decoder::getDuration() { return impl->getDuration(); }

#endif

}  // namespace sound
}  // namespace eve
