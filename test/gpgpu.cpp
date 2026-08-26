#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ScriptTest.h"
#include "common/ECS.h"
#include "common/Exception.h"
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
#include "procgen/PointCompute.h"
#include "procgen/PointGraph.h"
#include "window/Window.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
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

void setPointComputeFailure(const char *stage) {
#ifdef _WIN32
    _putenv_s("EVENGINE_POINT_COMPUTE_FAIL", stage ? stage : "");
#else
    if (stage)
        setenv("EVENGINE_POINT_COMPUTE_FAIL", stage, 1);
    else
        unsetenv("EVENGINE_POINT_COMPUTE_FAIL");
#endif
}

double medianMilliseconds(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    const size_t middle = samples.size() / 2;
    return samples.size() % 2 ? samples[middle] : (samples[middle - 1] + samples[middle]) * 0.5;
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

const char *kScaleKernelWgsl = R"(
struct Data { values: array<f32> };
struct Push { data: array<vec4f, 8> };
@group(0) @binding(0) var<storage, read_write> values: Data;
@group(0) @binding(8) var<uniform> push: Push;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    values.values[gid.x] *= push.data[0].x;
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

const char *kMoveKernelWgsl = R"(
struct Data { values: array<f32> };
struct Push { data: array<vec4f, 8> };
@group(0) @binding(0) var<storage, read_write> pos: Data;
@group(0) @binding(1) var<storage, read_write> vel: Data;
@group(0) @binding(8) var<uniform> push: Push;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= u32(push.data[0].y)) { return; }
    let base = gid.x * 2u;
    pos.values[base] += vel.values[base] * push.data[0].x;
    pos.values[base + 1u] += vel.values[base + 1u] * push.data[0].x;
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

const char* kDoubleKernelWgsl = R"(
struct Data { values: array<f32> };
@group(0) @binding(0) var<storage, read_write> input: Data;
@group(0) @binding(1) var<storage, read_write> output: Data;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    output.values[gid.x] = input.values[gid.x] * 2.0;
}
)";

const char* backendKernel(const char* glsl, const char* wgsl) {
    auto* gfx = eve::graphics::Graphics::create();
    return gfx && gfx->getBackendName() == "webgpu" ? wgsl : glsl;
}

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
    const std::string backend = gfx->getBackendName();
    const bool supportedBackend = backend == "vulkan" || backend == "webgpu";
    CHECK(supportedBackend);
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
        shader = mod->newShader(backendKernel(kScaleKernel, kScaleKernelWgsl));
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
        shader = mod->newShader(backendKernel(kDoubleKernel, kDoubleKernelWgsl));
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

