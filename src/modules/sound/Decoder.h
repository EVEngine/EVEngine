#pragma once
#include "common/Export.h"


#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace medialoader {
class Decoder;
}

namespace eve {
namespace sound {

/**
 * @brief Streaming audio decoder (medialoader-backed).
 * Native builds own encoded bytes and decode incrementally via decode(). Browser
 * builds share fully decoded PCM instead. Clones own independent playback positions.
 * Each decoder is thread-affine; calls on one instance must be serialized.
 */
class EVENGINE_API_PLATFORM Decoder {
public:
    /** @brief Wraps a medialoader decoder and the encoded bytes it needs. */
#if !defined(__EMSCRIPTEN__)
    Decoder(std::unique_ptr<medialoader::Decoder> impl, std::vector<char> ownedData);
#endif
    /** @brief Decoder. */
    ~Decoder();

    /**
     * @brief Creates a caller-owned decoder positioned at the start of the stream.
     * @return Independent playback state retaining the encoded storage even after
     * this decoder is destroyed. Either instance may be destroyed first.
     * @throws eve::Exception if the provider cannot clone the decoder.
     * @reentrancy Does not invoke application callbacks.
     */
    Decoder *clone() const;

    /** @brief Decodes the next chunk; returns bytes produced (0 = end/error). */
    int decode();
    /** @brief Bytes decoded so far into the internal buffer. */
    int getSize() const;
    /** @brief Internal decoded buffer. */
    void *getBuffer() const;
    /** @brief Seeks to a time in seconds; false when unsupported. */
    bool seek(double seconds);
    /** @brief Rewinds to the start; false when unsupported. */
    bool rewind();
    /** @brief True when seeking is supported. */
    bool isSeekable();
    /** @brief True when the stream is fully decoded. */
    bool isFinished();
    /** @brief Format metadata. */
    int getChannelCount() const;
    /** @brief Returns the bit depth. */
    int getBitDepth() const;
    /** @brief Returns the sample rate. */
    int getSampleRate() const;
    /** @brief Total duration in seconds. */
    double getDuration();

    /**
     * @brief Underlying medialoader decoder (advanced; null on browser builds).
     * @ownership Borrowed from this Decoder. @lifetime Until this Decoder is destroyed.
     * @nullable Yes on browser builds. @thread This Decoder's owning thread only.
     */
    medialoader::Decoder *getImpl() {
#if defined(__EMSCRIPTEN__)
        return nullptr;
#else
        return impl.get();
#endif
    }

private:
#if defined(__EMSCRIPTEN__)
    friend class Sound;
    /** @brief Browser-decoded PCM backend; shares immutable samples with clones. */
    Decoder(std::shared_ptr<const std::vector<std::uint8_t>> pcm, int sampleRate, int channels, int bufferSize);
#endif
#if !defined(__EMSCRIPTEN__)
    Decoder(std::unique_ptr<medialoader::Decoder> impl, std::shared_ptr<const std::vector<char>> ownedData);
    // Providers borrow these bytes. Declare storage first so it outlives impl's
    // destructor, including when this is the last clone.
    std::shared_ptr<const std::vector<char>> ownedData;
    std::unique_ptr<medialoader::Decoder>    impl;
#endif
#if defined(__EMSCRIPTEN__)
    std::shared_ptr<const std::vector<std::uint8_t>> browserPcm;
    std::vector<std::uint8_t>                        browserBuffer;
    std::size_t                                      browserOffset     = 0;
    int                                              browserSampleRate = 0;
    int                                              browserChannels   = 0;
    std::size_t                                      browserChunkBytes = 0;
#endif
};

}  // namespace sound
}  // namespace eve
