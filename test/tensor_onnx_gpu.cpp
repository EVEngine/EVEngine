#include "common/config.h"
#if !defined(EVENGINE_WEBGPU)
#include <cstring>
#include <memory>
#include "gpgpu/ComputeProgram.h"
#include "gpgpu/ComputeShader.h"
#include "gpgpu/Gpgpu.h"
#include "gpgpu/GpuBuffer.h"
#include "gpgpu/Sequence.h"
#include "graphics/Graphics.h"
#include "graphics/vulkan/Graphics.h"
#include "tensor/OnnxCompute.h"
#include "tensor/OnnxModel.h"
#include "zeroerr/unittest.h"
namespace {
void initialize() {
    static bool initialized = false;
    if (!initialized) {
        eve::graphics::Graphics::create()->initHeadless(64, 64);
        initialized = true;
    }
}
using Bytes = std::vector<uint8_t>;
void number(Bytes& b, uint64_t n) {
    while (n >= 128) {
        b.push_back(uint8_t(n) | 128);
        n >>= 7;
    }
    b.push_back(uint8_t(n));
}
void integer(Bytes& b, int tag, uint64_t n) {
    number(b, tag * 8);
    number(b, n);
}
void field(Bytes& b, int tag, const Bytes& value) {
    number(b, tag * 8 + 2);
    number(b, value.size());
    b.insert(b.end(), value.begin(), value.end());
}
void  text(Bytes& b, int tag, const std::string& s) { field(b, tag, Bytes(s.begin(), s.end())); }
Bytes chain(size_t count = 4096, float increment = 1, bool invalidReshape = false, bool mixed = false,
            int stages = 16) {
    Bytes graph, tensor, raw(count * sizeof(float));
    for (size_t i = 0; i < count; ++i) {
        float f = float(i);
        std::memcpy(raw.data() + i * 4, &f, 4);
    }
    integer(tensor, 1, count);
    integer(tensor, 2, 1);
    text(tensor, 8, "x");
    field(tensor, 9, raw);
    field(graph, 5, tensor);
    Bytes bias, one(4);
    float f = increment;
    std::memcpy(one.data(), &f, 4);
    integer(bias, 2, 1);
    text(bias, 8, "one");
    field(bias, 9, one);
    field(graph, 5, bias);
    std::string previous = "x";
    for (int i = 0; i < stages; ++i) {
        Bytes n;
        text(n, 1, previous);
        text(n, 1, "one");
        auto next = "v" + std::to_string(i);
        text(n, 2, next);
        text(n, 4, mixed ? std::vector<std::string>{"Add", "Mul", "Sub", "Div"}[i % 4] : "Add");
        field(graph, 1, n);
        previous = next;
        Bytes alias;
        text(alias, 1, previous);
        previous += "alias";
        text(alias, 2, previous);
        text(alias, 4, "Identity");
        field(graph, 1, alias);
    }
    if (invalidReshape) {
        Bytes   t, raw(8), node;
        int64_t dimension = int64_t(count) + 1;
        std::memcpy(raw.data(), &dimension, 8);
        integer(t, 1, 1);
        integer(t, 2, 7);
        text(t, 8, "badShape");
        field(t, 9, raw);
        field(graph, 5, t);
        text(node, 1, previous);
        text(node, 1, "badShape");
        text(node, 2, "invalid");
        text(node, 4, "Reshape");
        field(graph, 1, node);
        previous = "invalid";
    }
    Bytes out, opset, model;
    text(out, 1, previous);
    field(graph, 12, out);
    integer(opset, 2, 14);
    integer(model, 1, 7);
    field(model, 8, opset);
    field(model, 7, graph);
    return model;
}
}  // namespace
TEST_CASE("tensor.onnx.gpu.residentChain") {
    initialize();
    auto provider = eve::tensor::createOnnxGpuCompute();
    REQUIRE(provider.ok());
    auto model = eve::tensor::OnnxModel::load(chain());
    REQUIRE(model.ok());
    for (int repeat = 0; repeat < 2; ++repeat) {
        auto r = model.value()->runGpu({}, *provider.value());
        REQUIRE(r.ok());
        CHECK(r.value().dispatches == 16);
        CHECK(r.value().transfers.uploads == (repeat == 0 ? 2 : 0));
        CHECK(r.value().transfers.shaderCompilations == (repeat == 0 ? 1 : 0));
        CHECK(r.value().transfers.downloads == 1);
        CHECK(r.value().transfers.submissions == 1);
        const auto& raw = r.value().outputs[0].tensor.bytes;
        for (size_t i = 0; i < 4096; ++i) {
            float f;
            std::memcpy(&f, raw.data() + i * 4, 4);
            REQUIRE(f == float(i + 16));
        }
    }
}
TEST_CASE("gpgpu.sequence.stagingSizeReuse") {
    initialize();
    auto* gp = eve::gpgpu::Gpgpu::create();
    REQUIRE(gp->isAvailable());
    std::unique_ptr<eve::gpgpu::Sequence> seq(gp->newSequence());
    for (const auto& sizes :
         std::vector<std::vector<size_t>>{{4, 4096, 64}, {4096, 4, 64}, {8192, 8, 16384, 4}, {4, 16384, 8, 8192}}) {
        std::vector<std::unique_ptr<eve::gpgpu::GpuBuffer>> device, staging;
        std::vector<Bytes>                                  expected;
        seq->begin();
        for (size_t i = 0; i < sizes.size(); ++i) {
            Bytes src(sizes[i], uint8_t(i + 31));
            expected.push_back(src);
            device.emplace_back(gp->newBuffer(int(sizes[i])));
            staging.emplace_back(gp->newBuffer(int(sizes[i]), "staging"));
            seq->recordUpload(device.back().get(), src.data(), src.size());
            // recordUpload promises an immediate copy, including in a multi-upload batch.
            std::fill(src.begin(), src.end(), 0);
            seq->recordDownload(device.back().get(), staging.back().get(), sizes[i]);
        }
        REQUIRE(seq->submitAsync() == eve::gpgpu::SequenceStatus::Submitted);
        REQUIRE(seq->wait() == eve::gpgpu::SequenceStatus::Complete);
        for (size_t i = 0; i < sizes.size(); ++i) {
            Bytes actual(sizes[i]);
            staging[i]->downloadBytes(actual.data(), actual.size());
            REQUIRE(actual == expected[i]);
        }
    }
}

