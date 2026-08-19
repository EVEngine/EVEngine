#pragma once

#include "common/Module.h"
#include "common/Data.h"

namespace eve {
namespace sound {

class Decoder;
class SoundData;

class Sound : public Module {
public:
    Module_REG(Sound);

    Sound();
    ~Sound() override;

    Decoder *newDecoder(Data *data, int bufferSize = 16384);
    SoundData *newSoundData(Data *data);
    SoundData *newSoundDataFromDecoder(Decoder *decoder);
    SoundData *newSoundDataEmpty(int samples, int rate, int bitDepth, int channels);
};

}  // namespace sound
}  // namespace eve
