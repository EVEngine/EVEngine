#include "animation/tensor/SmrNeuralProvider.h"

#include "animation/AnimClip.h"
#include "animation/AnimSkeleton.h"
#include "common/Diagnostic.h"

#include <algorithm>

namespace eve::animation {

std::string SmrNeuralProvider::name() const { return loadedPath_.empty() ? "tensor-meshret" : "onnx"; }

bool SmrNeuralProvider::isReady(const AnimRetargetProfile* profile) const {
    if (!profile) return true;
    if (!profile->getNeuralRetargetEnabled()) return false;
    const std::string& backend = profile->getNeuralBackend();
    if (backend == "onnx") return ensureOnnx(*profile).ok();
    if (backend == "tensor") return true;
    if (!profile->getNeuralModelPath().empty()) return ensureOnnx(*profile).ok();
    return true;
}

Result<void> SmrNeuralProvider::ensureOnnx(const AnimRetargetProfile& profile) const {
    const std::string& path = profile.getNeuralModelPath();
    if (path.empty()) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "SmrNeuralProvider: empty ONNX path"));
    }
    if (modelLoaded_ && loadedPath_ == path) return Result<void>::success();
    auto loaded = onnx_.loadFile(path);
    if (!loaded.ok()) return loaded;
    loadedPath_  = path;
    modelLoaded_ = true;
    return Result<void>::success();
}

Result<SmrNeuralResult> SmrNeuralProvider::applyRot6d(const SmrNeuralRequest& request, const SmrFeatureBatch& features,
                                                      const std::vector<float>& rot6d,
                                                      const std::string&        backend) const {
    if (!request.targetClip || !request.targetSkeleton) {
        return Result<SmrNeuralResult>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "SmrNeuralProvider: null clip/skeleton"));
    }
    if (static_cast<int>(rot6d.size()) < features.frames * features.joints * 6) {
        return Result<SmrNeuralResult>::failure(
            Diagnostic::error(DiagnosticCode::InvariantViolation, "SmrNeuralProvider: rot6d size mismatch"));
    }

    AnimClip&   clip     = *request.targetClip;
    const float duration = clip.getDuration();
    for (int joint = 0; joint < features.joints; ++joint) {
        clip.clearTrack(joint);
        for (int frame = 0; frame < features.frames; ++frame) {
            const float  time = duration <= 0.f ? 0.f
                                                : duration * static_cast<float>(frame) /
                                                     static_cast<float>(std::max(features.frames - 1, 1));
            const float* r6   = rot6d.data() + static_cast<size_t>((frame * features.joints + joint) * 6);
            float        qx = 0.f, qy = 0.f, qz = 0.f, qw = 1.f;
            rot6dToQuat(r6, qx, qy, qz, qw);
            clip.addRotationKey(joint, time, qx, qy, qz, qw);
        }
    }

    SmrNeuralResult result;
    result.framesWritten = features.frames;
    result.backend       = backend;
    result.sensorCount   = static_cast<size_t>(features.sensors);
    result.pairCount     = static_cast<size_t>(features.pairs);
    return Result<SmrNeuralResult>::success(std::move(result));
}

Result<SmrNeuralResult> SmrNeuralProvider::retarget(const SmrNeuralRequest& request) {
    if (!request.sourceClip || !request.targetClip || !request.sourceSkeleton || !request.targetSkeleton ||
        !request.profile) {
        return Result<SmrNeuralResult>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "SmrNeuralProvider.retarget: null argument"));
    }

    SmrFeatureBatch features = buildSmrFeatures(*request.sourceClip, request.sourceSkeleton, request.targetSkeleton,
                                                request.sourceSkin, request.targetSkin, 2, 4);

    const std::string& backendHint = request.profile->getNeuralBackend();
    const bool         preferOnnx =
        backendHint == "onnx" || (backendHint != "tensor" && !request.profile->getNeuralModelPath().empty());
    if (preferOnnx) {
        auto ready = ensureOnnx(*request.profile);
        if (ready.ok()) {
            auto ran = onnx_.run(features);
            if (ran.ok()) return applyRot6d(request, features, ran.value(), "onnx");
            if (backendHint == "onnx") return Result<SmrNeuralResult>::failure(ran.status());
        } else if (backendHint == "onnx") {
            return Result<SmrNeuralResult>::failure(ready.status());
        }
    }

    auto ran = net_.forward(features);
    if (!ran.ok()) return Result<SmrNeuralResult>::failure(ran.status());
    return applyRot6d(request, features, ran.value(), "tensor-meshret");
}

}  // namespace eve::animation
