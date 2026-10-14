#include "graphics/VegetationMotion.h"
#include <algorithm>
#include <cmath>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <limits>
#include <new>

namespace eve::graphics {
namespace {
bool       finite(glm::vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }
bool       finite(glm::vec3 p) { return finite(glm::vec2(p)) && std::isfinite(p.z); }
bool       finite(glm::vec4 p) { return finite(glm::vec3(p)) && std::isfinite(p.w); }
bool       unit(float p) { return std::isfinite(p) && p >= 0.f && p <= 1.f; }
bool       nonnegative(float p) { return std::isfinite(p) && p >= 0.f && p <= 1000000.f; }
float      saturate(float v) { return std::clamp(v, 0.f, 1.f); }
double     fract(double v) { return v - std::floor(v); }
Diagnostic invalid() {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid vegetation motion input or output overflow", {},
                             {}, "graphics.vegetation");
}
glm::vec4 repeatNoise(const VegetationMask& texture, glm::dvec2 uv) {
    if (!std::isfinite(uv.x) || !std::isfinite(uv.y)) return glm::vec4(std::numeric_limits<float>::quiet_NaN());
    const double x  = fract(uv.x) * texture.width - 0.5;
    const double y  = fract(uv.y) * texture.height - 0.5;
    const int    x0 = int(std::floor(x)), y0 = int(std::floor(y));
    auto         pixel = [&](int px, int py) {
        const auto ix = uint32_t((px + int(texture.width)) % int(texture.width));
        const auto iy = uint32_t((py + int(texture.height)) % int(texture.height));
        return texture.pixels[size_t(iy) * texture.width + ix];
    };
    return glm::mix(glm::mix(pixel(x0, y0), pixel(x0 + 1, y0), float(fract(x))),
                    glm::mix(pixel(x0, y0 + 1), pixel(x0 + 1, y0 + 1), float(fract(x))), float(fract(y)));
}
glm::vec3 rotate(glm::vec3 position, glm::vec3 axis, float angle) {
    const auto onAxis = axis * glm::dot(axis, position);
    const auto other  = position - onAxis;
    return onAxis + other * std::cos(angle) + glm::cross(axis, other) * std::sin(angle);
}
struct Frame {
    glm::mat3 inverseModel, normalMatrix;
    glm::vec3 parentScale, globalDirection;
    double    time;
};
Frame makeFrame(const VegetationMotion& motion, VegetationGlobals globals) {
    Frame frame;
    frame.inverseModel = glm::inverse(glm::mat3(motion.objectToWorld));
    frame.normalMatrix = glm::transpose(frame.inverseModel);
    for (int i = 0; i < 3; ++i)
        frame.parentScale[i] =
            1.f / glm::length(glm::vec3(frame.inverseModel[0][i], frame.inverseModel[1][i], frame.inverseModel[2][i]));
    frame.globalDirection = {globals.motion.x, 0.f, globals.motion.y};
    frame.time =
        std::lerp(motion.time, motion.time * motion.timeScale + motion.timeOffset, double(motion.timeOverride));
    return frame;
}
glm::vec3 directionInObject(const Frame& frame, glm::vec3 worldDirection) {
    return (frame.inverseModel * worldDirection) * frame.parentScale;
}
struct LocalDeformation {
    glm::vec3 position;
    float     highlight;
};
LocalDeformation deformLocal(const VegetationVertex& v, glm::vec3 position, const VegetationSample& field,
                             const VegetationMotion& motion, const Frame& frame) {
    const bool   batched       = motion.batchMode == VegetationBatchMode::Batched;
    const auto   pivot         = motion.usePivots ? v.pivot : glm::vec3(0.f);
    const auto   world         = glm::vec3(motion.objectToWorld * glm::vec4(position, 1.f));
    const auto   objectWorld   = batched ? world : glm::vec3(motion.objectToWorld * glm::vec4(pivot, 1.f));
    const auto   shifted       = world - motion.worldOrigin;
    const auto   objectShifted = objectWorld - motion.worldOrigin;
    const double positionHash  = std::sin(double(objectShifted.x) * 12.9898 + double(objectShifted.z) * 78.233);
    const float  variation     = std::clamp(
        batched ? v.variation : float(fract(positionHash * (1.f - motion.dynamicMode) + v.variation)), 0.01f, 0.99f);
    const auto       noisePosition = glm::mix(shifted, objectShifted, motion.rigidity);
    const glm::dvec2 uv            = glm::dvec2(noisePosition.x, noisePosition.z) *
                          double((motion.bendingScale + 0.2f) * motion.noiseTiling * 0.0075f);
    const double     flowTime = (frame.time * motion.bendingSpeed + motion.bendingVariation * variation) * 0.03;
    const double     cycle    = fract(flowTime);
    const glm::dvec2 wind(frame.globalDirection.x, frame.globalDirection.z);
    const auto       noise =
        glm::mix(repeatNoise(motion.noise, uv - wind * cycle),
                 repeatNoise(motion.noise, uv - wind * fract(flowTime + 0.5)), float(std::abs(cycle - 0.5) / 0.5));
    const auto  noiseDirection = directionInObject(frame, {noise.r * 2.f - 1.f, 0.f, noise.g * 2.f - 1.f});
    const auto  windDirection  = directionInObject(frame, frame.globalDirection);
    const auto  reactDirection = directionInObject(frame, {field.motion.x, 0.f, field.motion.y});
    const float rawPower       = saturate(field.motion.z);
    const float power          = 1.f - (1.f - rawPower) * (1.f - rawPower);
    const float bendingMask    = 2.f * (batched ? v.bending * v.bending * v.boundsHeight : v.bending);
    const auto  bendDirection  = glm::mix(noiseDirection, windDirection, power * 0.6f);
    const auto  bending        = bendDirection * (bendingMask * motion.bending * power * power * motion.globalBending);
    const float interactionRemap =
        saturate(std::clamp(v.bending, 0.0001f, 0.9999f) / (motion.interactionMask + 0.0001f));
    const float interactionMask =
        2.f * (batched ? interactionRemap * interactionRemap * v.boundsHeight : interactionRemap);
    const auto   interaction         = reactDirection * (interactionMask * motion.interaction);
    const float  interactionStrength = saturate(field.motion.w);
    const float  interactionWeight   = saturate(motion.interaction * std::pow(interactionStrength, 4.f));
    const auto   angles              = glm::mix(bending, interaction, interactionWeight);
    const double sum                 = double(shifted.x) + shifted.y + shifted.z;
    const double branchPhase         = sum * motion.branchScale * 0.1;
    const float  sineA =
        float(std::sin(branchPhase + motion.branchVariation * variation + frame.time * motion.branchSpeed));
    const float sineB          = float(std::sin(frame.time * motion.branchSpeed * 0.6842 + branchPhase));
    const float localLength    = glm::length(position);
    const auto  normalPosition = localLength > 1e-8f ? position / localLength : glm::vec3(0.f);
    const float facing =
        batched
            ? 1.f
            : std::max(std::lerp(1.f, glm::dot(normalPosition, -reactDirection) * 0.5f + 0.5f, motion.facing), 0.001f);
    const float noiseBlue       = std::abs(noise.b);
    const float branchAmplitude = facing * power * std::pow(noiseBlue, std::lerp(1.8f, 0.4f, power));
    const float squashScale     = (std::max(sineA, sineB) * 0.5f + 0.5f) * branchAmplitude * motion.branch * v.branch *
                              v.boundsRadius * motion.globalBranch;
    const glm::vec3  squash  = glm::vec3(reactDirection.x, sineA * 0.3f, reactDirection.z) * squashScale;
    const float      rolling = sineA * branchAmplitude * motion.rolling * v.branch * motion.globalBranch;
    const glm::dvec2 flutterUv =
        glm::dvec2(shifted.x, shifted.z) * double(motion.flutterScale * 0.03f) +
        glm::dvec2(motion.flutterVariation * variation + frame.time * motion.flutterSpeed * 0.02);
    const auto  flutterNoise = glm::vec3(repeatNoise(motion.noise, flutterUv)) * 2.f - 1.f;
    const float fadeEnd      = motion.fadeDistance + 0.01f;
    const float fade         = saturate((glm::distance(world, motion.camera) - fadeEnd) / (-fadeEnd * 0.5f + 0.0001f));
    const float flutterAmplitude = motion.flutter * facing * fade * v.flutter * power * motion.globalFlutter *
                                   std::pow(noiseBlue, std::lerp(2.4f, 0.6f, power));
    const auto flutter = flutterNoise * flutterAmplitude;
    auto       result  = position - pivot;
    if (batched)
        result += glm::vec3(angles.x, 0.f, angles.z) + squash + flutter;
    else {
        result = rotate(result, {1, 0, 0}, angles.z);
        result = rotate(result, {0, 0, 1}, -angles.x);
        result = rotate(result + squash, {0, 1, 0}, rolling) + flutter;
    }
    auto        view       = motion.camera - world;
    const float viewLength = glm::length(view);
    view                   = viewLength > 1e-8f ? view / viewLength : glm::vec3(0.f);
    const auto cross       = glm::cross(view, glm::vec3(0, 1, 0));
    const auto push        = frame.inverseModel * glm::vec3(-cross.z, 0, cross.x) * motion.perspectivePush;
    const auto jitter      = glm::vec3(variation * 2.f - 1.f, 0, variation * 2.f - 1.f) * motion.perspectiveNoise;
    result += (push + jitter) * v.bending * std::pow(std::abs(view.y), motion.perspectiveAngle);
    if (!batched) {
        const float size = std::lerp(1.f, saturate(field.vertex.w), motion.globalSize);
        const float fadeSize =
            saturate((glm::distance(motion.camera, objectWorld) / motion.distanceFadeBias - motion.sizeFadeEnd) /
                     (motion.sizeFadeStart - motion.sizeFadeEnd + 0.0001f));
        result *= size * fadeSize;
    }
    return {result + pivot, std::abs(noise.a) * power * fade * v.bending};
}
bool validMotion(const VegetationMotion& m) {
    if (!std::isfinite(m.time) || std::abs(m.time) > 1e12 || !std::isfinite(m.timeScale) ||
        std::abs(m.timeScale) > 1e6 || !std::isfinite(m.timeOffset) || std::abs(m.timeOffset) > 1e12 ||
        !unit(m.timeOverride) || !finite(m.camera) || !finite(m.worldOrigin) || !unit(m.dynamicMode) ||
        !unit(m.rigidity) || !unit(m.facing) || !unit(m.interactionMask) || !unit(m.globalSize) || m.motionLayer > 8 ||
        m.vertexLayer > 8 ||
        (m.batchMode != VegetationBatchMode::Object && m.batchMode != VegetationBatchMode::Batched))
        return false;
    for (float value :
         {m.bending,          m.bendingSpeed,    m.bendingScale,     m.bendingVariation, m.branch,       m.rolling,
          m.branchSpeed,      m.branchScale,     m.branchVariation,  m.flutter,          m.flutterSpeed, m.flutterScale,
          m.flutterVariation, m.globalBending,   m.globalBranch,     m.globalFlutter,    m.noiseTiling,  m.interaction,
          m.fadeDistance,     m.perspectivePush, m.perspectiveNoise, m.perspectiveAngle})
        if (!nonnegative(value)) return false;
    if (!std::isfinite(m.sizeFadeStart) || !std::isfinite(m.sizeFadeEnd) || m.sizeFadeStart < 0 ||
        m.sizeFadeEnd - m.sizeFadeStart < 0.001f || !std::isfinite(m.distanceFadeBias) || m.distanceFadeBias < 0.0001f)
        return false;
    for (int column = 0; column < 4; ++column)
        if (!finite(m.objectToWorld[column])) return false;
    if (m.objectToWorld[0][3] != 0.f || m.objectToWorld[1][3] != 0.f || m.objectToWorld[2][3] != 0.f ||
        m.objectToWorld[3][3] != 1.f)
        return false;
    const float determinant = glm::determinant(glm::mat3(m.objectToWorld));
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12f) return false;
    const auto& texture = m.noise;
    if (texture.width == 0 || texture.height == 0 || texture.width > 2048 || texture.height > 2048 ||
        texture.pixels.size() != size_t(texture.width) * texture.height)
        return false;
    for (auto pixel : texture.pixels)
        if (!finite(pixel) || !unit(pixel.r) || !unit(pixel.g) || !unit(pixel.b) || !unit(pixel.a)) return false;
    return true;
}
}  // namespace

