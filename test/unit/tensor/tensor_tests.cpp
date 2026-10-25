// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "tensor_llm.cpp",
    "tensor.cpp"
  ]
}
*/
// clang-format on

#include "PrivateTestScope.h"
#include "common/Exception.h"
#include "tensor/AffineQuant.h"
#include "tensor/Graph.h"
#include "tensor/KernelGen.h"
#include "tensor/OnnxByteStorage.h"
#include "tensor/OnnxCompute.h"
#include "tensor/OnnxModel.h"
#include "tensor/Optimizer.h"
#include "tensor/Quant.h"
#include "tensor/TF.h"
#include "tensor/Tensor.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace eve::tensor;

namespace {
using Bytes = std::vector<uint8_t>;

bool near(float actual, float expected, float tolerance = 1e-5f) { return std::fabs(actual - expected) <= tolerance; }

void varint(Bytes& bytes, uint64_t value) {
    while (value >= 128) {
        bytes.push_back(static_cast<uint8_t>(value) | 128);
        value >>= 7;
    }
    bytes.push_back(static_cast<uint8_t>(value));
}
void number(Bytes& bytes, int field, uint64_t value) {
    varint(bytes, static_cast<uint64_t>(field * 8));
    varint(bytes, value);
}
void field(Bytes& bytes, int id, const Bytes& payload) {
    varint(bytes, static_cast<uint64_t>(id * 8 + 2));
    varint(bytes, payload.size());
    bytes.insert(bytes.end(), payload.begin(), payload.end());
}
Bytes stringBytes(const char* text) { return Bytes(text, text + std::char_traits<char>::length(text)); }

std::unique_ptr<Tensor> filled(int rows, int columns, float value) {
    return std::unique_ptr<Tensor>(TF::create()->fill2(rows, columns, value));
}

float maxDifference(const Tensor& left, const Tensor& right) {
    REQUIRE(left.getSize() == right.getSize());
    float maximum = 0.f;
    for (int index = 0; index < left.getSize(); ++index) {
        maximum = std::max(maximum, std::fabs(left.get(index) - right.get(index)));
    }
    return maximum;
}
}  // namespace

// Factory tensors own their storage and expose exact dimensions, values and device metadata.
TEST_CASE("tensor.factory.shapeZeroOneRangeAndScalarValues") {
    auto*                   tf = TF::create();
    std::unique_ptr<Tensor> zero(tf->zeros2(3, 4));
    REQUIRE(zero != nullptr);
    CHECK_EQ(zero->getRank(), 2);
    CHECK_EQ(zero->getDim0(), 3);
    CHECK_EQ(zero->getDim1(), 4);
    CHECK_EQ(zero->getSize(), 12);
    CHECK_EQ(zero->getDevice(), std::string("cpu"));
    CHECK(zero->isEager());
    CHECK(near(zero->reduceSum(), 0.f));
    std::unique_ptr<Tensor> one(tf->ones2(2, 2));
    CHECK(near(one->reduceSum(), 4.f));
    CHECK_EQ(one->getDtype(), std::string("float32"));
    std::unique_ptr<Tensor> range(tf->arange(5));
    CHECK(near(range->get1(4), 4.f));
    CHECK(near(range->reduceMean(), 2.f));
    std::unique_ptr<Tensor> scalar(tf->constantScalar(3.5f));
    CHECK_EQ(scalar->getSize(), 1);
    CHECK(near(scalar->get(0), 3.5f));
}

TEST_CASE("tensor.storage.indexMutationAndInPlaceArithmetic") {
    auto left  = filled(2, 2, 3.f);
    auto right = filled(2, 2, 2.f);
    left->set2(0, 0, 4.f);
    REQUIRE(near(left->get2(0, 0), 4.f));
    left->addInPlace(right.get());
    CHECK(near(left->get2(0, 0), 6.f));
    left->multiplyInPlace(right.get());
    CHECK(near(left->get2(0, 0), 12.f));
    left->addScalarInPlace(1.f);
    left->mulScalarInPlace(.5f);
    CHECK(near(left->get2(0, 0), 6.5f));
    CHECK(near(left->get2(1, 1), 5.5f));
}

