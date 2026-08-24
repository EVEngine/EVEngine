#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/ECS.h"
#include "common/Exception.h"
#include "ScriptTest.h"
#include "gpgpu/ComputeShader.h"
#include "gpgpu/EcsGpu.h"
#include "gpgpu/EcsScriptPack.h"
#include "gpgpu/Gpgpu.h"
#include "gpgpu/GpuBuffer.h"
#include "gpgpu/Sequence.h"
#include "gpgpu/ShaderSystem.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "window/Window.h"

#include <cmath>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

using namespace eve::gpgpu;

namespace {

/** Compute tests only need a live Vulkan device; headless init is enough. */
bool tryInitHeadlessGfx() {
    auto *gfx = eve::graphics::Graphics::create();
    if (!gfx) return false;
    gfx->initHeadless(320, 240);
    return true;
}

const char *kScaleKernel = R"(#version 450
layout(local_size_x = 64) in;
layout(set = 0, binding = 0) buffer Data {
    float values[];
} data;
layout(push_constant) uniform PC {
    float data[32];
} pc;
void main() {
    uint i = gl_GlobalInvocationID.x;
    data.values[i] *= pc.data[0];
}
)";

const char *kMoveKernel = R"(#version 450
layout(local_size_x = 64) in;
layout(set = 0, binding = 0) buffer Pos {
    float data[];
} pos;
layout(set = 0, binding = 1) buffer Vel {
    float data[];
} vel;
layout(push_constant) uniform PC {
    float data[32];
} pc;
void main() {
    uint i = gl_GlobalInvocationID.x;
    uint n = uint(pc.data[1]);
    if (i >= n) return;
    float dt = pc.data[0];
    uint b = i * 2u;
    pos.data[b + 0u] += vel.data[b + 0u] * dt;
    pos.data[b + 1u] += vel.data[b + 1u] * dt;
}
)";

const char *kDoubleKernel = R"(#version 450
layout(local_size_x = 64) in;
layout(set = 0, binding = 0) buffer In {
    float a[];
} inBuf;
layout(set = 0, binding = 1) buffer Out {
    float b[];
} outBuf;
void main() {
    uint i = gl_GlobalInvocationID.x;
    outBuf.b[i] = inBuf.a[i] * 2.0;
}
)";

class GpuNode : public ecs::Entity {
public:
    ENTITY(GpuNode, ecs::Entity)
    void release() override { ecs::DestroyEntity(this); }

    struct Position {
        float x = 0;
        float y = 0;
    };
    COMPONENT(Position, position)

    struct Velocity {
        float dx = 0;
        float dy = 0;
    };
    COMPONENT(Velocity, velocity)
};

}  // namespace

TEST_CASE("gpgpu.module.create") {
    auto *mod = Gpgpu::create();
    REQUIRE(mod != nullptr);
    CHECK_EQ(mod->getName(), std::string("Gpgpu"));
}

TEST_CASE("gpgpu.graphics.backendName") {
    if (!tryInitHeadlessGfx()) return;
    auto *gfx = eve::graphics::Graphics::create();
    REQUIRE(gfx != nullptr);
    CHECK_EQ(gfx->getBackendName(), std::string("vulkan"));
}