TEST_CASE("gpgpu.procgen.transformParity") {
    if (!tryInitHeadlessGfx()) return;
    constexpr int          count = 2048;
    eve::procgen::PointSet input;
    for (int index = 0; index < count; ++index) {
        const int point = input.add(float(index % 64), float(index % 7), float(index / 64));
        input.setNormal(point, 0.25f, 1.f, -0.5f);
        input.setYaw(point, float(index % 360));
        input.setScale(point, 0.5f, 1.5f, 2.f);
        input.setDensity(point, float(index % 10) * 0.1f);
        input.setPointSeed(point, uint32_t(index + 17));
        input.setStringAttribute(point, "asset", "oak");
    }
    const eve::procgen::PointSet expected =
        eve::procgen::transformPointSet(input, 12.f, -3.f, 8.f, 31.f, 1.25f, 0.75f, 2.f);
    eve::procgen::PointSet     actual;
    eve::procgen::PointCompute compute;
    REQUIRE(compute.transform(input, actual, 12.f, -3.f, 8.f, 31.f, 1.25f, 0.75f, 2.f));
    REQUIRE(actual.getCount() == expected.getCount());
    for (int index = 0; index < count; ++index) {
        CHECK(std::fabs(actual.getX(index) - expected.getX(index)) < 0.0001f);
        CHECK(std::fabs(actual.getY(index) - expected.getY(index)) < 0.0001f);
        CHECK(std::fabs(actual.getZ(index) - expected.getZ(index)) < 0.0001f);
        CHECK(std::fabs(actual.getNormalX(index) - expected.getNormalX(index)) < 0.0001f);
        CHECK(std::fabs(actual.getNormalY(index) - expected.getNormalY(index)) < 0.0001f);
        CHECK(std::fabs(actual.getNormalZ(index) - expected.getNormalZ(index)) < 0.0001f);
        CHECK(actual.getPointSeed(index) == input.getPointSeed(index));
        CHECK(actual.getStringAttribute(index, "asset", "") == "oak");
    }

    const eve::procgen::PointSet reusedExpected =
        eve::procgen::transformPointSet(input, -4.f, 6.f, 2.f, -17.f, 0.5f, 2.f, 1.25f);
    eve::procgen::PointSet reusedActual;
    REQUIRE(compute.transform(input, reusedActual, -4.f, 6.f, 2.f, -17.f, 0.5f, 2.f, 1.25f));
    REQUIRE(reusedActual.getCount() == reusedExpected.getCount());
    CHECK(std::fabs(reusedActual.getX(0) - reusedExpected.getX(0)) < 0.0001f);
    CHECK(std::fabs(reusedActual.getZ(count - 1) - reusedExpected.getZ(count - 1)) < 0.0001f);

    const std::vector<eve::procgen::PointCompute::Transform> chain{
        {2.f, 3.f, -1.f, 15.f, 1.2f, 0.8f, 1.1f},
        {-7.f, 0.5f, 4.f, -33.f, 0.75f, 2.f, 0.6f},
        {1.f, -2.f, 8.f, 91.f, 1.5f, 1.25f, 0.9f},
        {0.f, 6.f, -3.f, 5.f, 0.5f, 0.5f, 2.f},
    };
    eve::procgen::PointSet fusedExpected = input;
    for (const auto &operation : chain)
        fusedExpected =
            eve::procgen::transformPointSet(fusedExpected, operation.x, operation.y, operation.z, operation.yaw,
                                            operation.scaleX, operation.scaleY, operation.scaleZ);
    eve::procgen::PointSet fusedActual;
    REQUIRE(compute.transformChain(input, fusedActual, chain));
    REQUIRE(fusedActual.getCount() == fusedExpected.getCount());
    CHECK_EQ(compute.getUploadCount(), uint64_t(3));
    CHECK_EQ(compute.getDispatchCount(), uint64_t(3));
    CHECK_EQ(compute.getReadbackCount(), uint64_t(3));
    CHECK_EQ(compute.getBufferReuseCount(), uint64_t(2));
    CHECK(compute.getPeakBufferBytes() >= uint64_t(count * 11 * int(sizeof(float))));
    CHECK_EQ(compute.getLastFusedTransformCount(), 4);
    for (int index : {0, count / 2, count - 1}) {
        CHECK(std::fabs(fusedActual.getX(index) - fusedExpected.getX(index)) < 0.0002f);
        CHECK(std::fabs(fusedActual.getY(index) - fusedExpected.getY(index)) < 0.0002f);
        CHECK(std::fabs(fusedActual.getZ(index) - fusedExpected.getZ(index)) < 0.0002f);
        CHECK(std::fabs(fusedActual.getNormalX(index) - fusedExpected.getNormalX(index)) < 0.0002f);
        CHECK(std::fabs(fusedActual.getNormalY(index) - fusedExpected.getNormalY(index)) < 0.0002f);
        CHECK(std::fabs(fusedActual.getNormalZ(index) - fusedExpected.getNormalZ(index)) < 0.0002f);
    }
}

TEST_CASE("gpgpu.procgen.transformChainRejectsInvalidLength") {
    eve::procgen::PointSet input;
    input.add(1.f, 2.f, 3.f);
    eve::procgen::PointSet                                   output;
    eve::procgen::PointCompute                               compute;
    const std::vector<eve::procgen::PointCompute::Transform> empty;
    CHECK(!compute.transformChain(input, output, empty));
    CHECK(output.empty());
    CHECK_EQ(compute.getDispatchCount(), uint64_t(0));

    const std::vector<eve::procgen::PointCompute::Transform> oversized(5);
    CHECK(!compute.transformChain(input, output, oversized));
    CHECK(output.empty());
    CHECK_EQ(compute.getDispatchCount(), uint64_t(0));
}

