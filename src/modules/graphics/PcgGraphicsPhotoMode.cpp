#include "graphics/PcgGraphicsPhotoMode.h"
#include <cmath>

#include "common/Capability.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Graphics.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem3D.h"

namespace eve::graphics {
PcgGraphicsPhotoModeAuthority::~PcgGraphicsPhotoModeAuthority() {
    if (authority_) cap::removeListener<IPhotoModeFieldSink>(this);
}

void PcgGraphicsPhotoModeAuthority::setTargets(Graphics* graphics, RenderControl* control, Camera3D* camera) {
    graphics_ = graphics;
    control_  = control;
    camera_   = camera;
    project();
}

void PcgGraphicsPhotoModeAuthority::setAuthority(bool enabled) {
    if (enabled == authority_) return;
    if (enabled)
        cap::addListener<IPhotoModeFieldSink>(this);
    else
        cap::removeListener<IPhotoModeFieldSink>(this);
    authority_ = enabled;
}

PhotoModeFieldAcceptance PcgGraphicsPhotoModeAuthority::acceptsPhotoModeField(
    const PhotoModeAssignment& assignment) const noexcept {
    return assignment.domain == PhotoModeDomain::Graphics ||
                   (assignment.domain == PhotoModeDomain::Lighting &&
                    assignment.field == "m_globalShadowDistanceMultiplier")
               ? PhotoModeFieldAcceptance::Accepted
               : PhotoModeFieldAcceptance::Rejected;
}

Result<void> PcgGraphicsPhotoModeAuthority::applyPhotoModeField(const PhotoModeAssignment& assignment) {
    auto fail = [&](const char* message) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, assignment.field,
                                                       {}, "graphics.pcgPhotoMode"));
    };
    auto next         = state_;
    const auto* value = std::get_if<float>(&assignment.value);
    const auto* mode  = std::get_if<int64_t>(&assignment.value);
    if (assignment.field == "m_lodBias") {
        if (!value || !std::isfinite(*value) || *value <= 0) return fail("LOD bias must be positive and finite");
        next.lodBias = *value;
    } else if (assignment.field == "m_antiAliasing") {
        if (!mode || *mode < 0 || *mode > 3) return fail("anti-aliasing mode must be in [0,3]");
        next.antiAliasing = *mode;
    } else if (assignment.field == "m_shadowDistance") {
        if (!value || !std::isfinite(*value) || *value < 0)
            return fail("shadow distance must be finite and non-negative");
        next.shadowDistance = *value;
    } else if (assignment.field == "m_shadowResolution") {
        if (!mode || *mode < 0 || *mode > 3) return fail("shadow resolution must be in [0,3]");
        next.shadowResolution = *mode;
    } else if (assignment.field == "m_shadowCascades") {
        if (!mode || *mode < 0 || *mode > 4) return fail("shadow cascades must be in [0,4]");
        next.shadowCascades = *mode;
    } else if (assignment.field == "m_globalShadowDistanceMultiplier") {
        if (!value || !std::isfinite(*value) || *value < 0 || *value > 5)
            return fail("global shadow distance multiplier must be in [0,5]");
        next.globalShadowDistanceMultiplier = *value;
    } else {
        return fail("unsupported graphics photo-mode field");
    }
    state_ = next;
    project();
    return Result<void>::success();
}

void PcgGraphicsPhotoModeAuthority::project() {
    if (camera_) {
        camera_->data()->lodBias    = state_.lodBias;
        const float shadowDistance = state_.shadowDistance * state_.globalShadowDistanceMultiplier;
        for (int layer = 0; layer < 32; ++layer) camera_->setShadowLayerCullDistance(layer, shadowDistance);
    }
    if (!graphics_ || !control_) return;
    auto* antiAliasing = graphics_->pipelineAntiAliasing();
    const int mode     = int(state_.antiAliasing);
    if (mode == 0) {
        control_->disable("aa");
        control_->disable("taa");
    } else {
        control_->enable("aa");
        control_->enable(mode == 3 ? "taa" : "aa");
        if (mode != 3) control_->disable("taa");
        antiAliasing->setMode(mode == 2 ? "smaa" : mode == 3 ? "taa" : "fxaa");
    }
    control_->compile();
}
}  // namespace eve::graphics
