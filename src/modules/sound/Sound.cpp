#include "Sound.h"
#include "Decoder.h"
#include "SoundData.h"

#include "common/Exception.h"
#include "common/config.h"
#include "common/Resource.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#else
#include "medialoader/Exception.h"
#include "medialoader/sound/WaveDecoder.h"
#include "medialoader/sound/VorbisDecoder.h"
#include "medialoader/sound/Mpg123Decoder.h"
#include "medialoader/sound/FLACDecoder.h"
#include "medialoader/sound/ModPlugDecoder.h"
#endif

#include <simplesquirrel/simplesquirrel.hpp>

#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <string>

#if defined(_WIN32)
#include <stdlib.h>
#else
#include <cstdlib>
#endif

namespace eve {
namespace sound {

Module_IMPL(Sound, new Sound());

namespace {

#if defined(EVENGINE_MACOSX) || defined(EVENGINE_LINUX) || defined(EVENGINE_WINDOWS)

bool fileExists(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    return in.good();
}

std::string parentPath(std::string path) {
    while (!path.empty() && (path.back() == '/' || path.back() == '\\'))
        path.pop_back();
    auto slash = path.find_last_of("/\\");
    if (slash == std::string::npos)
        return {};
    if (slash == 0)
        return path.substr(0, 1);
    return path.substr(0, slash);
}

std::string joinPath(const std::string &a, const std::string &b) {
    if (a.empty())
        return b;
    if (a.back() == '/' || a.back() == '\\')
        return a + b;
    return a + "/" + b;
}

// Prefer installed / staged share/eve/timidity next to the executable.
// Does not override an existing MMPAT_PATH_TO_CFG (user trim / custom banks).
void ensureDesktopTimidityShare() {
    if (const char *existing = std::getenv("MMPAT_PATH_TO_CFG")) {
        if (existing[0] != '\0')
            return;
    }

    auto *fs = filesystem::Filesystem::create();
    std::string exe = fs->getExecutablePath();
    if (exe.empty())
        return;

    std::string dir = parentPath(exe);
    const char *relatives[] = {
        "share/eve/timidity",
        "../share/eve/timidity",
        "../../share/eve/timidity",
        "../../../share/eve/timidity",
    };

    std::string found;
    for (const char *rel : relatives) {
        std::string cand = joinPath(dir, rel);
        if (fileExists(joinPath(cand, "timidity.cfg"))) {
            found = cand;
            break;
        }
    }
    if (found.empty())
        return;

#if defined(_WIN32)
    _putenv_s("MMPAT_PATH_TO_CFG", found.c_str());
#else
    setenv("MMPAT_PATH_TO_CFG", found.c_str(), 0);
#endif
}

#endif  // desktop

}  // namespace

Sound::Sound() {
#if defined(EVENGINE_MACOSX) || defined(EVENGINE_LINUX) || defined(EVENGINE_WINDOWS)
    ensureDesktopTimidityShare();
#endif
}

Sound::~Sound() = default;

namespace {

#if defined(__EMSCRIPTEN__)
// clang-format off
EM_JS(int, startBrowserDecode, (const void* bytes, int size), {
    try {
        const encoded = HEAPU8.slice(bytes, bytes + size);
        const store = globalThis.eveSoundDecode ||= { next: 1, requests: new Map(), context: null };
        const id = store.next++;
        const request = { status: 0, pcm: null, rate: 0, channels: 0 };
        store.requests.set(id, request);
        const Context = globalThis.AudioContext || globalThis.webkitAudioContext;
        if (!Context) throw new Error('Web Audio is unavailable');
        store.context ||= new Context();
        store.context.decodeAudioData(encoded.buffer).then((buffer) => {
            if (buffer.numberOfChannels < 1 || buffer.numberOfChannels > 2 ||
                buffer.length * buffer.numberOfChannels * 2 > 0x7fffffff) {
                request.status = -1;
                return;
            }
            const channels = buffer.numberOfChannels;
            const pcm = new Uint8Array(buffer.length * channels * 2);
            const view = new DataView(pcm.buffer);
            const samples = Array.from({ length: channels }, (_, c) => buffer.getChannelData(c));
            for (let frame = 0; frame < buffer.length; ++frame) {
                for (let channel = 0; channel < channels; ++channel) {
                    const sample = Math.max(-1, Math.min(1, samples[channel][frame]));
                    view.setInt16((frame * channels + channel) * 2,
                                  sample < 0 ? Math.round(sample * 32768) : Math.round(sample * 32767), true);
                }
            }
            request.pcm = pcm;
            request.rate = buffer.sampleRate;
            request.channels = channels;
            request.status = 1;
        }).catch((error) => { console.error('[Sound] Browser decode failed', error); request.status = -1; });
        return id;
    } catch (error) {
        console.error('[Sound] Browser decode could not start', error);
        return -1;
    }
});
EM_JS(int, browserDecodeStatus, (int id), {
    const request = globalThis.eveSoundDecode?.requests.get(id);
    return request ? request.status : -1;
});
EM_JS(int, browserDecodeSize, (int id), {
    return globalThis.eveSoundDecode.requests.get(id).pcm.length;
});
EM_JS(int, browserDecodeRate, (int id), {
    return globalThis.eveSoundDecode.requests.get(id).rate;
});
EM_JS(int, browserDecodeChannels, (int id), {
    return globalThis.eveSoundDecode.requests.get(id).channels;
});
EM_JS(void, browserDecodeCopyAndRelease, (int id, void* destination), {
    const store = globalThis.eveSoundDecode;
    const request = store.requests.get(id);
    HEAPU8.set(request.pcm, destination);
    store.requests.delete(id);
});
EM_JS(void, browserDecodeRelease, (int id), {
    globalThis.eveSoundDecode?.requests.delete(id);
});
// clang-format on
#else
std::string lowerExt(std::string ext) {
    for (char &c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

template <typename T>
std::unique_ptr<medialoader::Decoder> tryMake(const char *data, size_t size, int bufferSize) {
    try {
        return std::unique_ptr<medialoader::Decoder>(new T(data, size, bufferSize));
    } catch (...) {
        // Every decoder failure has the same fallback; one catch avoids a
        // redundant typed exception handler in each decoder specialization.
        return nullptr;
    }
}
#endif

}  // namespace

Decoder *Sound::newDecoder(Data *data, int bufferSize) {
    if (data == nullptr || data->getData() == nullptr || data->getSize() == 0)
        throw eve::Exception("Cannot decode empty sound data");

#if defined(__EMSCRIPTEN__)
    if (data->getSize() > static_cast<std::size_t>(0x7fffffff) || bufferSize <= 0)
        throw eve::Exception("Browser audio data is too large or buffer size is invalid");
    const int id = startBrowserDecode(data->getData(), static_cast<int>(data->getSize()));
    if (id < 0) throw eve::Exception("Could not start browser audio decode");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    int        status   = browserDecodeStatus(id);
    while (status == 0 && std::chrono::steady_clock::now() < deadline) {
        emscripten_sleep(1);
        status = browserDecodeStatus(id);
    }
    if (status != 1) {
        browserDecodeRelease(id);
        throw eve::Exception("Could not decode browser audio data");
    }
    const int size = browserDecodeSize(id);
    if (size <= 0) {
        browserDecodeRelease(id);
        throw eve::Exception("Browser audio decoded to an empty buffer");
    }
    try {
        std::vector<std::uint8_t> pcm(static_cast<std::size_t>(size));
        const int                 rate     = browserDecodeRate(id);
        const int                 channels = browserDecodeChannels(id);
        browserDecodeCopyAndRelease(id, pcm.data());
        return new Decoder(std::make_shared<const std::vector<std::uint8_t>>(std::move(pcm)), rate, channels,
                           bufferSize);
    } catch (...) {
        browserDecodeRelease(id);
        throw;
    }
#else

    std::vector<char> owned(static_cast<const char *>(data->getData()),
                            static_cast<const char *>(data->getData()) + data->getSize());

    std::string ext;
    if (auto *fd = dynamic_cast<filesystem::FileData *>(data)) ext = lowerExt(fd->getExtension());

    const char *ptr  = owned.data();
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
#endif
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

SoundData *Sound::newSoundDataFromFile(std::string path) {
    if (path.empty())
        throw eve::Exception("Sound::newSoundDataFromFile: empty path");

    eve::Resource *resource = eve::ResourceManager::getInstance().get(path);
    if (!resource)
        throw eve::Exception("Could not load sound file: %s", path.c_str());
    return static_cast<SoundData *>(resource);
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
    cls.addFunc("newSoundDataFromFile", &Sound::newSoundDataFromFile);
    cls.addFunc("newSoundDataEmpty", &Sound::newSoundDataEmpty);
}

}  // namespace sound
}  // namespace eve