TEST_CASE("gpgpu.procgen.pointGraphCompilesLinearTransformSegment") {
    if (!tryInitHeadlessGfx()) return;
    constexpr int          count = 2048;
    eve::procgen::PointSet input;
    for (int index = 0; index < count; ++index) input.add(float(index), 2.f, -3.f);

    eve::procgen::PointGraph graph;
    REQUIRE(graph.addNode("source", "input"));
    REQUIRE(graph.setNodePoints("source", &input));
    std::string previous = "source";
    for (int index = 0; index < 4; ++index) {
        const std::string id        = "move-" + std::to_string(index);
        REQUIRE(graph.addNode(id, "transform"));
        REQUIRE(graph.connect(previous, id));
        REQUIRE(graph.setNodeFloat(id, "x", float(index + 1)));
        previous = id;
    }
    REQUIRE(graph.setComputePolicy("gpu"));
    std::unique_ptr<eve::procgen::PointSet> output(graph.execute(previous));
    REQUIRE(bool(output));
    CHECK_EQ(graph.getComputeUploadCount(), uint64_t(1));
    CHECK_EQ(graph.getComputeDispatchCount(), uint64_t(1));
    CHECK_EQ(graph.getComputeReadbackCount(), uint64_t(1));
    CHECK_EQ(graph.getLastFusedTransformCount(), 4);
    CHECK(std::fabs(output->getX(0) - 10.f) < 0.0001f);

    std::unique_ptr<eve::procgen::PointSet> preview(graph.getNodeOutput("move-1"));
    REQUIRE(bool(preview));
    CHECK(std::fabs(preview->getX(0) - 3.f) < 0.0001f);
    CHECK_EQ(graph.getComputeReadbackCount(), uint64_t(1));

    std::unique_ptr<eve::procgen::PointSet> cached(graph.execute(previous));
    REQUIRE(bool(cached));
    CHECK_EQ(graph.getComputeDispatchCount(), uint64_t(1));
    CHECK(graph.getCacheHitCount() > 0);
}

TEST_CASE("gpgpu.procgen.pointGraphFallsBackAtomicallyAfterForcedGpuFailures") {
    if (!tryInitHeadlessGfx()) return;
    for (const char *stage : {"compile", "allocation", "submit", "readback"}) {
        eve::procgen::PointSet input;
        for (int index = 0; index < 2048; ++index) input.add(float(index), 0.f, 0.f);
        eve::procgen::PointGraph graph;
        REQUIRE(graph.addNode("source", "input"));
        REQUIRE(graph.addNode("first", "transform"));
        REQUIRE(graph.addNode("second", "transform"));
        REQUIRE(graph.connect("source", "first"));
        REQUIRE(graph.connect("first", "second"));
        REQUIRE(graph.setNodePoints("source", &input));
        REQUIRE(graph.setNodeFloat("first", "x", 2.f));
        REQUIRE(graph.setNodeFloat("second", "x", 3.f));
        REQUIRE(graph.setComputePolicy("gpu"));

        setPointComputeFailure(stage);
        std::unique_ptr<eve::procgen::PointSet> output(graph.execute("second"));
        setPointComputeFailure(nullptr);
        REQUIRE(bool(output));
        CHECK_EQ(output->getX(0), 5.f);
        CHECK(graph.getComputeFallbackReason().find("forced") != std::string::npos);
        CHECK_EQ(graph.getComputeUploadCount(), uint64_t(0));
        CHECK_EQ(graph.getComputeDispatchCount(), uint64_t(0));
        CHECK_EQ(graph.getComputeReadbackCount(), uint64_t(0));
        std::unique_ptr<eve::procgen::PointSet> intermediate(graph.getNodeOutput("first"));
        REQUIRE(bool(intermediate));
        CHECK_EQ(intermediate->getX(0), 2.f);
    }
}

