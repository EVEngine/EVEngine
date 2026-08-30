#pragma once

#include "common/Module.h"
#include "common/Data.h"

namespace eve {
namespace sound {

class Decoder;
class SoundData;

/**
 * @brief Sound module: decodes compressed audio and produces SoundData buffers.
 * Script: `sound <- eve.Sound();`
 */
class Sound : public Module {
public:
    Module_REG(Sound);

    Sound();
    ~Sound() override;

    /** @brief Creates a streaming decoder over raw encoded data. */
    Decoder *newDecoder(Data *data, int bufferSize = 16384);
    /** @brief Fully decodes data into a SoundData buffer. */
    SoundData *newSoundData(Data *data);
    /** @brief Fully decodes a Decoder into a SoundData buffer. */
    SoundData *newSoundDataFromDecoder(Decoder *decoder);
    /**
     * @brief Decodes a sound file from a VFS path through the unified resource
     * cache. Repeated loads of one path share a single decoded buffer; a file
     * change refreshes it in place (see ResourceManager).
     * @param path The VFS path of the audio file.
     * @return The cached SoundData.
     * @throws eve::Exception when the file cannot be read or decoded.
     */
    SoundData *newSoundDataFromFile(std::string path);
    /** @brief Creates an empty PCM buffer (e.g. for synthesis). */
    SoundData *newSoundDataEmpty(int samples, int rate, int bitDepth, int channels);
};

}  // namespace sound
}  // namespace eve
