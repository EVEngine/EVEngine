#include "action/ActionCameraBlock.h"
#include "common/Capability.h"
#include "stylize/AttackVfxLayerExecutor.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace eve::stylize_action {
namespace {

float resolveParam(const eve::stylize::AttackVfxLayerStartRequest& request, const char* key,
                   float fallback) {
    if (request.layer) {
        const auto found = request.layer->floatParams.find(key);
        if (found != request.layer->floatParams.end()) return found->second;
    }
    if (request.skin) {
        const auto found = request.skin->shakeProfile.find(key);
        if (found != request.skin->shakeProfile.end()) return found->second;
    }
    return fallback;
}

class CameraAttackVfxExecutor final : public eve::stylize::IAttackVfxLayerExecutor {
public:
    eve::stylize::AttackVfxLayerRole role() const noexcept override {
        return eve::stylize::AttackVfxLayerRole::Camera;
    }

    eve::Result<eve::stylize::AttackVfxLayerHandle> start(
        const eve::stylize::AttackVfxLayerStartRequest& request) override {
        if (!request.layer)
            return eve::Result<eve::stylize::AttackVfxLayerHandle>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "AttackVfx camera layer is null", "layer"));

        eve::action::ActionCameraCueBinding binding;
        if (!request.layer->uri.empty()) {
            auto cue = eve::LogicalId::parse(request.layer->uri);
            if (!cue)
                return eve::Result<eve::stylize::AttackVfxLayerHandle>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument,
                    "camera layer uri must be a LogicalId cue when set", "uri"));
            binding.cue = std::move(*cue);
        } else {
            auto cue = eve::LogicalId::parse("attackvfx:camera");
            if (!cue)
                return eve::Result<eve::stylize::AttackVfxLayerHandle>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvariantViolation, "default camera cue id is invalid", "cue"));
            binding.cue = std::move(*cue);
        }

        const float posAmp = resolveParam(request, "posAmp", static_cast<float>(binding.positionAmplitude));
        const float rotAmp = resolveParam(request, "rotAmp", static_cast<float>(binding.rotationAmplitude));
        const float fovAmp = resolveParam(request, "fovAmp", static_cast<float>(binding.fovAmplitude));
        float duration = resolveParam(request, "duration", 0.0f);
        if (duration <= 0.0f) duration = resolveParam(request, "durationSeconds", 0.25f);
        if (posAmp < 0.0f || rotAmp < 0.0f || duration <= 0.0f)
            return eve::Result<eve::stylize::AttackVfxLayerHandle>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "camera layer amplitudes/duration are invalid",
                "floatParams"));

        binding.positionAmplitude = posAmp;
        binding.rotationAmplitude = rotAmp;
        binding.fovAmplitude      = fovAmp;
        auto parsedDuration       = eve::Duration::fromSeconds(duration);
        if (!parsedDuration)
            return eve::Result<eve::stylize::AttackVfxLayerHandle>::failure(parsedDuration.status());
        binding.duration = std::move(parsedDuration).takeValue();
        if (request.playRequest)
            binding.seed = request.playRequest->sourceId ^ (request.playRequest->targetId << 16);

        eve::action::ActionNotifyContext context;
        std::optional<eve::Result<void>> result;
        eve::cap::forEachUntil<eve::action::IActionCameraCueSink>([&](eve::action::IActionCameraCueSink* sink) {
            if (!sink->supports(binding.cue)) return false;
            result.emplace(sink->trigger(binding, context));
            return true;
        });
        if (!result)
            return eve::Result<eve::stylize::AttackVfxLayerHandle>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::NotFound, "no camera sink accepts the AttackVfx cue",
                binding.cue.format()));
        if (!*result) return eve::Result<eve::stylize::AttackVfxLayerHandle>::failure(result->status());

        const auto id = ++nextId_;
        live_.emplace(id, true);
        return eve::Result<eve::stylize::AttackVfxLayerHandle>::success(
            eve::stylize::AttackVfxLayerHandle{id});
    }

    eve::Result<void> update(eve::stylize::AttackVfxLayerHandle handle, double,
                             const eve::stylize::AttackVfxLayerStartRequest&) override {
        if (!live_.contains(handle.id))
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::StaleHandle, "camera AttackVfx layer handle is stale", "handle"));
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    }

    eve::Result<void> stop(eve::stylize::AttackVfxLayerHandle handle,
                           eve::stylize::AttackVfxStopBehavior) override {
        const auto found = live_.find(handle.id);
        if (found == live_.end())
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::StaleHandle, "camera AttackVfx layer handle is stale", "handle"));
        live_.erase(found);
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

private:
    std::uint64_t nextId_ = 1;
    std::unordered_map<std::uint64_t, bool> live_;
};

CameraAttackVfxExecutor& cameraExecutor() {
    static CameraAttackVfxExecutor instance;
    return instance;
}

bool gCameraRegistered = false;

}  // namespace

void registerCameraAttackVfxExecutor() {
    if (gCameraRegistered) return;
    eve::cap::addListener<eve::stylize::IAttackVfxLayerExecutor>(&cameraExecutor());
    gCameraRegistered = true;
}

void unregisterCameraAttackVfxExecutor() {
    if (!gCameraRegistered) return;
    eve::cap::removeListener<eve::stylize::IAttackVfxLayerExecutor>(&cameraExecutor());
    gCameraRegistered = false;
}

}  // namespace eve::stylize_action