TEST_CASE("gpgpu.procgen.millionPointAcceptanceBenchmark") {
    if (!std::getenv("EVENGINE_PCG_MILLION_BENCHMARK") || !tryInitHeadlessGfx()) return;
    constexpr int                                            count = 1000000;
    const std::vector<eve::procgen::PointCompute::Transform> chain{
        {2.f, 3.f, -1.f, 15.f, 1.2f, 0.8f, 1.1f},
        {-7.f, 0.5f, 4.f, -33.f, 0.75f, 2.f, 0.6f},
        {1.f, -2.f, 8.f, 91.f, 1.5f, 1.25f, 0.9f},
        {0.f, 6.f, -3.f, 5.f, 0.5f, 0.5f, 2.f},
    };
    eve::procgen::PointSet input;
    for (int index = 0; index < count; ++index) input.add(float(index % 1000), float(index % 31), float(index / 1000));

    const auto measure = [](auto &&operation) {
        const auto started = std::chrono::steady_clock::now();
        operation();
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    };
    auto cpuRun = [&] {
        eve::procgen::PointSet output = input;
        for (const auto &transform : chain)
            output = eve::procgen::transformPointSet(output, transform.x, transform.y, transform.z, transform.yaw,
                                                     transform.scaleX, transform.scaleY, transform.scaleZ);
        REQUIRE(output.getCount() == count);
    };
    eve::procgen::PointGraph fusedGraph;
    REQUIRE(fusedGraph.addNode("source", "input"));
    REQUIRE(fusedGraph.setNodePoints("source", &input));
    std::string previous = "source";
    for (int index = 0; index < 4; ++index) {
        const std::string id = "move-" + std::to_string(index);
        const auto       &transform = chain[size_t(index)];
        REQUIRE(fusedGraph.addNode(id, "transform"));
        REQUIRE(fusedGraph.connect(previous, id));
        REQUIRE(fusedGraph.setNodeFloat(id, "x", transform.x));
        REQUIRE(fusedGraph.setNodeFloat(id, "y", transform.y));
        REQUIRE(fusedGraph.setNodeFloat(id, "z", transform.z));
        REQUIRE(fusedGraph.setNodeFloat(id, "yaw", transform.yaw));
        REQUIRE(fusedGraph.setNodeFloat(id, "scaleX", transform.scaleX));
        REQUIRE(fusedGraph.setNodeFloat(id, "scaleY", transform.scaleY));
        REQUIRE(fusedGraph.setNodeFloat(id, "scaleZ", transform.scaleZ));
        previous = id;
    }
    REQUIRE(fusedGraph.setComputePolicy("gpu"));
    auto fusedRun = [&] {
        fusedGraph.clearCache();
        std::unique_ptr<eve::procgen::PointSet> output(fusedGraph.execute(previous));
        REQUIRE(bool(output));
        REQUIRE(output->getCount() == count);
    };
    eve::procgen::PointCompute isolatedCompute;
    auto                       isolatedRun = [&] {
        eve::procgen::PointSet current = input;
        for (const auto &transform : chain) {
            eve::procgen::PointSet output;
            REQUIRE(isolatedCompute.transform(current, output, transform.x, transform.y, transform.z, transform.yaw,
                                                                    transform.scaleX, transform.scaleY, transform.scaleZ));
            current = std::move(output);
        }
    };

    for (int warmup = 0; warmup < 5; ++warmup) {
        fusedRun();
        isolatedRun();
    }
    cpuRun();
    std::vector<double> cpu, fused, isolated;
    for (int iteration = 0; iteration < 20; ++iteration) {
        cpu.push_back(measure(cpuRun));
        fused.push_back(measure(fusedRun));
        isolated.push_back(measure(isolatedRun));
    }
    const double cpuMedian      = medianMilliseconds(cpu);
    const double fusedMedian    = medianMilliseconds(fused);
    const double isolatedMedian = medianMilliseconds(isolated);
    CHECK(cpuMedian / fusedMedian >= 2.0);
    CHECK(isolatedMedian / fusedMedian >= 1.5);
    CHECK_EQ(fusedGraph.getComputeUploadCount(), uint64_t(25));
    CHECK_EQ(fusedGraph.getComputeDispatchCount(), uint64_t(25));
    CHECK_EQ(fusedGraph.getComputeReadbackCount(), uint64_t(25));
    CHECK_EQ(isolatedCompute.getUploadCount(), uint64_t(100));
    CHECK_EQ(isolatedCompute.getDispatchCount(), uint64_t(100));
    CHECK_EQ(isolatedCompute.getReadbackCount(), uint64_t(100));
    std::cout << "PCG_BENCHMARK_JSON={\"points\":" << count << ",\"operations\":4,\"samples\":20,"
              << "\"cpuMedianMs\":" << cpuMedian << ",\"fusedGpuMedianMs\":" << fusedMedian
              << ",\"isolatedGpuMedianMs\":" << isolatedMedian << ",\"cpuSpeedup\":" << cpuMedian / fusedMedian
              << ",\"roundTripSpeedup\":" << isolatedMedian / fusedMedian << "}\n";
    CHECK_EQ(fusedGraph.getLastFusedTransformCount(), 4);
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
        shader = mod->newShader(backendKernel(kDoubleKernel, kDoubleKernelWgsl));
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
    for (int i = 0; i < count; ++i) CHECK(std::fabs(dst[size_t(i)] - float(i % 7) * 4.f) < 1e-3f);

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
        sys.setShaderSource(backendKernel(kMoveKernel, kMoveKernelWgsl));
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
