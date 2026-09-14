#include "stylize/BeamSkillEffect.h"

#include "common/Diagnostic.h"

#include <cstdint>
#include <string>
#include <utility>

namespace eve::stylize {

BeamSkillEffect::BeamSkillEffect(BeamSkillPreset preset) : preset_(preset) {
    MeshEffectPlayback playback;
    switch (preset_) {
        case BeamSkillPreset::Laser:
            geometry_.segments = 8;
            geometry_.widthStart = 0.08f;
            geometry_.widthEnd = 0.08f;
            geometry_.alphaStart = 1.f;
            geometry_.alphaEnd = 1.f;
            geometry_.noiseAmplitude = 0.f;
            playback.fadeIn = 0.03f;
            playback.duration = 0.2f;
            playback.fadeOut = 0.06f;
            playback.loop = true;
            effect_.style().setFloat("refractionStrength", 0.12f);
            effect_.style().setFloat("flowWarp", 0.f);
            effect_.style().setFloat("edgeDistortion", 0.f);
            break;
        case BeamSkillPreset::Lightning:
            geometry_.segments = 16;
            geometry_.widthStart = 0.11f;
            geometry_.widthEnd = 0.02f;
            geometry_.noiseAmplitude = 0.18f;
            geometry_.noiseCycles = 5.f;
            geometry_.branchCount = 3;
            playback.fadeIn = 0.01f;
            playback.duration = 0.12f;
            playback.fadeOut = 0.18f;
            effect_.style().setFloat("refractionStrength", 0.08f);
            effect_.style().setFloat("flowWarp", 0.35f);
            effect_.style().setFloat("edgeDistortion", 0.25f);
            break;
        case BeamSkillPreset::ChainLightning:
            geometry_.segments = 10;
            geometry_.widthStart = 0.09f;
            geometry_.widthEnd = 0.025f;
            geometry_.noiseAmplitude = 0.13f;
            geometry_.noiseCycles = 4.f;
            geometry_.branchCount = 1;
            playback.fadeIn = 0.01f;
            playback.duration = 0.18f;
            playback.fadeOut = 0.2f;
            effect_.style().setFloat("refractionStrength", 0.06f);
            effect_.style().setFloat("flowWarp", 0.3f);
            effect_.style().setFloat("edgeDistortion", 0.2f);
            break;
    }
    effect_.setPlayback(playback);
}

void BeamSkillEffect::setPath(std::span<const glm::vec3> points) {
    path_.assign(points.begin(), points.end());
}

void BeamSkillEffect::setEndpoints(const glm::vec3& start, const glm::vec3& end) {
    path_ = {start, end};
}

BeamBuildResult BeamSkillEffect::build(const glm::vec3& cameraForward) const {
    BeamBuildResult merged;
    if (path_.size() < 2) {
        merged.status = BeamBuildStatus::DegenerateInput;
        return merged;
    }
    for (std::size_t segment = 1; segment < path_.size(); ++segment) {
        BeamEffectConfig segmentConfig = geometry_;
        segmentConfig.randomSeed = geometry_.randomSeed ^
                                   static_cast<std::uint32_t>(segment * 0x9e3779b9u);
        auto built = buildBeamEffect(path_[segment - 1u], path_[segment], cameraForward,
                                     segmentConfig);
        if (built.status != BeamBuildStatus::Built) return built;
        const std::uint32_t base = static_cast<std::uint32_t>(merged.mesh.vertices.size());
        merged.mesh.vertices.insert(merged.mesh.vertices.end(), built.mesh.vertices.begin(),
                                    built.mesh.vertices.end());
        merged.mesh.indices.reserve(merged.mesh.indices.size() + built.mesh.indices.size());
        for (const auto index : built.mesh.indices) merged.mesh.indices.push_back(base + index);
    }
    merged.status = BeamBuildStatus::Built;
    return merged;
}

eve::Result<MeshEffectSubmitStatus> BeamSkillEffect::submit(
    MeshEffectRenderer& renderer, const glm::vec3& cameraForward,
    const graphics::Color& tint) {
    auto built = build(cameraForward);
    if (built.status != BeamBuildStatus::Built) {
        const auto code = built.status == BeamBuildStatus::DegenerateInput
                              ? eve::DiagnosticCode::InvalidArgument
                              : eve::DiagnosticCode::PreconditionViolation;
        return eve::Result<MeshEffectSubmitStatus>::failure(eve::Diagnostic::error(
            code, "beam skill path could not produce renderable geometry", {}, {},
            "stylize.beam-skill"));
    }
    return renderer.submitTrail(effect_, built.mesh, glm::mat4(1.f), tint);
}

}  // namespace eve::stylize
