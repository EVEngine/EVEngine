#include "procgen/algorithms/CaveSediment.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <utility>

namespace eve::procgen {
namespace {

constexpr float Pi = 3.1415926535f;

float barDistance(float x, float y, float z, const CaveSedimentBar& bar) {
    const float dx = x - bar.x, dy = y - bar.y, dz = z - bar.z;
    const float along  = dx * bar.flowX + dz * bar.flowZ;
    const float across = -dx * bar.flowZ + dz * bar.flowX;
    const float q =
        std::sqrt((along * along) / (bar.length * bar.length) + (across * across) / (bar.width * bar.width) +
                  (dy * dy) / (bar.thickness * bar.thickness));
    return (q - 1.f) * std::min({bar.length, bar.width, bar.thickness});
}

float clastDistance(float x, float y, float z, const CaveSedimentClast& clast) {
    const float dx = x - clast.x, dy = y - clast.y, dz = z - clast.z;
    const float across      = -dx * clast.flowZ + dz * clast.flowX;
    const float along       = dx * clast.flowX + dz * clast.flowZ;
    const float c           = std::cos(clast.pitch);
    const float s           = std::sin(clast.pitch);
    const float tiltedAlong = along * c - dy * s;
    const float tiltedUp    = along * s + dy * c;
    const float q           = std::sqrt((across * across) / (clast.longRadius * clast.longRadius) +
                                        (tiltedUp * tiltedUp) / (clast.thickRadius * clast.thickRadius) +
                                        (tiltedAlong * tiltedAlong) / (clast.shortRadius * clast.shortRadius));
    return (q - 1.f) * std::min({clast.longRadius, clast.thickRadius, clast.shortRadius});
}

float parageneticChannelDistance(float x, float y, float z, const CaveSedimentBar& bar, float strength) {
    const float dx = x - bar.x, dy = y - bar.ceilingY, dz = z - bar.z;
    const float along       = dx * bar.flowX + dz * bar.flowZ;
    const float clamped     = std::clamp(along, -bar.channelHalfLength, bar.channelHalfLength);
    const float endDistance = along - clamped;
    const float phase       = clamped / std::max(bar.channelHalfLength, 1e-5f);
    const float meander     = std::sin(phase * Pi) * bar.channelMeander * strength;
    const float across      = -dx * bar.flowZ + dz * bar.flowX - meander;
    const float q =
        std::sqrt((endDistance * endDistance + across * across) / (bar.channelHalfWidth * bar.channelHalfWidth) +
                  (dy * dy) / (bar.channelLift * bar.channelLift));
    return (q - 1.f) * std::min(bar.channelHalfWidth, bar.channelLift);
}

float alluvialNotchDistance(float x, float y, float z, const CaveSedimentBar& bar) {
    const float dx = x - bar.x, dy = y - bar.palaeofillY, dz = z - bar.z;
    const float along       = dx * bar.flowX + dz * bar.flowZ;
    const float clamped     = std::clamp(along, -bar.notchHalfLength, bar.notchHalfLength);
    const float endDistance = along - clamped;
    const float phase       = clamped / std::max(bar.notchHalfLength, 1e-5f);
    const float across      = -dx * bar.flowZ + dz * bar.flowX - std::sin(phase * Pi) * bar.channelMeander * 0.45f;
    const float q           = std::sqrt((endDistance * endDistance) / (bar.notchHalfWidth * bar.notchHalfWidth) +
                                        (across * across) / (bar.notchHalfWidth * bar.notchHalfWidth) +
                                        (dy * dy) / (bar.notchHalfHeight * bar.notchHalfHeight));
    return (q - 1.f) * std::min(bar.notchHalfWidth, bar.notchHalfHeight);
}

}  // namespace

CaveSedimentSet createCaveSediment(const std::vector<CaveSedimentPathPoint>& path, int barCount, float strength,
                                   float paragenesis, uint32_t seed) {
    CaveSedimentSet result;
    if (strength <= 0.f || barCount <= 0 || path.size() < 2) return result;

    std::mt19937                          rng(seed ^ 0x6c8e9cf5u);
    std::uniform_real_distribution<float> unit(0.f, 1.f);
    const float                           boundedStrength = std::clamp(strength, 0.f, 1.f);
    result.paragenesisStrength                            = std::clamp(paragenesis, 0.f, 1.f);
    float totalPitch                                      = 0.f;
    float totalChannelWidth                               = 0.f;
    float totalPalaeofillRatio                            = 0.f;
    float totalNotchThickness                             = 0.f;
    result.bars.reserve(size_t(barCount));
    for (int i = 0; i < barCount; ++i) {
        const size_t segment = size_t(1 + rng() % uint32_t(path.size() - 1));
        const auto&  start   = path[segment - 1];
        const auto&  end     = path[segment];
        float        flowX   = end.x - start.x;
        float        flowZ   = end.z - start.z;
        const float  length  = std::sqrt(flowX * flowX + flowZ * flowZ);
        if (length < 1e-5f) {
            flowX = 1.f;
            flowZ = 0.f;
        } else {
            flowX /= length;
            flowZ /= length;
        }
        const float     t      = 0.3f + unit(rng) * 0.4f;
        const float     radius = start.radius + (end.radius - start.radius) * t;
        CaveSedimentBar bar;
        bar.x               = start.x + (end.x - start.x) * t + (-flowZ) * (unit(rng) * 2.f - 1.f) * radius * 0.22f;
        bar.z               = start.z + (end.z - start.z) * t + flowX * (unit(rng) * 2.f - 1.f) * radius * 0.22f;
        bar.flowX           = flowX;
        bar.flowZ           = flowZ;
        bar.length          = radius * (0.75f + unit(rng) * 0.55f);
        bar.width           = radius * (0.32f + unit(rng) * 0.24f);
        bar.thickness       = radius * (0.08f + boundedStrength * (0.05f + unit(rng) * 0.035f));
        const float centerY = start.y + (end.y - start.y) * t;
        const float floorY  = centerY - radius * 0.92f;
        bar.y               = floorY + bar.thickness * 0.42f;
        bar.ceilingY        = centerY + radius * 0.84f;
        bar.passageRadius   = radius;
        if (result.paragenesisStrength > 0.f) {
            // Cooper & Covington (2020): equilibrium channel width grows with discharge but weakly narrows
            // with sediment supply. Passage radius is the deterministic discharge proxy used by this recipe.
            bar.channelHalfLength = std::max(bar.length * 1.8f, length * 0.42f);
            bar.channelHalfWidth  = radius * (0.22f + 0.12f * (1.f - boundedStrength));
            bar.channelLift       = radius * result.paragenesisStrength * (0.07f + 0.11f * boundedStrength);
            bar.channelMeander    = bar.channelHalfWidth * (0.18f + unit(rng) * 0.22f);
            bar.palaeofillY       = centerY + radius * (0.22f + 0.42f * boundedStrength);
            bar.notchHalfLength   = std::max(bar.length * 1.55f, length * 0.36f);
            bar.notchHalfWidth    = radius * (1.02f + 0.1f * result.paragenesisStrength);
            // Keep the concave notch profile resolvable by the recipe's normal 2x isosurface sampling.
            bar.notchHalfHeight = radius * result.paragenesisStrength * (0.12f + 0.08f * boundedStrength);
            ++result.parageneticChannels;
            result.maximumCeilingLift  = std::max(result.maximumCeilingLift, bar.channelLift);
            result.maximumNotchRetreat = std::max(result.maximumNotchRetreat, bar.notchHalfWidth - bar.passageRadius);
            totalChannelWidth += bar.channelHalfWidth * 2.f;
            totalPalaeofillRatio += (bar.palaeofillY - floorY) / (bar.ceilingY - floorY);
            totalNotchThickness += bar.notchHalfHeight * 2.f;
        }

        const int clastCount = 3 + int(rng() % 4u);
        bar.clasts.reserve(size_t(clastCount));
        for (int clastIndex = 0; clastIndex < clastCount; ++clastIndex) {
            CaveSedimentClast clast;
            clast.flowX              = flowX;
            clast.flowZ              = flowZ;
            clast.longRadius         = radius * (0.09f + unit(rng) * 0.06f);
            clast.shortRadius        = clast.longRadius * (0.48f + unit(rng) * 0.18f);
            clast.thickRadius        = clast.longRadius * (0.24f + unit(rng) * 0.12f);
            const float along        = (unit(rng) * 2.f - 1.f) * bar.length * 0.72f;
            const float across       = (unit(rng) * 2.f - 1.f) * bar.width * 0.68f;
            clast.x                  = bar.x + flowX * along - flowZ * across;
            clast.z                  = bar.z + flowZ * along + flowX * across;
            const float pitchDegrees = 9.f + boundedStrength * (8.f + unit(rng) * 11.f);
            clast.pitch              = -pitchDegrees * Pi / 180.f;
            clast.y                  = bar.y + bar.thickness * 0.72f + clast.thickRadius * 0.45f;
            totalPitch += pitchDegrees;
            result.depositedVolume += 4.f / 3.f * Pi * clast.longRadius * clast.thickRadius * clast.shortRadius;
            ++result.clastCount;
            bar.clasts.push_back(clast);
        }
        result.depositedVolume += 4.f / 3.f * Pi * bar.length * bar.width * bar.thickness;
        result.bars.push_back(std::move(bar));
    }
    if (result.clastCount > 0) result.meanImbricationDegrees = totalPitch / float(result.clastCount);
    if (result.parageneticChannels > 0)
        result.meanParageneticWidth = totalChannelWidth / float(result.parageneticChannels);
    if (result.parageneticChannels > 0)
        result.meanPalaeofillRatio = totalPalaeofillRatio / float(result.parageneticChannels);
    if (result.parageneticChannels > 0)
        result.meanNotchThickness = totalNotchThickness / float(result.parageneticChannels);
    return result;
}

float carveCaveParagenesis(float x, float y, float z, float current, const CaveSedimentSet& sediment) {
    if (sediment.paragenesisStrength <= 0.f) return current;
    for (const CaveSedimentBar& bar : sediment.bars)
        current = std::min({current, parageneticChannelDistance(x, y, z, bar, sediment.paragenesisStrength),
                            alluvialNotchDistance(x, y, z, bar)});
    return current;
}

float addCaveSediment(float x, float y, float z, float current, const CaveSedimentSet& sediment) {
    for (const CaveSedimentBar& bar : sediment.bars) {
        current = std::max(current, -barDistance(x, y, z, bar));
        for (const CaveSedimentClast& clast : bar.clasts) current = std::max(current, -clastDistance(x, y, z, clast));
    }
    return current;
}

bool isCaveSedimentSurface(float x, float y, float z, float tolerance, const CaveSedimentSet& sediment) {
    for (const CaveSedimentBar& bar : sediment.bars) {
        if (std::fabs(barDistance(x, y, z, bar)) <= tolerance) return true;
        for (const CaveSedimentClast& clast : bar.clasts)
            if (std::fabs(clastDistance(x, y, z, clast)) <= tolerance) return true;
    }
    return false;
}

}  // namespace eve::procgen