// Elementwise operators broadcast compatible dimensions and reject incompatible shapes.
TEST_CASE("tensor.elementwise.broadcastUnaryAndInvalidShape") {
    auto*                   tf = TF::create();
    std::unique_ptr<Tensor> matrix(tf->zeros2(2, 3));
    for (int index = 0; index < 6; ++index) matrix->set(index, static_cast<float>(index + 1));
    std::unique_ptr<Tensor> bias(tf->fill1(3, 10.f));
    std::unique_ptr<Tensor> sum(tf->add(matrix.get(), bias.get()));
    REQUIRE_EQ(sum->getRank(), 2);
    CHECK(near(sum->get2(0, 0), 11.f));
    CHECK(near(sum->get2(1, 2), 16.f));
    auto                    scale = filled(2, 1, 2.f);
    std::unique_ptr<Tensor> product(tf->multiply(matrix.get(), scale.get()));
    CHECK(near(product->get2(1, 2), 12.f));
    std::unique_ptr<Tensor> negative(tf->neg(product.get()));
    std::unique_ptr<Tensor> absolute(tf->abs(negative.get()));
    CHECK(near(absolute->get2(1, 2), 12.f));
    std::unique_ptr<Tensor> clipped(tf->clamp(absolute.get(), 0.f, 5.f));
    CHECK(near(clipped->get2(1, 2), 5.f));
    bool threw   = false;
    auto invalid = filled(3, 3, 1.f);
    try {
        std::unique_ptr<Tensor> rejected(tf->add(matrix.get(), invalid.get()));
    } catch (const eve::Exception&) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("tensor.matrix.multiplyAndReshapeTransposeValues") {
    auto*                   tf = TF::create();
    std::unique_ptr<Tensor> left(tf->arange(6));
    std::unique_ptr<Tensor> matrix(tf->reshape2(left.get(), 2, 3));
    REQUIRE(near(matrix->get2(1, 2), 5.f));
    std::unique_ptr<Tensor> transposed(tf->transpose(matrix.get()));
    CHECK_EQ(transposed->getDim0(), 3);
    CHECK_EQ(transposed->getDim1(), 2);
    CHECK(near(transposed->get2(2, 1), 5.f));
    auto                    right = filled(3, 2, 1.f);
    std::unique_ptr<Tensor> result(tf->matmul(matrix.get(), right.get()));
    CHECK_EQ(result->getDim0(), 2);
    CHECK_EQ(result->getDim1(), 2);
    CHECK(near(result->get2(0, 0), 3.f));
    CHECK(near(result->get2(1, 1), 12.f));
}

TEST_CASE("tensor.reduction.axisKeepDimensionsAndArgmax") {
    auto*                   tf = TF::create();
    std::unique_ptr<Tensor> matrix(tf->zeros2(2, 3));
    for (int index = 0; index < 6; ++index) matrix->set(index, static_cast<float>(index + 1));
    std::unique_ptr<Tensor> mean(tf->meanAxis(matrix.get(), 1, 0));
    REQUIRE_EQ(mean->getRank(), 1);
    CHECK(near(mean->get1(0), 2.f));
    CHECK(near(mean->get1(1), 5.f));
    std::unique_ptr<Tensor> kept(tf->sumAxis(matrix.get(), 1, 1));
    CHECK_EQ(kept->getDim1(), 1);
    CHECK(near(kept->get2(0, 0), 6.f));
    std::unique_ptr<Tensor> maximum(tf->maxAxis(matrix.get(), 1, 0));
    CHECK(near(maximum->get1(1), 6.f));
    std::unique_ptr<Tensor> indices(tf->argmax(matrix.get(), 1, 0));
    CHECK(near(indices->get1(0), 2.f));
}

TEST_CASE("tensor.random.sameSeedReplaysAndBoundsOutput") {
    auto* tf = TF::create();
    tf->setRandomSeed(7);
    std::unique_ptr<Tensor> first(tf->randomUniform2(4, 4));
    tf->setRandomSeed(7);
    std::unique_ptr<Tensor> second(tf->rand2(4, 4));
    REQUIRE(near(first->get(0), second->get(0), 1e-7f));
    CHECK(first->reduceMin() >= 0.f);
    CHECK(first->reduceMax() <= 1.f);
}

// Neural operators have explicit numeric reference results and preserve output shapes.
TEST_CASE("tensor.neural.softmaxLogSoftmaxAndNormalization") {
    auto*                   tf = TF::create();
    std::unique_ptr<Tensor> logits(tf->zeros2(2, 3));
    logits->set2(0, 0, 1.f);
    logits->set2(0, 1, 2.f);
    logits->set2(0, 2, 3.f);
    std::unique_ptr<Tensor> probabilities(tf->softmax(logits.get(), -1));
    auto                    reference = std::exp(1.f) / (std::exp(1.f) + std::exp(2.f) + std::exp(3.f));
    REQUIRE(near(probabilities->get2(0, 0), reference));
    CHECK(near(probabilities->reduceSum(), 2.f, 1e-4f));
    std::unique_ptr<Tensor> logarithms(tf->logSoftmax(logits.get(), 1));
    CHECK(near(logarithms->get2(0, 0), std::log(reference), 1e-4f));
    std::unique_ptr<Tensor> normalized(tf->layernorm(logits.get(), 1e-5f));
    CHECK(near(normalized->get2(0, 0), -1.224735f, 1e-4f));
    std::unique_ptr<Tensor> rms(tf->rmsnorm(logits.get(), 1e-5f));
    CHECK(near(rms->get2(0, 0), 1.f / std::sqrt(14.f / 3.f + 1e-5f), 1e-4f));
}

TEST_CASE("tensor.neural.activationConvolutionAndPooling") {
    auto*                   tf = TF::create();
    std::unique_ptr<Tensor> input(tf->linspace(-2.f, 2.f, 5));
    std::unique_ptr<Tensor> gelu(tf->gelu(input.get()));
    std::unique_ptr<Tensor> silu(tf->silu(input.get()));
    REQUIRE(near(gelu->get1(2), 0.f));
    CHECK(near(gelu->get1(4), 1.9546f, 1e-3f));
    CHECK(near(silu->get1(4), 2.f / (1.f + std::exp(-2.f))));
    std::unique_ptr<Tensor> image(tf->zeros4(1, 1, 3, 3));
    for (int index = 0; index < 9; ++index) image->set(index, static_cast<float>(index + 1));
    std::unique_ptr<Tensor> kernel(tf->fill4(1, 1, 3, 3, 1.f));
    std::unique_ptr<Tensor> convolution(tf->conv2d(image.get(), kernel.get(), 1, 0));
    CHECK_EQ(convolution->getDim2(), 1);
    CHECK(near(convolution->get4(0, 0, 0, 0), 45.f));
    std::unique_ptr<Tensor> maximum(tf->maxpool2d(image.get(), 2, 1, 0));
    std::unique_ptr<Tensor> average(tf->avgpool2d(image.get(), 2, 1, 0));
    CHECK(near(maximum->get4(0, 0, 0, 0), 5.f));
    CHECK(near(average->get4(0, 0, 0, 0), 3.f));
}

TEST_CASE("tensor.neural.embeddingSliceConcatAndPermute") {
    auto*                   tf = TF::create();
    std::unique_ptr<Tensor> table(tf->zeros2(4, 2));
    for (int index = 0; index < 8; ++index) table->set(index, static_cast<float>(index + 10));
    std::unique_ptr<Tensor> indices(tf->zeros1(2));
    indices->set1(0, 0.f);
    indices->set1(1, 3.f);
    std::unique_ptr<Tensor> embedded(tf->embedding(table.get(), indices.get()));
    REQUIRE_EQ(embedded->getDim0(), 2);
    CHECK(near(embedded->get2(1, 1), 17.f));
    auto                    extra = filled(2, 1, 99.f);
    std::unique_ptr<Tensor> concatenated(tf->concat2(embedded.get(), extra.get(), 1));
    CHECK_EQ(concatenated->getDim1(), 3);
    CHECK(near(concatenated->get2(0, 2), 99.f));
    std::unique_ptr<Tensor> sliced(tf->slice(concatenated.get(), 1, 1, 3));
    CHECK_EQ(sliced->getDim1(), 2);
    CHECK(near(sliced->get2(1, 0), 17.f));
}

TEST_CASE("tensor.neural.attentionMaskOfOnesMatchesUnmaskedReference") {
    auto* tf = TF::create();
    tf->setRandomSeed(19);
    std::unique_ptr<Tensor> query(tf->randomNormal4(1, 1, 2, 4));
    std::unique_ptr<Tensor> key(tf->randomNormal4(1, 1, 2, 4));
    std::unique_ptr<Tensor> value(tf->randomNormal4(1, 1, 2, 4));
    std::unique_ptr<Tensor> allow(tf->ones4(1, 1, 2, 2));
    std::unique_ptr<Tensor> reference(tf->sdpa(query.get(), key.get(), value.get(), .5f));
    std::unique_ptr<Tensor> masked(tf->sdpaMasked(query.get(), key.get(), value.get(), allow.get(), .5f));
    REQUIRE(reference != nullptr);
    REQUIRE(masked != nullptr);
    CHECK_EQ(masked->getSize(), reference->getSize());
    CHECK(maxDifference(*masked, *reference) < 1e-4f);
}

TEST_CASE("tensor.neural.oneDimensionalConvolutionAndWeightedNorm") {
    auto*                   tf = TF::create();
    std::unique_ptr<Tensor> input(tf->zeros3(1, 1, 4));
    for (int index = 0; index < 4; ++index) input->set3(0, 0, index, static_cast<float>(index + 1));
    std::unique_ptr<Tensor> kernel(tf->zeros3(1, 1, 3));
    kernel->set3(0, 0, 0, 1.f);
    kernel->set3(0, 0, 1, 2.f);
    kernel->set3(0, 0, 2, 1.f);
    std::unique_ptr<Tensor> output(tf->conv1d(input.get(), kernel.get(), 1, 0));
    REQUIRE_EQ(output->getDim2(), 2);
    CHECK(near(output->get3(0, 0, 0), 8.f));
    CHECK(near(output->get3(0, 0, 1), 12.f));
    auto                    vector = filled(2, 4, 2.f);
    std::unique_ptr<Tensor> weight(tf->fill1(4, 3.f));
    std::unique_ptr<Tensor> bias(tf->fill1(4, 1.f));
    std::unique_ptr<Tensor> normalized(tf->layernorm(vector.get(), 1e-5f));
    std::unique_ptr<Tensor> weighted(tf->layernormWB(vector.get(), weight.get(), bias.get(), 1e-5f));
    CHECK(near(weighted->get2(0, 0), normalized->get2(0, 0) * 3.f + 1.f, 1e-4f));
}

// Graph compilation must follow eager results, respect dependencies and reject symbolic reads.
TEST_CASE("tensor.graph.compiledMatmulMatchesEager") {
    auto*                   tf     = TF::create();
    auto                    input  = filled(2, 3, 2.f);
    auto                    weight = filled(3, 2, 1.f);
    std::unique_ptr<Tensor> eager(tf->matmul(input.get(), weight.get()));
    std::unique_ptr<Func>   function(tf->func());
    Tensor*                 symbolic = function->input2(2, 3);
    function->setOutput(tf->matmul(symbolic, weight.get()));
    std::unique_ptr<CompiledFunction> compiled(function->compile());
    std::unique_ptr<Tensor>           actual(compiled->run1(input.get()));
    REQUIRE(actual != nullptr);
    CHECK_EQ(actual->getDim0(), 2);
    CHECK_EQ(actual->getDim1(), 2);
    CHECK(maxDifference(*actual, *eager) < 1e-4f);
    CHECK_EQ(compiled->getDevice(), std::string("cpu"));
}

TEST_CASE("tensor.graph.optimizerOrdersDependenciesBeforeConsumers") {
    auto*                 tf      = TF::create();
    auto                  weights = filled(3, 2, 1.f);
    std::unique_ptr<Func> function(tf->func());
    Tensor*               input = function->input2(2, 3);
    function->setOutput(tf->relu(tf->matmul(input, weights.get())));
    OptimizedGraph optimized = optimizeGraph(function->graph(), function->outputNode());
    CHECK(!optimized.order.empty());
    CHECK_EQ(optimized.outputNode, function->outputNode());
    CHECK_EQ(optimized.nodeSlot.size(), static_cast<size_t>(function->graph().nodeCount()));
    std::vector<bool> seen(static_cast<size_t>(function->graph().nodeCount()), false);
    for (int id : optimized.order) {
        REQUIRE(id >= 0);
        REQUIRE(id < function->graph().nodeCount());
        CHECK(!seen[static_cast<size_t>(id)]);
        const auto& node = function->graph().node(id);
        for (int dependency : {node.in0, node.in1, node.in2, node.in3, node.in4}) {
            if (dependency >= 0) CHECK(seen[static_cast<size_t>(dependency)]);
        }
        seen[static_cast<size_t>(id)] = true;
    }
}

TEST_CASE("tensor.graph.symbolicReadThrowsBeforeExecution") {
    auto*                 tf = TF::create();
    std::unique_ptr<Func> function(tf->func());
    Tensor*               symbolic = function->input1(4);
    bool                  threw    = false;
    try {
        (void)symbolic->get1(0);
    } catch (const eve::Exception&) {
        threw = true;
    }
    REQUIRE(threw);
}

// Quantized weights support read-only inference; every supported dtype has an explicit error tolerance.
TEST_CASE("tensor.quant.weightRoundTripAndReadonlyGuards") {
    auto*                   tf = TF::create();
    std::unique_ptr<Tensor> source(tf->zeros2(4, 8));
    for (int index = 0; index < source->getSize(); ++index) {
        source->set(index, static_cast<float>(index - 16) / 16.f);
    }
    struct Case {
        const char* type;
        float       tolerance;
    };
    const Case cases[]{{"fp16", .002f}, {"int8", .04f}, {"fp8", .10f}, {"int4", .35f}, {"fp4", .40f}};
    for (const auto& item : cases) {
        std::unique_ptr<Tensor> quantized(tf->quantizeWeight(source.get(), item.type, 8));
        REQUIRE(quantized != nullptr);
        CHECK(quantized->isQuantized());
        CHECK_EQ(quantized->getDtype(), std::string(item.type));
        CHECK(maxDifference(*quantized, *source) < item.tolerance);
        bool readThrows = false, writeThrows = false;
        try {
            (void)quantized->data();
        } catch (...) {
            readThrows = true;
        }
        try {
            quantized->set(0, 1.f);
        } catch (...) {
            writeThrows = true;
        }
        CHECK(readThrows);
        CHECK(writeThrows);
    }
}

TEST_CASE("tensor.quant.eagerAndCompiledMatmulAgree") {
    auto* tf      = TF::create();
    auto  input   = filled(2, 8, .25f);
    auto  weights = filled(8, 4, .5f);
    for (const char* type : {"fp16", "int8", "fp8", "int4", "fp4"}) {
        std::unique_ptr<Tensor> quantized(tf->quantizeWeight(weights.get(), type, 8));
        std::unique_ptr<Tensor> eager(tf->matmul(input.get(), quantized.get()));
        std::unique_ptr<Func>   function(tf->func());
        function->setOutput(tf->matmul(function->input2(2, 8), quantized.get()));
        std::unique_ptr<CompiledFunction> compiled(function->compile());
        std::unique_ptr<Tensor>           actual(compiled->run1(input.get()));
        REQUIRE(actual != nullptr);
        CHECK(maxDifference(*actual, *eager) < .01f);
    }
}

TEST_CASE("tensor.quant.embeddingMatchesFloatReferenceWithinDtypeTolerance") {
    auto*                   tf = TF::create();
    std::unique_ptr<Tensor> table(tf->zeros2(10, 6));
    for (int index = 0; index < table->getSize(); ++index) {
        table->set(index, static_cast<float>(index % 9 - 4) / 8.f);
    }
    std::unique_ptr<Tensor> indices(tf->zeros1(3));
    indices->set1(0, 0.f);
    indices->set1(1, 5.f);
    indices->set1(2, 9.f);
    std::unique_ptr<Tensor> reference(tf->embedding(table.get(), indices.get()));
    for (const char* type : {"fp16", "int8", "fp8", "int4", "fp4"}) {
        std::unique_ptr<Tensor> quantized(tf->quantizeWeight(table.get(), type, 6));
        std::unique_ptr<Tensor> output(tf->embedding(quantized.get(), indices.get()));
        REQUIRE(output != nullptr);
        CHECK(maxDifference(*output, *reference) < .4f);
    }
}

// ONNX helpers validate numeric ranges and imported result ownership.
TEST_CASE("tensor.onnx.affineSaturationRoundingAndInvalidScales") {
    using namespace eve::tensor::affine;
    auto values = quantize(std::vector<float>{-1000.f, -1.25f, -.75f, -.25f, .25f, .75f, 1.25f, 1000.f}, .5f, 0, true);
    REQUIRE(values.ok());
    CHECK(values.value() == Bytes({128, 254, 254, 0, 0, 2, 2, 127}));
    auto zero = dynamicQuantize(std::vector<float>{0.f, 0.f});
    REQUIRE(zero.ok());
    CHECK_EQ(zero.value().scale, 1.f);
    CHECK(!quantize(std::vector<float>{1.f}, 0.f, 0, false).ok());
    CHECK(!dynamicQuantize(std::vector<float>{std::numeric_limits<float>::infinity()}).ok());
}

TEST_CASE("tensor.onnx.affineAccumulatorAndConvolutionShapeValidation") {
    using namespace eve::tensor::affine;
    Bytes a(1025, 255), b(1025, 127);
    auto  product = matmul({a, false}, {b, true}, 1, 1025, 1);
    REQUIRE(product.ok());
    CHECK_EQ(product.value()[0], 33194625);
    CHECK(!matmul({a, false}, {b, true}, 2, 1025, 1).ok());
    CHECK(!matmul({a, false}, {b, true}, 1, 1025, 1, 256).ok());
    ConvShape shape;
    shape.width    = 3;
    shape.kernelW  = 3;
    shape.padLeft  = 1;
    shape.padRight = 1;
    auto output    = conv({Bytes{11, 12, 13}, false}, {Bytes{1, 1, 1}, false}, shape, 10, std::vector<int32_t>{0});
    REQUIRE(output.ok());
    CHECK(output.value() == (std::vector<int32_t>{3, 6, 5}));
    shape.groups = 2;
    CHECK(!conv({Bytes{11, 12, 13}, false}, {Bytes{1, 1, 1}, false}, shape, 10, std::vector<int32_t>{0}).ok());
}

TEST_CASE("tensor.onnx.importedConstantSurvivesModelAndBufferRelease") {
    Bytes tensor, attribute, node, graph, output, opset, encoded;
    number(tensor, 2, 1);
    const float expected = 1.5f;
    Bytes       payload(sizeof(float));
    std::memcpy(payload.data(), &expected, sizeof(float));
    field(tensor, 9, payload);
    field(attribute, 1, stringBytes("value"));
    number(attribute, 20, 4);
    field(attribute, 5, tensor);
    field(node, 2, stringBytes("y"));
    field(node, 4, stringBytes("Constant"));
    field(node, 5, attribute);
    field(graph, 1, node);
    field(output, 1, stringBytes("y"));
    field(graph, 12, output);
    number(opset, 2, 14);
    number(encoded, 1, 7);
    field(encoded, 8, opset);
    field(encoded, 7, graph);
    auto model = OnnxModel::load(encoded);
    REQUIRE(model.ok());
    encoded.clear();
    auto result = model.value()->run({});
    REQUIRE(result.ok());
    model.value().reset();
    REQUIRE(result.value().size() == size_t{1});
    float actual = 0.f;
    std::memcpy(&actual, result.value()[0].tensor.bytes.data(), sizeof(float));
    CHECK(near(actual, expected));
}

TEST_CASE("tensor.onnx.deferredStorageAliasCopiesOnCpuMutation") {
    class Deferred final : public OnnxDeviceStorage {
    public:
        mutable int                       reads = 0;
        eve::Result<std::vector<uint8_t>> readback() const override {
            ++reads;
            return eve::Result<std::vector<uint8_t>>::success({1, 2, 3, 4});
        }
    };
    auto                     deferred = std::make_shared<Deferred>();
    onnx_detail::ByteStorage original(OnnxBuffer{{}, deferred, 4});
    onnx_detail::ByteStorage alias = original;
    REQUIRE_EQ(alias.size(), size_t{4});
    CHECK_EQ(deferred->reads, 0);
    alias.data()[0] = 9;
    CHECK_EQ(deferred->reads, 1);
    CHECK_EQ(static_cast<const onnx_detail::ByteStorage&>(alias)[0], uint8_t{9});
    CHECK_EQ(static_cast<const onnx_detail::ByteStorage&>(original)[0], uint8_t{1});
    CHECK_EQ(deferred->reads, 2);
}
