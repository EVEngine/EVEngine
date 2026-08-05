#include "Sound.h"
#include "Decoder.h"
#include "SoundData.h"

#include "common/Exception.h"
#include "filesystem/FileData.h"

#include "medialoader/Exception.h"
#include "medialoader/sound/WaveDecoder.h"
#include "medialoader/sound/VorbisDecoder.h"
#include "medialoader/sound/Mpg123Decoder.h"
#include "medialoader/sound/FLACDecoder.h"
#include "medialoader/sound/ModPlugDecoder.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cctype>
#include <cstring>

namespace eve {
namespace sound {

Module_IMPL(Sound, new Sound());

Sound::Sound() = default;
Sound::~Sound() = default;

namespace {

std::string lowerExt(std::string ext) {
    for (char &c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

template <typename T>
std::unique_ptr<medialoader::Decoder> tryMake(const char *data, size_t size, int bufferSize) {
    try {
        return std::unique_ptr<medialoader::Decoder>(new T(data, size, bufferSize));
    } catch (const medialoader::Exception &) {
        return nullptr;
    } catch (...) {
        return nullptr;
    }
}

}  // namespace

Decoder *Sound::newDecoder(Data *data, int bufferSize) {
    if (data == nullptr || data->getData() == nullptr || data->getSize() == 0)
        throw eve::Exception("Cannot decode empty sound data");

    std::vector<char> owned(static_cast<const char *>(data->getData()),
                            static_cast<const char *>(data->getData()) + data->getSize());

    std::string ext;
    if (auto *fd = dynamic_cast<filesystem::FileData *>(data))
        ext = lowerExt(fd->getExtension());

    const char *ptr = owned.data();
    size_t size = owned.size();

    auto tryByExt = [&](const std::string &e) -> std::unique_ptr<medialoader::Decoder> {
        if (e.empty())
            return nullptr;
        if (medialoader::WaveDecoder::accepts(e))
            return tryMake<medialoader::WaveDecoder>(ptr, size, bufferSize);
        if (medialoader::VorbisDecoder::accepts(e))
            return tryMake<medialoader::VorbisDecoder>(ptr, size, bufferSize);
        if (medialoader::Mpg123Decoder::accepts(e))
            return tryMake<medialoader::Mpg123Decoder>(ptr, size, bufferSize);
        if (medialoader::FLACDecoder::accepts(e))
            return tryMake<medialoader::FLACDecoder>(ptr, size, bufferSize);
        if (medialoader::ModPlugDecoder::accepts(e))
            return tryMake<medialoader::ModPlugDecoder>(ptr, size, bufferSize);
        return nullptr;
    };

    auto impl = tryByExt(ext);
    if (!impl) {
        if (!impl) impl = tryMake<medialoader::WaveDecoder>(ptr, size, bufferSize);
        if (!impl) impl = tryMake<medialoader::VorbisDecoder>(ptr, size, bufferSize);
        if (!impl) impl = tryMake<medialoader::Mpg123Decoder>(ptr, size, bufferSize);
        if (!impl) impl = tryMake<medialoader::FLACDecoder>(ptr, size, bufferSize);
        if (!impl) impl = tryMake<medialoader::ModPlugDecoder>(ptr, size, bufferSize);
    }

    if (!impl)
        throw eve::Exception("Could not decode sound data: unsupported format");

    return new Decoder(std::move(impl), std::move(owned));
}

SoundData *Sound::newSoundDataFromDecoder(Decoder *decoder) {
    if (decoder == nullptr)
        throw eve::Exception("Decoder is null");

    std::vector<uint8_t> pcm;
    while (!decoder->isFinished()) {
        int n = decoder->decode();
        if (n <= 0)
            break;
        auto *buf = static_cast<const uint8_t *>(decoder->getBuffer());
        pcm.insert(pcm.end(), buf, buf + n);
    }

    return new SoundData(std::move(pcm), decoder->getSampleRate(), decoder->getBitDepth(),
                         decoder->getChannelCount());
}

SoundData *Sound::newSoundData(Data *data) {
    Decoder *dec = newDecoder(data);
    try {
        SoundData *sd = newSoundDataFromDecoder(dec);
        delete dec;
        return sd;
    } catch (...) {
        delete dec;
        throw;
    }
}

SoundData *Sound::newSoundDataEmpty(int samples, int rate, int bitDepth, int channels) {
    if (samples < 0)
        throw eve::Exception("Invalid sample count");
    size_t bytes = static_cast<size_t>(samples) * static_cast<size_t>(bitDepth / 8) * static_cast<size_t>(channels);
    std::vector<uint8_t> pcm(bytes, 0);
    return new SoundData(std::move(pcm), rate, bitDepth, channels);
}

void Sound::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Sound::create, false);
    expose(cls);

    auto dec = table.addClass<Decoder>(
        "Decoder", std::function<Decoder *()>([]() -> Decoder * { return nullptr; }), true);
    dec.addFunc("decode", &Decoder::decode);
    dec.addFunc("getSize", &Decoder::getSize);
    dec.addFunc("seek", &Decoder::seek);
    dec.addFunc("rewind", &Decoder::rewind);
    dec.addFunc("isSeekable", &Decoder::isSeekable);
    dec.addFunc("isFinished", &Decoder::isFinished);
    dec.addFunc("getChannelCount", &Decoder::getChannelCount);
    dec.addFunc("getBitDepth", &Decoder::getBitDepth);
    dec.addFunc("getSampleRate", &Decoder::getSampleRate);
    dec.addFunc("getDuration", &Decoder::getDuration);

    auto sd = table.addClass<SoundData>(
        "SoundData", std::function<SoundData *()>([]() -> SoundData * { return nullptr; }), true);
    sd.addFunc("getSampleCount", &SoundData::getSampleCount);
    sd.addFunc("getSampleRate", &SoundData::getSampleRate);
    sd.addFunc("getBitDepth", &SoundData::getBitDepth);
    sd.addFunc("getChannelCount", &SoundData::getChannelCount);
    sd.addFunc("getDuration", &SoundData::getDuration);
    sd.addFunc("getSize", &SoundData::getSize);
}

void Sound::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Sound::getName);
    cls.addFunc("newDecoder", &Sound::newDecoder);
    cls.addFunc("newSoundData", &Sound::newSoundData);
    cls.addFunc("newSoundDataFromDecoder", &Sound::newSoundDataFromDecoder);
    cls.addFunc("newSoundDataEmpty", &Sound::newSoundDataEmpty);
}

}  // namespace sound
}  // namespace eve
