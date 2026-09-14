#include "animation/tensor/SmrOnnxRunner.h"

#include "common/Diagnostic.h"

#include <cstring>
#include <fstream>
#include <vector>

namespace eve::animation {
namespace {

tensor::OnnxNamedTensor makeF32(const char* name, std::vector<int64_t> shape, const std::vector<float>& data) {
    tensor::OnnxNamedTensor named;
    named.name           = name;
    named.tensor.element = tensor::OnnxElement::Float32;
    named.tensor.shape   = std::move(shape);
    named.tensor.bytes.resize(data.size() * sizeof(float));
    if (!data.empty()) std::memcpy(named.tensor.bytes.data(), data.data(), named.tensor.bytes.size());
    return named;
}

}  // namespace

Result<void> SmrOnnxRunner::load(std::span<const uint8_t> bytes) {
    auto loaded = tensor::OnnxModel::load(bytes);
    if (!loaded.ok()) return Result<void>::failure(loaded.status());
    model_ = std::move(loaded.value());
    return Result<void>::success();
}

Result<void> SmrOnnxRunner::loadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "SmrOnnxRunner: open failed", path));
    }
    const auto size = static_cast<std::streamoff>(file.tellg());
    if (size <= 0) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "SmrOnnxRunner: empty file", path));
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, "SmrOnnxRunner: read failed", path));
    }
    return load(bytes);
}

tensor::OnnxModelInfo SmrOnnxRunner::info() const {
    if (!model_) return {};
    return model_->info();
}

Result<std::vector<float>> SmrOnnxRunner::run(const SmrFeatureBatch& features) const {
    if (!model_) {
        return Result<std::vector<float>>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "SmrOnnxRunner: no model"));
    }
    if (features.frames <= 0 || features.joints <= 0) {
        return Result<std::vector<float>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "SmrOnnxRunner: empty features"));
    }
    const int64_t T = features.frames;
    const int64_t J = features.joints;
    const int64_t S = std::max(features.sensors, 1);
    const int64_t P = std::max(features.pairs, 1);

    std::vector<tensor::OnnxNamedTensor> feeds;
    feeds.push_back(makeF32("source_rot6d", {1, T, J, 6}, features.sourceRot6d));
    feeds.push_back(makeF32("source_geom", {1, S, 7}, features.sourceGeom));
    feeds.push_back(makeF32("target_geom", {1, S, 7}, features.targetGeom));
    feeds.push_back(makeF32("source_dmi", {1, T, P, 10}, features.sourceDmi));

    const std::string outName = "target_rot6d";
    auto              ran     = model_->run(feeds, std::span<const std::string>(&outName, 1));
    if (!ran.ok()) return Result<std::vector<float>>::failure(ran.status());
    if (ran.value().empty()) {
        return Result<std::vector<float>>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "SmrOnnxRunner: missing output"));
    }
    const auto&        bytes = ran.value()[0].tensor.bytes;
    std::vector<float> out(bytes.size() / sizeof(float));
    if (!out.empty()) std::memcpy(out.data(), bytes.data(), out.size() * sizeof(float));
    return Result<std::vector<float>>::success(std::move(out));
}

}  // namespace eve::animation
