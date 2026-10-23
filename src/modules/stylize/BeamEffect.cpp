#include "stylize/BeamEffect.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <string>
#include <utility>
#include <vector>
#include "common/Diagnostic.h"
#include "stylize/BeamSkillEffect.h"

// BeamEffect.cpp
namespace eve::stylize {
namespace {

bool finite(glm::vec3 value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

struct RandomStream {
    std::uint32_t state = 1;

    float signedUnit() {
        state ^= state << 13u;
        state ^= state >> 17u;
        state ^= state << 5u;
        return static_cast<float>(state >> 8u) * (2.f / 16777216.f) - 1.f;
    }
};

glm::vec3 safeNormal(glm::vec3 value, glm::vec3 fallback) {
    const float length = glm::length(value);
    return length > 1e-6f && std::isfinite(length) ? value / length : fallback;
}

void appendRibbon(TrailMeshSnapshot& mesh, const std::vector<glm::vec3>& centers, const glm::vec3& cameraForward,
                  float widthStart, float widthEnd, float alphaStart, float alphaEnd) {
    if (centers.size() < 2) return;
    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
    for (std::size_t i = 0; i < centers.size(); ++i) {
        const float     t        = static_cast<float>(i) / static_cast<float>(centers.size() - 1u);
        const glm::vec3 previous = centers[i == 0 ? 0 : i - 1u];
        const glm::vec3 next     = centers[std::min(i + 1u, centers.size() - 1u)];
        const glm::vec3 tangent  = safeNormal(next - previous, glm::vec3(0.f, 1.f, 0.f));
        glm::vec3       side     = glm::cross(cameraForward, tangent);
        if (glm::length(side) <= 1e-6f) side = glm::cross(glm::vec3(0.f, 1.f, 0.f), tangent);
        if (glm::length(side) <= 1e-6f) side = glm::vec3(1.f, 0.f, 0.f);
        side                  = glm::normalize(side);
        const float halfWidth = 0.5f * (widthStart + (widthEnd - widthStart) * t);
        const float alpha     = alphaStart + (alphaEnd - alphaStart) * t;
        mesh.vertices.push_back({centers[i] - side * halfWidth, {t, 0.f}, alpha});
        mesh.vertices.push_back({centers[i] + side * halfWidth, {t, 1.f}, alpha});
        if (i == 0) continue;
        const std::uint32_t left0  = base + static_cast<std::uint32_t>((i - 1u) * 2u);
        const std::uint32_t right0 = left0 + 1u;
        const std::uint32_t left1  = base + static_cast<std::uint32_t>(i * 2u);
        const std::uint32_t right1 = left1 + 1u;
        mesh.indices.insert(mesh.indices.end(), {left0, right0, left1, left1, right0, right1});
    }
}

std::vector<glm::vec3> makeCenters(const glm::vec3& start, const glm::vec3& end, std::uint32_t segments,
                                   float amplitude, float cycles, const glm::vec3& cameraForward,
                                   RandomStream& random) {
    std::vector<glm::vec3> centers;
    centers.reserve(static_cast<std::size_t>(segments) + 1u);
    const glm::vec3 direction = safeNormal(end - start, glm::vec3(0.f, 1.f, 0.f));
    glm::vec3       side      = safeNormal(glm::cross(cameraForward, direction), glm::vec3(1.f, 0.f, 0.f));
    glm::vec3       up        = safeNormal(glm::cross(direction, side), glm::vec3(0.f, 1.f, 0.f));
    for (std::uint32_t i = 0; i <= segments; ++i) {
        const float     t        = static_cast<float>(i) / static_cast<float>(segments);
        const float     envelope = std::sin(glm::pi<float>() * t);
        const float     wave     = std::sin(glm::two_pi<float>() * cycles * t + random.signedUnit());
        const glm::vec3 noise =
            (side * (wave + random.signedUnit() * 0.5f) + up * random.signedUnit()) * (amplitude * envelope);
        centers.push_back(start + (end - start) * t + noise);
    }
    centers.front() = start;
    centers.back()  = end;
    return centers;
}

}  // namespace

BeamBuildResult buildBeamEffect(const glm::vec3& start, const glm::vec3& end, const glm::vec3& cameraForward,
                                const BeamEffectConfig& config) {
    BeamBuildResult result;
    if (!finite(start) || !finite(end) || !finite(cameraForward) || config.segments == 0 ||
        config.branchSegments == 0 || !std::isfinite(config.widthStart) || !std::isfinite(config.widthEnd) ||
        config.widthStart <= 0.f || config.widthEnd < 0.f || !std::isfinite(config.noiseAmplitude) ||
        config.noiseAmplitude < 0.f || !std::isfinite(config.branchLength) || config.branchLength < 0.f) {
        result.status = BeamBuildStatus::InvalidConfig;
        return result;
    }
    const glm::vec3 delta  = end - start;
    const float     length = glm::length(delta);
    if (!std::isfinite(length) || length <= 1e-6f) {
        result.status = BeamBuildStatus::DegenerateInput;
        return result;
    }

    RandomStream    random{config.randomSeed ? config.randomSeed : 1u};
    const glm::vec3 view = safeNormal(cameraForward, glm::vec3(0.f, 0.f, -1.f));
    auto centers = makeCenters(start, end, config.segments, config.noiseAmplitude, config.noiseCycles, view, random);
    appendRibbon(result.mesh, centers, view, config.widthStart, config.widthEnd, config.alphaStart, config.alphaEnd);

    const glm::vec3 direction = delta / length;
    glm::vec3       side      = safeNormal(glm::cross(view, direction), glm::vec3(1.f, 0.f, 0.f));
    glm::vec3       up        = safeNormal(glm::cross(direction, side), glm::vec3(0.f, 1.f, 0.f));
    for (std::uint32_t branch = 0; branch < config.branchCount; ++branch) {
        const float       t = static_cast<float>(branch + 1u) / static_cast<float>(config.branchCount + 1u);
        const std::size_t parentIndex =
            std::min(static_cast<std::size_t>(std::round(t * config.segments)), centers.size() - 1u);
        const glm::vec3 branchDirection =
            safeNormal(direction * 0.35f + side * random.signedUnit() + up * random.signedUnit(), side);
        const glm::vec3 branchStart = centers[parentIndex];
        const glm::vec3 branchEnd   = branchStart + branchDirection * (length * config.branchLength);
        auto branchCenters = makeCenters(branchStart, branchEnd, config.branchSegments, config.noiseAmplitude * 0.5f,
                                         config.noiseCycles, view, random);
        appendRibbon(result.mesh, branchCenters, view, config.widthStart * config.branchWidthScale, 0.f,
                     config.alphaStart, 0.f);
    }
    result.status = BeamBuildStatus::Built;
    return result;
}

}  // namespace eve::stylize

// BeamSkillEffect.cpp
namespace eve::stylize {

BeamSkillEffect::BeamSkillEffect(BeamSkillPreset preset) : preset_(preset) {
    MeshEffectPlayback playback;
    switch (preset_) {
        case BeamSkillPreset::Laser:
            geometry_.segments       = 8;
            geometry_.widthStart     = 0.08f;
            geometry_.widthEnd       = 0.08f;
            geometry_.alphaStart     = 1.f;
            geometry_.alphaEnd       = 1.f;
            geometry_.noiseAmplitude = 0.f;
            playback.fadeIn          = 0.03f;
            playback.duration        = 0.2f;
            playback.fadeOut         = 0.06f;
            playback.loop            = true;
            effect_.style().setFloat("refractionStrength", 0.12f);
            effect_.style().setFloat("flowWarp", 0.f);
            effect_.style().setFloat("edgeDistortion", 0.f);
            break;
        case BeamSkillPreset::Lightning:
            geometry_.segments       = 16;
            geometry_.widthStart     = 0.11f;
            geometry_.widthEnd       = 0.02f;
            geometry_.noiseAmplitude = 0.18f;
            geometry_.noiseCycles    = 5.f;
            geometry_.branchCount    = 3;
            playback.fadeIn          = 0.01f;
            playback.duration        = 0.12f;
            playback.fadeOut         = 0.18f;
            effect_.style().setFloat("refractionStrength", 0.08f);
            effect_.style().setFloat("flowWarp", 0.35f);
            effect_.style().setFloat("edgeDistortion", 0.25f);
            break;
        case BeamSkillPreset::ChainLightning:
            geometry_.segments       = 10;
            geometry_.widthStart     = 0.09f;
            geometry_.widthEnd       = 0.025f;
            geometry_.noiseAmplitude = 0.13f;
            geometry_.noiseCycles    = 4.f;
            geometry_.branchCount    = 1;
            playback.fadeIn          = 0.01f;
            playback.duration        = 0.18f;
            playback.fadeOut         = 0.2f;
            effect_.style().setFloat("refractionStrength", 0.06f);
            effect_.style().setFloat("flowWarp", 0.3f);
            effect_.style().setFloat("edgeDistortion", 0.2f);
            break;
    }
    effect_.setPlayback(playback);
}

void BeamSkillEffect::setPath(std::span<const glm::vec3> points) { path_.assign(points.begin(), points.end()); }

void BeamSkillEffect::setEndpoints(const glm::vec3& start, const glm::vec3& end) { path_ = {start, end}; }

BeamBuildResult BeamSkillEffect::build(const glm::vec3& cameraForward) const {
    BeamBuildResult merged;
    if (path_.size() < 2) {
        merged.status = BeamBuildStatus::DegenerateInput;
        return merged;
    }
    for (std::size_t segment = 1; segment < path_.size(); ++segment) {
        BeamEffectConfig segmentConfig = geometry_;
        segmentConfig.randomSeed       = geometry_.randomSeed ^ static_cast<std::uint32_t>(segment * 0x9e3779b9u);
        auto built = buildBeamEffect(path_[segment - 1u], path_[segment], cameraForward, segmentConfig);
        if (built.status != BeamBuildStatus::Built) return built;
        const std::uint32_t base = static_cast<std::uint32_t>(merged.mesh.vertices.size());
        merged.mesh.vertices.insert(merged.mesh.vertices.end(), built.mesh.vertices.begin(), built.mesh.vertices.end());
        merged.mesh.indices.reserve(merged.mesh.indices.size() + built.mesh.indices.size());
        for (const auto index : built.mesh.indices) merged.mesh.indices.push_back(base + index);
    }
    merged.status = BeamBuildStatus::Built;
    return merged;
}

eve::Result<MeshEffectSubmitStatus> BeamSkillEffect::submit(MeshEffectRenderer&    renderer,
                                                            const glm::vec3&       cameraForward,
                                                            const graphics::Color& tint) {
    auto built = build(cameraForward);
    if (built.status != BeamBuildStatus::Built) {
        const auto code = built.status == BeamBuildStatus::DegenerateInput ? eve::DiagnosticCode::InvalidArgument
                                                                           : eve::DiagnosticCode::PreconditionViolation;
        return eve::Result<MeshEffectSubmitStatus>::failure(eve::Diagnostic::error(
            code, "beam skill path could not produce renderable geometry", {}, {}, "stylize.beam-skill"));
    }
    return renderer.submitTrail(effect_, built.mesh, glm::mat4(1.f), tint);
}

}  // namespace eve::stylize
