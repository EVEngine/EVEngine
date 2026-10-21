#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_gpgpu)
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#include <vector>
#include "gpgpu/GpuBuffer.h"
// gpgpu.cpp
namespace test_gpgpu_local_boundary {
class TestBuffer : public eve::gpgpu::GpuBuffer {
public:
    std::vector<float> floats;

    explicit TestBuffer(int bytes) : floats(size_t(bytes) / sizeof(float), 0.f) {}

    int                  getSize() const override { return int(floats.size()) * int(sizeof(float)); }
    std::string          getUsage() const override { return "test"; }
    void                 writeData(eve::data::ByteData *, int) override {}
    eve::data::ByteData *readData(int, int) override { return nullptr; }
    void                 writeFloat32(int i, float v) override { floats[size_t(i)] = v; }
    float                readFloat32(int i) override { return floats[size_t(i)]; }
    void                 fillFloat32(float v) override { std::fill(floats.begin(), floats.end(), v); }
    void                 writeFloat32s(const float *data, int count, int startIndex) override {
        for (int i = 0; i < count; ++i) floats[size_t(startIndex + i)] = data[i];
    }
    void readFloat32s(float *out, int count, int startIndex) const override {
        for (int i = 0; i < count; ++i) out[i] = floats[size_t(startIndex + i)];
    }
    void uploadBytes(const void *, uint64_t, uint64_t) override {}
    void downloadBytes(void *, uint64_t, uint64_t) const override {}
};
TEST_CASE("gpgpu.buffer.readFloat32sCopyClipsToCapacity") {
    TestBuffer buffer(4 * int(sizeof(float)));
    for (int i = 0; i < 4; ++i) buffer.writeFloat32(i, float(i + 1));

    const std::vector<float> firstTwo = buffer.readFloat32sCopy(2, 0);
    REQUIRE_EQ(firstTwo.size(), std::size_t{2});
    CHECK_EQ(firstTwo[0], 1.f);
    CHECK_EQ(firstTwo[1], 2.f);

    // 起始下标之后只剩一个：只返回剩下的那一个，不越界。
    const std::vector<float> tail = buffer.readFloat32sCopy(4, 3);
    REQUIRE_EQ(tail.size(), std::size_t{1});
    CHECK_EQ(tail[0], 4.f);

    // 多要、起点越界、非正个数、负起点：都是空数组，而不是异常或越界读。
    CHECK(buffer.readFloat32sCopy(0).empty());
    CHECK(buffer.readFloat32sCopy(4, 4).empty());
    CHECK(buffer.readFloat32sCopy(4, 99).empty());
    CHECK(buffer.readFloat32sCopy(-1).empty());
    CHECK(buffer.readFloat32sCopy(4, -1).empty());

    // 读整段就是整段。
    REQUIRE_EQ(buffer.readFloat32sCopy(4, 0).size(), std::size_t{4});
}
}  // namespace test_gpgpu_local_boundary
#endif
