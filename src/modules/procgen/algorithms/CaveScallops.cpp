#include "procgen/algorithms/CaveScallops.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen {
namespace {

constexpr float Pi = 3.1415926535f;

float smoothstep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

float scallopCell(float along, float angle, float radius, float scale, float phaseOffset, float exponent,
                  float flowSeparation) {
    const int   cells          = std::max(3, int(std::round(2.f * Pi * radius / scale)));
    const float flowPhase      = along * 2.f * Pi / scale + phaseOffset;
    const float crossPhase     = angle * float(cells) + 0.5f * std::sin(flowPhase * 0.5f) + phaseOffset * 0.37f;
    const float stationaryWave = 0.5f + 0.5f * std::cos(flowPhase) + 0.16f * std::sin(flowPhase);
    const float separatedPhase = flowPhase + (0.32f + 0.24f * (1.f - exponent)) * std::cos(flowPhase);
    const float separatedWave  = 0.5f + 0.5f * std::cos(separatedPhase) + 0.1f * std::sin(separatedPhase);
    const float shiftedWave = std::clamp(stationaryWave + flowSeparation * (separatedWave - stationaryWave), 0.f, 1.f);
    const float crossWave   = 0.5f + 0.5f * std::cos(crossPhase);
    return std::pow(shiftedWave * crossWave, exponent);
}

float             dot(CaveHydrologyVec3 a, CaveHydrologyVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
CaveHydrologyVec3 sub(CaveHydrologyVec3 a, CaveHydrologyVec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
CaveHydrologyVec3 add(CaveHydrologyVec3 a, CaveHydrologyVec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
CaveHydrologyVec3 mul(CaveHydrologyVec3 a, float scale) { return {a.x * scale, a.y * scale, a.z * scale}; }

}  // namespace

CaveScallopSample sampleCaveScallops(const CaveScallopInput& input) {
    CaveScallopSample result;
    const float       hydraulicScale  = 1.f / std::sqrt(std::clamp(input.hydraulicIntensity, 0.25f, 1.45f));
    const float       seedPhase       = float(input.seed % 1009u) * (2.f * Pi / 1009.f);
    const float       patchScale      = std::max(input.baseScale * 5.5f, 1e-4f);
    const float       correlatedField = 0.68f * std::sin(input.along * 2.f * Pi / patchScale + seedPhase) +
                                        0.32f * std::sin(input.angle * 2.f + seedPhase * 1.73f);
    result.scaleMultiplier            = std::exp(input.scaleVariability * 0.38f * correlatedField);
    result.scale = input.baseScale * (1.f + input.hydraulicScaling * (hydraulicScale - 1.f)) * result.scaleMultiplier;
    const float wallMask      = 1.f - smoothstep(input.radius * 1.25f, input.radius * 2.2f, input.distance);
    const float crestExponent = 0.62f - input.maturity * 0.24f;
    const float youngCell =
        scallopCell(input.along, input.angle, input.radius, result.scale, 0.f, crestExponent, input.flowSeparation);
    // Normal ablation broadens troughs until adjacent cells merge. A subdued
    // fine-scale remnant keeps the mature surface from becoming a single wave.
    const float matureScale = result.scale * (1.f + input.maturity * 1.6f);
    const float matureCell =
        scallopCell(input.along, input.angle, input.radius, matureScale, 1.17f, crestExponent, input.flowSeparation);
    const float coarsenedCell = std::max(youngCell * (1.f - input.maturity * 0.68f), matureCell * input.maturity);

    // Curved conduits focus fresh reactant and separated flow on the outer bank.
    // The broad cosine lobe avoids a seam while retaining the scallop-scale cusps.
    const float outerBank = std::max(0.f, std::cos(input.angle - input.outerBankAngle));
    const float bendGain  = 1.f + input.bendUndercut * input.bendStrength * outerBank * 1.35f;
    result.erosion = coarsenedCell * wallMask * std::clamp(result.scale / input.baseScale, 0.7f, 1.5f) * bendGain;
    return result;
}

CavePassageFrame nearestCavePassageFrame(const CaveHydrologyVec3& point, const std::vector<CaveHydrologyPoint>& path,
                                         const std::vector<float>& hydraulicIntensity) {
    CavePassageFrame nearest;
    float            accumulated   = 0.f;
    auto             nodeIntensity = [&](size_t node) {
        if (hydraulicIntensity.empty()) return 1.f;
        if (node == 0) return hydraulicIntensity.front();
        if (node >= path.size() - 1) return hydraulicIntensity.back();
        return 0.5f * (hydraulicIntensity[node - 1] + hydraulicIntensity[node]);
    };
    auto nodeBend = [&](size_t node) {
        if (node == 0 || node + 1 >= path.size()) return CaveHydrologyVec3{};
        const CaveHydrologyVec3 incoming       = sub(path[node].position, path[node - 1].position);
        const CaveHydrologyVec3 outgoing       = sub(path[node + 1].position, path[node].position);
        const float             incomingLength = std::sqrt(dot(incoming, incoming));
        const float             outgoingLength = std::sqrt(dot(outgoing, outgoing));
        if (incomingLength <= 1e-8f || outgoingLength <= 1e-8f) return CaveHydrologyVec3{};
        return sub(mul(outgoing, 1.f / outgoingLength), mul(incoming, 1.f / incomingLength));
    };
    for (size_t i = 1; i < path.size(); ++i) {
        const CaveHydrologyVec3 segment = sub(path[i].position, path[i - 1].position);
        const float             length2 = dot(segment, segment);
        const float             length  = std::sqrt(length2);
        const float             t =
            length2 > 1e-8f ? std::clamp(dot(sub(point, path[i - 1].position), segment) / length2, 0.f, 1.f) : 0.f;
        const CaveHydrologyVec3 closest  = add(path[i - 1].position, mul(segment, t));
        const CaveHydrologyVec3 delta    = sub(point, closest);
        const float             distance = std::sqrt(dot(delta, delta));
        if (distance < nearest.distance) {
            const CaveHydrologyVec3 tangent =
                length > 1e-8f ? mul(segment, 1.f / length) : CaveHydrologyVec3{1.f, 0.f, 0.f};
            CaveHydrologyVec3 binormal{-tangent.z, 0.f, tangent.x};
            const float       binormalLength = std::sqrt(dot(binormal, binormal));
            binormal = binormalLength > 1e-6f ? mul(binormal, 1.f / binormalLength) : CaveHydrologyVec3{1.f, 0.f, 0.f};
            nearest.along                      = accumulated + t * length;
            nearest.angle                      = std::atan2(dot(delta, binormal), delta.y);
            nearest.distance                   = distance;
            nearest.radius                     = path[i - 1].radius + (path[i].radius - path[i - 1].radius) * t;
            nearest.hydraulicIntensity         = nodeIntensity(i - 1) + (nodeIntensity(i) - nodeIntensity(i - 1)) * t;
            nearest.tangent                    = tangent;
            const CaveHydrologyVec3 bend       = add(mul(nodeBend(i - 1), 1.f - t), mul(nodeBend(i), t));
            const float             bendLength = std::sqrt(dot(bend, bend));
            nearest.bendStrength               = std::clamp(bendLength, 0.f, 1.f);
            if (bendLength > 1e-6f) {
                const CaveHydrologyVec3 outer = mul(bend, -1.f / bendLength);
                nearest.outerBankAngle        = std::atan2(dot(outer, binormal), outer.y);
            }
        }
        accumulated += length;
    }
    return nearest;
}

}  // namespace eve::procgen