TEST_CASE("gpgpu.newShaderFromSpvFile.delegatesWhenMissing") {
    if (!tryInitHeadlessGfx()) return;
    auto *mod = Gpgpu::create();
    if (!mod->isAvailable()) return;
    bool threw = false;
    try {
        mod->newShaderFromSpvFile("__eve_missing_compute.spv");
    } catch (...) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("gpgpu.dispatch.scaleFloats") {
    if (!tryInitHeadlessGfx()) return;
    auto *mod = Gpgpu::create();
    REQUIRE(mod->isAvailable());

    const int count = 128;
    GpuBuffer *buf = mod->newBuffer(count * int(sizeof(float)), "storage");
    REQUIRE(buf != nullptr);
    CHECK_EQ(buf->getSize(), count * int(sizeof(float)));

    for (int i = 0; i < count; ++i)
        buf->writeFloat32(i, float(i + 1));

    ComputeShader *shader = nullptr;
    try {
        shader = mod->newShader(kScaleKernel);
    } catch (...) {
        // glslc may be missing in some CI images — skip GPU path.
        return;
    }
    REQUIRE(shader != nullptr);
    shader->bindBuffer(0, buf);
    shader->setFloat(0, 2.f);

    const int groups = (count + 63) / 64;
    mod->dispatch(shader, groups, 1, 1);

    CHECK(std::fabs(buf->readFloat32(0) - 2.f) < 1e-4f);
    CHECK(std::fabs(buf->readFloat32(1) - 4.f) < 1e-4f);
    CHECK(std::fabs(buf->readFloat32(count - 1) - float(count) * 2.f) < 1e-3f);

    delete shader;
    delete buf;
}

TEST_CASE("gpgpu.sequence.dispatchScale") {
    if (!tryInitHeadlessGfx()) return;
    auto *mod = Gpgpu::create();
    REQUIRE(mod->isAvailable());

    const int count = 128;
    GpuBuffer *in = mod->newBuffer(count * int(sizeof(float)), "storage");
    GpuBuffer *out = mod->newBuffer(count * int(sizeof(float)), "storage");
    GpuBuffer *staging = mod->newBuffer(count * int(sizeof(float)), "staging");
    REQUIRE(in != nullptr);
    REQUIRE(out != nullptr);
    REQUIRE(staging != nullptr);

    ComputeShader *shader = nullptr;
    try {
        shader = mod->newShader(kDoubleKernel);
    } catch (...) {
        delete in;
        delete out;
        delete staging;
        return;  // no compiler available — skip GPU path
    }
    REQUIRE(shader != nullptr);

    std::vector<float> src;
    src.reserve(count);
    for (int i = 0; i < count; ++i) src.push_back(float(i + 1));

    Sequence *seq = mod->newSequence();
    REQUIRE(seq->isAvailable());
    shader->bindBuffer(0, in);
    shader->bindBuffer(1, out);

    const int groups = (count + 63) / 64;
    seq->begin();
    seq->recordUpload(in, src.data(), uint64_t(count) * sizeof(float));
    seq->recordDispatch(shader, groups, 1, 1);
    seq->recordDownload(out, staging, uint64_t(count) * sizeof(float));
    seq->submit();

    std::vector<float> dst(size_t(count), 0.f);
    staging->downloadBytes(dst.data(), uint64_t(count) * sizeof(float));
    CHECK(std::fabs(dst[0] - 2.f) < 1e-4f);
    CHECK(std::fabs(dst[1] - 4.f) < 1e-4f);
    CHECK(std::fabs(dst[count - 1] - float(count) * 2.f) < 1e-3f);

    delete seq;
    delete shader;
    delete in;
    delete out;
    delete staging;
}

TEST_CASE("gpgpu.sequence.singleSubmitChainedDispatches") {
    if (!tryInitHeadlessGfx()) return;
    auto *mod = Gpgpu::create();
    REQUIRE(mod->isAvailable());

    const int count = 256;
    GpuBuffer *a = mod->newBuffer(count * int(sizeof(float)), "storage");
    GpuBuffer *b = mod->newBuffer(count * int(sizeof(float)), "storage");
    GpuBuffer *c = mod->newBuffer(count * int(sizeof(float)), "storage");
    GpuBuffer *staging = mod->newBuffer(count * int(sizeof(float)), "staging");

    ComputeShader *shader = nullptr;
    try {
        shader = mod->newShader(kDoubleKernel);
    } catch (...) {
        delete a;
        delete b;
        delete c;
        delete staging;
        return;
    }

    std::vector<float> src(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) src[size_t(i)] = float(i % 7);

    Sequence *seq = mod->newSequence();
    REQUIRE(seq->isAvailable());
    const int groups = (count + 63) / 64;

    // a -> b -> c in one submission; c[i] must be src[i] * 4.
    seq->begin();
    seq->recordUpload(a, src.data(), uint64_t(count) * sizeof(float));
    shader->bindBuffer(0, a);
    shader->bindBuffer(1, b);
    seq->recordDispatch(shader, groups, 1, 1);
    shader->bindBuffer(0, b);
    shader->bindBuffer(1, c);
    seq->recordDispatch(shader, groups, 1, 1);
    seq->recordDownload(c, staging, uint64_t(count) * sizeof(float));
    seq->submit();

    std::vector<float> dst(size_t(count), 0.f);
    staging->downloadBytes(dst.data(), uint64_t(count) * sizeof(float));
    for (int i = 0; i < count; ++i)
        CHECK(std::fabs(dst[size_t(i)] - float(i % 7) * 4.f) < 1e-3f);

    // Reuse the same sequence for a second cycle.
    seq->begin();
    seq->recordUpload(a, src.data(), uint64_t(count) * sizeof(float));
    shader->bindBuffer(0, a);
    shader->bindBuffer(1, b);
    seq->recordDispatch(shader, groups, 1, 1);
    seq->recordDownload(b, staging, uint64_t(count) * sizeof(float));
    seq->submit();

    staging->downloadBytes(dst.data(), uint64_t(count) * sizeof(float));
    CHECK(std::fabs(dst[0] - 0.f) < 1e-4f);
    CHECK(std::fabs(dst[count - 1] - float((count - 1) % 7) * 2.f) < 1e-3f);

    delete seq;
    delete shader;
    delete a;
    delete b;
    delete c;
    delete staging;
}

TEST_CASE("gpgpu.shaderSystem.ecsMove") {
    if (!tryInitHeadlessGfx()) return;
    auto *mod = Gpgpu::create();
    REQUIRE(mod->isAvailable());

    ecs::Table world;
    ecs::ScopedTable guard(world);

    auto *a = GpuNode::create();
    a->position()->x = 1.f;
    a->position()->y = 2.f;
    a->velocity()->dx = 10.f;
    a->velocity()->dy = 20.f;

    auto *b = GpuNode::create();
    b->position()->x = 0.f;
    b->position()->y = 0.f;
    b->velocity()->dx = -5.f;
    b->velocity()->dy = 1.f;

    ShaderSystem sys;
    sys.setGpgpu(mod);
    try {
        sys.setShaderSource(kMoveKernel);
    } catch (...) {
        return;
    }

    const int nPos = packViewComponent<GpuNode, GpuNode::Position>(sys, 0);
    const int nVel = packViewComponent<GpuNode, GpuNode::Velocity>(sys, 1);
    REQUIRE_EQ(nPos, nVel);
    REQUIRE_GE(nPos, 2);

    sys.dispatch(nPos, 0.5f);

    unpackViewComponent<GpuNode, GpuNode::Position>(sys, 0, nPos);

    CHECK(std::fabs(a->position()->x - 6.f) < 1e-3f);
    CHECK(std::fabs(a->position()->y - 12.f) < 1e-3f);
    CHECK(std::fabs(b->position()->x - (-2.5f)) < 1e-3f);
    CHECK(std::fabs(b->position()->y - 0.5f) < 1e-3f);
}

TEST_CASE("gpgpu.buffer.bulkFloats") {
    if (!tryInitHeadlessGfx()) return;
    auto *mod = Gpgpu::create();
    REQUIRE(mod->isAvailable());

    std::vector<float> src = {1.f, 2.f, 3.f, 4.f};
    GpuBuffer *buf = mod->newBuffer(int(src.size() * sizeof(float)), "storage");
    buf->writeFloat32s(src.data(), int(src.size()), 0);
    std::vector<float> dst(src.size(), 0.f);
    buf->readFloat32s(dst.data(), int(dst.size()), 0);
    CHECK_EQ(dst[0], 1.f);
    CHECK_EQ(dst[3], 4.f);
    delete buf;
}

namespace {

// CPU-only GpuBuffer so pack/unpackScriptEntityFloats can be exercised without
// a live Vulkan device.
class TestBuffer : public eve::gpgpu::GpuBuffer {
public:
    std::vector<float> floats;

    explicit TestBuffer(int bytes) : floats(size_t(bytes) / sizeof(float), 0.f) {}

    int getSize() const override { return int(floats.size()) * int(sizeof(float)); }
    std::string getUsage() const override { return "test"; }
    void writeData(eve::data::ByteData *, int) override {}
    eve::data::ByteData *readData(int, int) override { return nullptr; }
    void writeFloat32(int i, float v) override { floats[size_t(i)] = v; }
    float readFloat32(int i) override { return floats[size_t(i)]; }
    void fillFloat32(float v) override { std::fill(floats.begin(), floats.end(), v); }
    void writeFloat32s(const float *data, int count, int startIndex) override {
        for (int i = 0; i < count; ++i) floats[size_t(startIndex + i)] = data[i];
    }
    void readFloat32s(float *out, int count, int startIndex) const override {
        for (int i = 0; i < count; ++i) out[i] = floats[size_t(startIndex + i)];
    }
    void uploadBytes(const void *, uint64_t, uint64_t) override {}
    void downloadBytes(void *, uint64_t, uint64_t) const override {}
};

bool throwsException(const std::function<void()> &fn) {
    try {
        fn();
    } catch (const eve::Exception &) {
        return true;
    }
    return false;
}

float readArrayFloat(ssq::VM &vm, ssq::Object arr, int idx) {
    HSQUIRRELVM h = arr.getHandle();
    const SQInteger top = sq_gettop(h);
    sq_pushobject(h, arr.getRaw());
    sq_pushinteger(h, idx);
    SQFloat v = 0.f;
    if (SQ_SUCCEEDED(sq_get(h, -2))) sq_getfloat(h, -1, &v);
    sq_settop(h, top);
    return float(v);
}

}  // namespace

UnitSciptTest(EcsPackFixture, R"SQ(
class Vec { x = null; y = null }
class Entity { Vec = null }
function makeEntities() {
    local a = Entity(); a.Vec = Vec(); a.Vec.x = 1.0; a.Vec.y = 2.0
    local b = Entity(); b.Vec = Vec(); b.Vec.x = 3.0; b.Vec.y = 4.0
    return [a, b]
}
function getEntities() { return makeEntities() }
function getFields() { return ["x", "y"] }
function getNoComp() { return [Entity()] }
function getNotInstances() { return [1, 2] }
function getScalar() { return 42 }
function readBack(ents) { return [ents[0].Vec.x, ents[0].Vec.y, ents[1].Vec.x, ents[1].Vec.y] }
)SQ");

TEST_CASE_FIXTURE(EcsPackFixture, "gpgpu.ecsPack.roundTrip") {
    ssq::Object ents = vm.callFunc(vm.findFunc("getEntities"), vm);
    ssq::Object fields = vm.callFunc(vm.findFunc("getFields"), vm);
    TestBuffer buf(4 * int(sizeof(float)));

    CHECK_EQ(packScriptEntityFloats(ents, "Vec", fields, &buf), 2);
    CHECK(std::fabs(buf.floats[0] - 1.f) < 1e-5f);
    CHECK(std::fabs(buf.floats[1] - 2.f) < 1e-5f);
    CHECK(std::fabs(buf.floats[2] - 3.f) < 1e-5f);
    CHECK(std::fabs(buf.floats[3] - 4.f) < 1e-5f);

    // Overwrite the buffer and unpack back into the instances.
    buf.floats = {10.f, 20.f, 30.f, 40.f};
    CHECK_EQ(unpackScriptEntityFloats(ents, "Vec", fields, &buf, 2), 2);
    ssq::Object back = vm.callFunc(vm.findFunc("readBack"), vm, ents);
    CHECK(std::fabs(readArrayFloat(vm, back, 0) - 10.f) < 1e-5f);
    CHECK(std::fabs(readArrayFloat(vm, back, 1) - 20.f) < 1e-5f);
    CHECK(std::fabs(readArrayFloat(vm, back, 2) - 30.f) < 1e-5f);
    CHECK(std::fabs(readArrayFloat(vm, back, 3) - 40.f) < 1e-5f);

    // entityCount caps the unpack; non-array / empty inputs are no-ops.
    CHECK_EQ(unpackScriptEntityFloats(ents, "Vec", fields, &buf, 0), 0);
    CHECK_EQ(unpackScriptEntityFloats(ents, "Vec", fields, &buf, 99), 2);
}

TEST_CASE_FIXTURE(EcsPackFixture, "gpgpu.ecsPack.errors") {
    ssq::Object ents = vm.callFunc(vm.findFunc("getEntities"), vm);
    ssq::Object fields = vm.callFunc(vm.findFunc("getFields"), vm);
    TestBuffer buf(4 * int(sizeof(float)));

    CHECK(throwsException([&] { packScriptEntityFloats(ents, "", fields, &buf); }));
    CHECK(throwsException([&] { packScriptEntityFloats(ents, "Vec", fields, nullptr); }));

    ssq::Object noComp = vm.callFunc(vm.findFunc("getNoComp"), vm);
    CHECK(throwsException([&] { packScriptEntityFloats(noComp, "Missing", fields, &buf); }));
    ssq::Object notInst = vm.callFunc(vm.findFunc("getNotInstances"), vm);
    CHECK(throwsException([&] { packScriptEntityFloats(notInst, "Vec", fields, &buf); }));

    TestBuffer small(2 * int(sizeof(float)));
    CHECK(throwsException([&] { packScriptEntityFloats(ents, "Vec", fields, &small); }));

    ssq::Object scalar = vm.callFunc(vm.findFunc("getScalar"), vm);
    CHECK_EQ(packScriptEntityFloats(scalar, "Vec", fields, &buf), 0);
}