TEST_CASE("gpgpu.sequence.cancelRecordingRetiresDescriptors") {
    initialize();
    auto*                                      gp = eve::gpgpu::Gpgpu::create();
    std::unique_ptr<eve::gpgpu::GpuBuffer>     buffer(gp->newBuffer(4));
    std::unique_ptr<eve::gpgpu::ComputeShader> shader(
        gp->newShader("#version 450\nlayout(local_size_x=64)in;layout(std430,binding=0)buffer B{float x[];};void "
                      "main(){if(gl_GlobalInvocationID.x==0)x[0]*=2.0;}"));
    float value = 1;
    buffer->uploadBytes(&value, 4);
    for (int i = 0; i < 80; ++i) {
        std::unique_ptr<eve::gpgpu::Sequence> canceled(gp->newSequence());
        canceled->begin();
        shader->bindBuffer(0, buffer.get());
        canceled->recordDispatch(shader.get(), 1);
    }
    std::unique_ptr<eve::gpgpu::Sequence>  seq(gp->newSequence());
    std::unique_ptr<eve::gpgpu::GpuBuffer> staging(gp->newBuffer(4, "staging"));
    seq->begin();
    shader->bindBuffer(0, buffer.get());
    seq->recordDispatch(shader.get(), 1);
    seq->recordDownload(buffer.get(), staging.get(), 4);
    seq->submit();
    staging->downloadBytes(&value, 4);
    CHECK(value == 2);
}

