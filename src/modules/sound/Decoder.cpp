#include "Decoder.h"

#include "common/Exception.h"
#include "medialoader/sound/Decoder.h"

namespace eve {
namespace sound {

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

}  // namespace sound
}  // namespace eve
