#include "tensor/AffineQuant.h"
#include "tensor/OnnxByteStorage.h"
#include "tensor/OnnxCompute.h"
#include "tensor/OnnxModel.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace {
using Bytes = std::vector<uint8_t>;
void varint(Bytes& b, uint64_t n) {
    while (n >= 128) {
        b.push_back(static_cast<uint8_t>(n) | 128);
        n >>= 7;
    }
    b.push_back(static_cast<uint8_t>(n));
}
void number(Bytes& b, int id, uint64_t n) {
    varint(b, id * 8);
    varint(b, n);
}
void field(Bytes& b, int id, const Bytes& value) {
    varint(b, id * 8 + 2);
    varint(b, value.size());
    b.insert(b.end(), value.begin(), value.end());
}
Bytes text(const char* s) { return Bytes(s, s + std::char_traits<char>::length(s)); }
Bytes unknownModel() {
    Bytes node, graph, output, opset, model;
    field(node, 2, text("y"));
    field(node, 3, text("unsupported-node"));
    field(node, 4, text("UnknownOperator"));
    field(graph, 1, node);
    field(output, 1, text("y"));
    field(graph, 12, output);
    number(opset, 2, 14);
    number(model, 1, 7);
    field(model, 8, opset);
    field(model, 7, graph);
    return model;
}
}  // namespace

TEST_CASE("tensor.onnx.affine.roundingAndRange") {
    using namespace eve::tensor::affine;
    const std::vector<float> input{-1000, -1.25f, -.75f, -.25f, .25f, .75f, 1.25f, 1000};
    auto                     q = quantize(input, .5f, 0, true);
    REQUIRE(q.ok());
    CHECK(q.value() == Bytes({128, 254, 254, 0, 0, 2, 2, 127}));
    auto zero = dynamicQuantize(std::vector<float>{0, 0});
    REQUIRE(zero.ok());
    CHECK(zero.value().scale == 1);
    auto invalid = quantize(input, 0, 0, false);
    CHECK(!invalid.ok());
    auto nonfinite = dynamicQuantize(std::vector<float>{std::numeric_limits<float>::infinity()});
    CHECK(!nonfinite.ok());
}

TEST_CASE("tensor.onnx.affine.exactAccumulators") {
    using namespace eve::tensor::affine;
    Bytes a(1025, 255), b(1025, 127);
    auto  product = matmul({a, false}, {b, true}, 1, 1025, 1);
    REQUIRE(product.ok());
    CHECK(product.value()[0] == 33194625);
    auto mismatch = matmul({a, false}, {b, true}, 2, 1025, 1);
    CHECK(!mismatch.ok());
    auto zero = matmul({a, false}, {b, true}, 1, 1025, 1, 256);
    CHECK(!zero.ok());
    Bytes largeA(40000, 255), largeB(40000, 255);
    auto  overflow = matmul({largeA, false}, {largeB, false}, 1, 40000, 1);
    CHECK(!overflow.ok());
}

TEST_CASE("tensor.onnx.affine.convolutionValidation") {
    using namespace eve::tensor::affine;
    ConvShape shape;
    shape.width    = 3;
    shape.kernelW  = 3;
    shape.padLeft  = 1;
    shape.padRight = 1;
    const Bytes                x{11, 12, 13}, w{1, 1, 1};
    const std::vector<int32_t> z{0};
    auto                       result = conv({x, false}, {w, false}, shape, 10, z);
    REQUIRE(result.ok());
    CHECK(result.value() == std::vector<int32_t>({3, 6, 5}));
    shape.groups = 2;
    auto invalid = conv({x, false}, {w, false}, shape, 10, z);
    CHECK(!invalid.ok());
}

TEST_CASE("tensor.onnx.import.transactionAndDiagnostics") {
    using eve::tensor::OnnxModel;
    auto bytes = unknownModel();
    auto good  = OnnxModel::load(bytes);
    REQUIRE(good.ok());
    CHECK(good.value()->info().unsupportedNodes.size() == 1);
    auto rejected = good.value()->run({});
    REQUIRE(!rejected.ok());
    REQUIRE(rejected.error() != nullptr);
    CHECK(rejected.error()->code() == eve::DiagnosticCode::Unsupported);
    CHECK(rejected.error()->path() == "unsupported-node");
    bytes.pop_back();
    auto broken = OnnxModel::load(bytes);
    CHECK(!broken.ok());
    CHECK(good.value()->info().nodeCount == 1);
    auto empty = OnnxModel::load({});
    CHECK(!empty.ok());
    auto overflow = OnnxModel::load(Bytes(12, 255));
    CHECK(!overflow.ok());
}