TEST_CASE("tensor.onnx.gpu.sessionShapesAndLifetime") {
    auto* previous = eve::graphics::Graphics::create();
    auto  device   = std::make_unique<eve::graphics::vulkan::Graphics>();
    eve::ModuleManager::insert("Graphics", device.get());
    struct Restore {
        eve::graphics::Graphics* previous;
        ~Restore() { eve::ModuleManager::insert("Graphics", previous); }
    } restore{previous};
    device->initHeadless(64, 64);
    auto gpu = eve::tensor::createOnnxGpuCompute();
    REQUIRE(gpu.ok());
    auto original = eve::tensor::OnnxModel::load(chain());
    REQUIRE(original.ok());
    auto alternate = eve::tensor::OnnxModel::load(chain(2048, 2));
    REQUIRE(alternate.ok());
    for (int pass = 0; pass < 3; ++pass) {
        auto& model = pass == 1 ? alternate : original;
        auto  r     = model.value()->runGpu({}, *gpu.value());
        REQUIRE(r.ok());
        CHECK(r.value().transfers.shaderCompilations == (pass < 2 ? 1 : 0));
        const size_t count = pass == 1 ? 2048 : 4096;
        const float  added = pass == 1 ? 32.f : 16.f;
        REQUIRE(r.value().outputs[0].tensor.bytes.size() == count * 4);
        for (size_t i = 0; i < count; ++i) {
            float actual;
            std::memcpy(&actual, r.value().outputs[0].tensor.bytes.data() + i * 4, 4);
            REQUIRE(actual == float(i) + added);
        }
    }
    auto failing = eve::tensor::OnnxModel::load(chain(4096, 1, true));
    REQUIRE(failing.ok());
    auto failure = failing.value()->runGpu({}, *gpu.value());
    REQUIRE(!failure.ok());
    const std::vector<std::string> intermediate{"v15alias"};
    auto                           recovered = failing.value()->runGpu({}, *gpu.value(), intermediate);
    REQUIRE(recovered.ok());
    CHECK(recovered.value().transfers.shaderCompilations == 0);
    CHECK(recovered.value().transfers.uploads == 2);
    float recoveredValue = 0;
    std::memcpy(&recoveredValue, recovered.value().outputs[0].tensor.bytes.data(), 4);
    CHECK(recoveredValue == 16);
    // Session-first retirement cancels its observer and releases device resources.
    {
        auto second = eve::tensor::createOnnxGpuCompute();
        REQUIRE(second.ok());
        auto r = original.value()->runGpu({}, *second.value());
        REQUIRE(r.ok());
    }
    // Device-first retirement clears the surviving session before destroying Vulkan.
    device.reset();
    eve::ModuleManager::insert("Graphics", previous);
    auto stale = original.value()->runGpu({}, *gpu.value());
    REQUIRE(!stale.ok());
    CHECK(stale.error()->message().find("retired") != std::string::npos);
    gpu.value().reset();
}
TEST_CASE("tensor.onnx.gpu.parallelCompilation") {
    // Compilation has no dependency on a live Graphics device.
    auto bytecode = eve::gpgpu::compileComputeSpirv("#version 450\nlayout(local_size_x=64)in;void main(){}");
    REQUIRE(bytecode.ok());
    REQUIRE(bytecode.value().front() == 0x07230203);
    auto invalidHeader = eve::gpgpu::createComputeShader({});
    REQUIRE(!invalidHeader.ok());
    for (uint32_t invalid : {0u, 9u}) {
        auto r = eve::tensor::createOnnxGpuCompute(invalid);
        REQUIRE(!r.ok());
    }
    initialize();
    Bytes serial;
    for (uint32_t workers : {1u, 4u}) {
        auto provider = eve::tensor::createOnnxGpuCompute(workers);
        REQUIRE(provider.ok());
        auto model = eve::tensor::OnnxModel::load(chain(4096, 2, false, true, 80));
        REQUIRE(model.ok());
        for (int repeat = 0; repeat < 2; ++repeat) {
            auto r = model.value()->runGpu({}, *provider.value());
            REQUIRE(r.ok());
            CHECK(r.value().dispatches == 80);
            CHECK(r.value().transfers.shaderCompilations == (repeat ? 0 : 4));
            CHECK(r.value().transfers.compilerPeakWorkers <= workers);
            if (!repeat)
                CHECK(r.value().transfers.compilerPeakWorkers >= (workers == 1 ? 1 : 2));
            else
                CHECK(r.value().transfers.compilerPeakWorkers == 0);
            const auto& raw = r.value().outputs[0].tensor.bytes;
            for (size_t i = 0; i < 4096; ++i) {
                float actual;
                std::memcpy(&actual, raw.data() + i * 4, 4);
                REQUIRE(actual == float(i + 20));
            }
            if (serial.empty())
                serial = raw;
            else
                REQUIRE(raw == serial);
        }
        // A fresh, abandoned segment has active CPU compile jobs but no submitted
        // GPU work. Retry must rebuild those programs and re-upload its weights.
        auto canceled = eve::tensor::OnnxModel::load(chain(4093, 2, true, true));
        REQUIRE(canceled.ok());
        auto failed = canceled.value()->runGpu({}, *provider.value());
        REQUIRE(!failed.ok());
        const std::vector<std::string> requested{"v15alias"};
        auto                           retried = canceled.value()->runGpu({}, *provider.value(), requested);
        REQUIRE(retried.ok());
        CHECK(retried.value().transfers.shaderCompilations == 4);
        CHECK(retried.value().transfers.uploads == 2);
        // Compilation failure travels through the future and run cleanup;
        // subsequent valid work on the same session remains usable.
        eve::tensor::OnnxKernel bad{"#version 450\nvoid main(){missing_function();}", {}, 4, 1};
        auto                    rejected = provider.value()->dispatch(bad);
        REQUIRE(!rejected.ok());
        auto recovered = model.value()->runGpu({}, *provider.value());
        REQUIRE(recovered.ok());
        REQUIRE(recovered.value().outputs[0].tensor.bytes == serial);
    }
}
#endif
