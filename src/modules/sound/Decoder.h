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

class Decoder : public Object {
public:
    Decoder(std::unique_ptr<medialoader::Decoder> impl, std::vector<char> ownedData);
    ~Decoder() override;

    Decoder *clone() const;

    int decode();
    int getSize() const;
    void *getBuffer() const;
    bool seek(double seconds);
    bool rewind();
    bool isSeekable();
    bool isFinished();
    int getChannelCount() const;
    int getBitDepth() const;
    int getSampleRate() const;
    double getDuration();

    medialoader::Decoder *getImpl() { return impl.get(); }

private:
    std::unique_ptr<medialoader::Decoder> impl;
    std::vector<char> ownedData;
};

}  // namespace sound
}  // namespace eve
