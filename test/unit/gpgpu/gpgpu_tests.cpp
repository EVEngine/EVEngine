#include "gpgpu/GpuBuffer.h"
#include "zeroerr/unittest.h"

#include <array>
#include <cstddef>
#include <vector>

namespace test_gpgpu_local_boundary {
namespace {

// A CPU-only backend records the transfer contract that the owning wrapper
// must honor. It deliberately implements no clipping of its own.
class RecordingBuffer final : public eve::gpgpu::GpuBuffer {
public:
    explicit RecordingBuffer(int byteLength) : byteLength_(byteLength), values_(std::size_t(byteLength / 4)) {}

    int                  getSize() const override { return byteLength_; }
    std::string          getUsage() const override { return "test"; }
    void                 writeData(eve::data::ByteData*, int) override {}
    eve::data::ByteData* readData(int, int) override { return nullptr; }
    void                 writeFloat32(int index, float value) override { values_[std::size_t(index)] = value; }
    float                readFloat32(int index) override { return values_[std::size_t(index)]; }
    void                 fillFloat32(float value) override {
        for (auto& element : values_) element = value;
    }
    void writeFloat32s(const float* data, int count, int startIndex) override {
        for (int index = 0; index < count; ++index) values_[std::size_t(startIndex + index)] = data[index];
    }
    void readFloat32s(float* out, int count, int startIndex) const override {
        ++readCalls;
        lastCount = count;
        lastStart = startIndex;
        for (int index = 0; index < count; ++index) out[index] = values_[std::size_t(startIndex + index)];
    }
    void uploadBytes(const void*, uint64_t, uint64_t) override {}
    void downloadBytes(void*, uint64_t, uint64_t) const override {}

    mutable int readCalls = 0;
    mutable int lastCount = -1;
    mutable int lastStart = -1;

private:
    int                byteLength_;
    std::vector<float> values_;
};

void seedFourFloats(RecordingBuffer& buffer) {
    const std::array<float, 4> input{1.f, 2.f, 3.f, 4.f};
    buffer.writeFloat32s(input.data(), int(input.size()), 0);
}

}  // namespace

// Input: requested ranges in float indices. Expected: only the intersection
// with [0, capacity) is read, and invalid/empty ranges avoid the backend.
TEST_CASE("gpgpu.buffer.copyRangeContract") {
    struct Example {
        int                count;
        int                start;
        std::vector<float> expected;
    };
    const std::array<Example, 9> examples{{
        {4, 0, {1.f, 2.f, 3.f, 4.f}},
        {2, 1, {2.f, 3.f}},
        {4, 3, {4.f}},
        {1, 4, {}},
        {1, 99, {}},
        {4, -1, {}},
        {0, 0, {}},
        {-1, 0, {}},
        {100, 0, {1.f, 2.f, 3.f, 4.f}},
    }};
    RecordingBuffer              buffer(4 * int(sizeof(float)));
    seedFourFloats(buffer);
    int expectedReads = 0;
    for (const auto& example : examples) {
        const auto actual = buffer.readFloat32sCopy(example.count, example.start);
        REQUIRE_EQ(actual.size(), example.expected.size());
        for (std::size_t index = 0; index < actual.size(); ++index) CHECK_EQ(actual[index], example.expected[index]);
        if (!example.expected.empty()) ++expectedReads;
        CHECK_EQ(buffer.readCalls, expectedReads);
    }
}

// Input: a buffer with 18 bytes. Expected: only four complete float slots are
// visible, and the backend receives exactly the clipped count and float offset.
TEST_CASE("gpgpu.buffer.partialTrailingBytesAreNotFloats") {
    RecordingBuffer            buffer(18);
    const std::array<float, 4> values{10.f, 20.f, 30.f, 40.f};
    buffer.writeFloat32s(values.data(), int(values.size()), 0);

    const auto result = buffer.readFloat32sCopy(10, 2);
    REQUIRE_EQ(result.size(), std::size_t{2});
    CHECK_EQ(result[0], 30.f);
    CHECK_EQ(result[1], 40.f);
    CHECK_EQ(buffer.readCalls, 1);
    CHECK_EQ(buffer.lastCount, 2);
    CHECK_EQ(buffer.lastStart, 2);
}

// Input: one copy followed by a write to the source buffer. Expected: the
// returned vector retains its original value because it owns the readback.
TEST_CASE("gpgpu.buffer.readbackOwnsItsValues") {
    RecordingBuffer buffer(4 * int(sizeof(float)));
    seedFourFloats(buffer);
    const auto snapshot = buffer.readFloat32sCopy(2, 1);
    buffer.writeFloat32(1, 99.f);

    REQUIRE_EQ(snapshot.size(), std::size_t{2});
    CHECK_EQ(snapshot[0], 2.f);
    CHECK_EQ(snapshot[1], 3.f);
    CHECK_EQ(buffer.readFloat32(1), 99.f);
}

}  // namespace test_gpgpu_local_boundary
