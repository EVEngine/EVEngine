// Exercise the real tryMake body against controlled failures, using the real
// medialoader Decoder/Exception implementations for allocation and unwinding.
#include <medialoader/sound/Decoder.h>
#include <medialoader/Exception.h>
#include <cassert>
#include <memory>
#include <new>
#include <stdexcept>

#include "decoder_fallback.inc"

class ControlledDecoder : public medialoader::Decoder {
public:
    inline static int allocations = 0;
    inline static bool failAllocation = false;
    static void* operator new(size_t size) {
        if (failAllocation) throw std::bad_alloc();
        void* p = ::operator new(size);
        ++allocations;
        return p;
    }
    static void operator delete(void* p) noexcept { --allocations; ::operator delete(p); }
    ControlledDecoder(const char* data, size_t size, int bufferSize)
        : medialoader::Decoder(data, size, bufferSize) {
        switch (*data) {
            case 'l': throw medialoader::Exception("decode failed");
            case 's': throw std::runtime_error("other failure");
            case 'i': throw 42;
        }
    }
    medialoader::Decoder* clone() override { return nullptr; }
    int decode() override { return 0; }
    bool seek(double) override { return false; }
    bool rewind() override { return false; }
    bool isSeekable() override { return false; }
    int getChannelCount() const override { return 1; }
    int getBitDepth() const override { return 16; }
    double getDuration() override { return 0; }
};

int main() {
    for (const char mode : {'l', 's', 'i'}) {
        assert(!tryMake<ControlledDecoder>(&mode, 1, 16));
        assert(ControlledDecoder::allocations == 0);
    }
    const char mode = 'o';
    {
        auto decoder = tryMake<ControlledDecoder>(&mode, 1, 16);
        assert(decoder && ControlledDecoder::allocations == 1);
    }
    assert(ControlledDecoder::allocations == 0);
    ControlledDecoder::failAllocation = true;
    assert(!tryMake<ControlledDecoder>(&mode, 1, 16));
    assert(ControlledDecoder::allocations == 0);
}
