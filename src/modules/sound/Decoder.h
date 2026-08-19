#pragma once

#include "common/Object.h"

#include <memory>
#include <vector>
#include <string>

namespace medialoader {
class Decoder;
}

namespace eve {
namespace sound {

/**
 * @brief Streaming audio decoder (medialoader-backed).
 * Owns the raw encoded bytes and decodes incrementally via decode().
 */
class Decoder : public Object {
public:
    /** @brief Wraps a medialoader decoder and the encoded bytes it needs. */
    Decoder(std::unique_ptr<medialoader::Decoder> impl, std::vector<char> ownedData);
    ~Decoder() override;

    /** @brief Duplicates the decoder (each clone keeps its own position). */
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
    int getBitDepth() const;
    int getSampleRate() const;
    /** @brief Total duration in seconds. */
    double getDuration();

    /** @brief Underlying medialoader decoder (advanced). */
    medialoader::Decoder *getImpl() { return impl.get(); }

private:
    std::unique_ptr<medialoader::Decoder> impl;
    std::vector<char> ownedData;
};

}  // namespace sound
}  // namespace eve