TEST_CASE("tensor.onnx.import.owningOutputs") {
    Bytes tensor, attribute, node, graph, output, opset, encoded;
    number(tensor, 2, 1);
    const float expected = 1.5f;
    Bytes       payload(sizeof(float));
    std::memcpy(payload.data(), &expected, sizeof(float));
    field(tensor, 9, payload);
    field(attribute, 1, text("value"));
    number(attribute, 20, 4);
    field(attribute, 5, tensor);
    field(node, 2, text("y"));
    field(node, 4, text("Constant"));
    field(node, 5, attribute);
    field(graph, 1, node);
    field(output, 1, text("y"));
    field(graph, 12, output);
    number(opset, 2, 14);
    number(encoded, 1, 7);
    field(encoded, 8, opset);
    field(encoded, 7, graph);
    auto imported = eve::tensor::OnnxModel::load(encoded);
    REQUIRE(imported.ok());
    encoded.clear();
    auto result = imported.value()->run({});
    REQUIRE(result.ok());
    imported.value().reset();
    REQUIRE(result.value().size() == 1);
    REQUIRE(result.value()[0].tensor.bytes.size() == 4);
    float actual;
    std::memcpy(&actual, result.value()[0].tensor.bytes.data(), 4);
    CHECK(actual == expected);
}

TEST_CASE("tensor.onnx.gpu.failureAndScope") {
    class FailedGpu final : public eve::tensor::OnnxCompute {
    public:
        int                               begins = 0, ends = 0, dispatches = 0;
        eve::Result<std::vector<uint8_t>> dispatch(const eve::tensor::OnnxKernel&) override {
            ++dispatches;
            return eve::Result<std::vector<uint8_t>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected GPU failure"));
        }

    protected:
        void beginRun() noexcept override { ++begins; }
        void endRun() noexcept override { ++ends; }
    } gpu;

    auto unsupported = eve::tensor::OnnxModel::load(unknownModel());
    REQUIRE(unsupported.ok());

    auto rejected = unsupported.value()->runGpu({}, gpu);

    CHECK(!rejected.ok());
    CHECK(gpu.dispatches == 0);
    CHECK(gpu.begins == 1);
    CHECK(gpu.ends == 1);
    Bytes tensor, node, graph, output, opset, model;
    number(tensor, 2, 1);
    field(tensor, 8, text("x"));
    float value = -2;
    Bytes data(4);
    std::memcpy(data.data(), &value, 4);
    field(tensor, 9, data);
    field(node, 1, text("x"));
    field(node, 2, text("y"));
    field(node, 3, text("relu-test"));
    field(node, 4, text("Relu"));
    field(graph, 1, node);
    field(graph, 5, tensor);
    field(output, 1, text("y"));
    field(graph, 12, output);
    number(opset, 2, 14);
    number(model, 1, 7);
    field(model, 8, opset);
    field(model, 7, graph);
    auto imported = eve::tensor::OnnxModel::load(model);
    REQUIRE(imported.ok());

    auto failed = imported.value()->runGpu({}, gpu);
    REQUIRE(!failed.ok());
    CHECK(failed.error()->code() == eve::DiagnosticCode::Failed);
    CHECK(failed.error()->path() == "relu-test");
    CHECK(gpu.dispatches == 1);
    CHECK(gpu.begins == 2);
    CHECK(gpu.ends == 2);
    auto cpu = imported.value()->run({});
    REQUIRE(cpu.ok());
    float out = -1;
    std::memcpy(&out, cpu.value()[0].tensor.bytes.data(), 4);
    CHECK(out == 0);
}

TEST_CASE("tensor.onnx.storage.aliasAndCpuMutation") {
    using eve::tensor::onnx_detail::ByteStorage;
    class Deferred final : public eve::tensor::OnnxDeviceStorage {
    public:
        mutable int                       reads = 0;
        eve::Result<std::vector<uint8_t>> readback() const override {
            ++reads;
            return eve::Result<std::vector<uint8_t>>::success({1, 2, 3, 4});
        }
    };
    auto        deferred = std::make_shared<Deferred>();
    ByteStorage original(eve::tensor::OnnxBuffer{{}, deferred, 4});
    ByteStorage alias = original;
    CHECK(alias.size() == 4);
    CHECK(deferred->reads == 0);
    alias.data()[0] = 9;
    CHECK(deferred->reads == 1);
    CHECK(static_cast<const ByteStorage&>(alias)[0] == 9);
    CHECK(static_cast<const ByteStorage&>(original)[0] == 1);
    CHECK(deferred->reads == 2);
    ByteStorage copy = alias;
    copy.data()[1]   = 8;
    CHECK(static_cast<const ByteStorage&>(alias)[1] == 2);
    CHECK(static_cast<const ByteStorage&>(copy)[1] == 8);
}