Result<VegetationGeometry> deformVegetation(const VegetationField& field, std::span<const VegetationVertex> vertices,
                                            const VegetationMotion& motion) {
    if (!validMotion(motion) || vertices.size() > 4 * 1024 * 1024)
        return Result<VegetationGeometry>::failure(invalid());
    const bool authored = !vertices.empty() && vertices.front().tangent.has_value();
    for (const auto& v : vertices) {
        if (v.tangent.has_value() != authored) return Result<VegetationGeometry>::failure(invalid());
        if (v.tangent) {
            const auto t     = glm::vec3(*v.tangent);
            const auto cross = glm::cross(v.normal, t);
            if (!finite(t) || (v.tangent->w != 1.f && v.tangent->w != -1.f) || !finite(cross) ||
                glm::length(cross) < 1e-6f)
                return Result<VegetationGeometry>::failure(invalid());
        }
    }
    for (const auto& v : vertices)
        if (!finite(v.position) || !finite(v.normal) || !finite(v.pivot) || !unit(v.bending) || !unit(v.branch) ||
            !unit(v.flutter) || !unit(v.variation) || !unit(v.occlusion) || !unit(v.detail) ||
            !nonnegative(v.boundsHeight) || !nonnegative(v.boundsRadius) || !finite(v.texcoord) ||
            !finite(v.secondaryTexcoord) || !finite(v.detailCoord) || glm::length(v.normal) < 1e-6f ||
            !std::isfinite(glm::length(v.normal)))
            return Result<VegetationGeometry>::failure(invalid());
    const auto frame = makeFrame(motion, field.globalValues());
    try {
        VegetationGeometry geometry;
        geometry.positions.reserve(vertices.size() * 3);
        geometry.normals.reserve(vertices.size() * 3);
        geometry.vegetationFactors.reserve(vertices.size() * 5);
        geometry.motionHighlights.reserve(vertices.size());
        if (authored) {
            geometry.tangents.reserve(vertices.size() * 3);
            geometry.bitangents.reserve(vertices.size() * 3);
        }
        for (const auto& v : vertices) {
            const auto samplePosition = motion.batchMode == VegetationBatchMode::Batched
                                            ? v.position
                                            : (motion.usePivots ? v.pivot : glm::vec3(0.f));
            auto       sample         = field.sample(glm::vec3(motion.objectToWorld * glm::vec4(samplePosition, 1.f)),
                                                     {0, 0, motion.motionLayer, motion.vertexLayer});
            if (!sample.ok()) return Result<VegetationGeometry>::failure(sample.status());
            const auto& f          = sample.value();
            const auto  local      = deformLocal(v, v.position, f, motion, frame);
            const auto  p          = glm::vec3(motion.objectToWorld * glm::vec4(local.position, 1.f));
            const auto  baseNormal = glm::normalize(v.normal);
            const auto  axis       = std::abs(baseNormal.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            const auto  tangent =
                authored
                     ? glm::normalize(glm::vec3(*v.tangent) - baseNormal * glm::dot(baseNormal, glm::vec3(*v.tangent)))
                     : glm::normalize(glm::cross(axis, baseNormal));
            const auto      bitangent = glm::cross(baseNormal, tangent);
            constexpr float epsilon   = 0.001f;
            const auto      dp        = deformLocal(v, v.position + tangent * epsilon, f, motion, frame).position -
                            deformLocal(v, v.position - tangent * epsilon, f, motion, frame).position;
            const auto dq = deformLocal(v, v.position + bitangent * epsilon, f, motion, frame).position -
                            deformLocal(v, v.position - bitangent * epsilon, f, motion, frame).position;
            const auto cross = glm::cross(dp, dq);
            const auto n =
                glm::normalize(frame.normalMatrix * (glm::length(cross) > 1e-12f ? glm::normalize(cross) : baseNormal));
            if (!finite(p) || !finite(n) || !std::isfinite(local.highlight))
                return Result<VegetationGeometry>::failure(invalid());
            geometry.motionHighlights.push_back(local.highlight);
            geometry.vegetationFactors.insert(geometry.vegetationFactors.end(),
                                              {v.variation, v.occlusion, v.detail, v.detailCoord.x, v.detailCoord.y});
            geometry.positions.insert(geometry.positions.end(), {p.x, p.y, p.z});
            geometry.normals.insert(geometry.normals.end(), {n.x, n.y, n.z});
            if (authored) {
                // A collapsed surface has no derivative frame; retain its transformed rest directions.
                const auto model = glm::mat3(motion.objectToWorld);
                const auto t     = glm::normalize(model * (glm::length(dp) > 1e-12f ? dp : tangent));
                const auto b     = glm::normalize(model * (glm::length(dq) > 1e-12f ? dq : bitangent)) * v.tangent->w;
                if (!finite(t) || !finite(b)) return Result<VegetationGeometry>::failure(invalid());
                geometry.tangents.insert(geometry.tangents.end(), {t.x, t.y, t.z});
                geometry.bitangents.insert(geometry.bitangents.end(), {b.x, b.y, b.z});
            }
        }
        return Result<VegetationGeometry>::success(std::move(geometry));
    } catch (const std::bad_alloc&) {
        return Result<VegetationGeometry>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "vegetation geometry allocation failed", {}, {}, "graphics.vegetation"));
    }
}
}  // namespace eve::graphics
