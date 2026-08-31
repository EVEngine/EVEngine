#pragma once

#include <cstdint>
#include <vector>

namespace eve::procgen {

struct CaveSedimentPathPoint {
    float x = 0.f, y = 0.f, z = 0.f;
    float radius = 0.16f;
};

struct CaveSedimentClast {
    float x = 0.f, y = 0.f, z = 0.f;
    float flowX = 1.f, flowZ = 0.f;
    float longRadius = 0.035f, thickRadius = 0.012f, shortRadius = 0.022f;
    float pitch = 0.f;
};

struct CaveSedimentBar {
    float                          x = 0.f, y = 0.f, z = 0.f;
    float                          ceilingY = 0.f;
    float                          flowX = 1.f, flowZ = 0.f;
    float                          length = 0.18f, width = 0.09f, thickness = 0.025f;
    float                          channelHalfLength = 0.f, channelHalfWidth = 0.f;
    float                          channelLift = 0.f, channelMeander = 0.f;
    float                          passageRadius = 0.f, palaeofillY = 0.f;
    float                          notchHalfLength = 0.f, notchHalfWidth = 0.f, notchHalfHeight = 0.f;
    std::vector<CaveSedimentClast> clasts;
};

struct CaveSedimentSet {
    std::vector<CaveSedimentBar> bars;
    int                          clastCount             = 0;
    float                        depositedVolume        = 0.f;
    float                        meanImbricationDegrees = 0.f;
    float                        paragenesisStrength    = 0.f;
    int                          parageneticChannels    = 0;
    float                        maximumCeilingLift     = 0.f;
    float                        meanParageneticWidth   = 0.f;
    float                        meanPalaeofillRatio    = 0.f;
    float                        maximumNotchRetreat    = 0.f;
    float                        meanNotchThickness     = 0.f;
};

/**
 * @brief Create flow-aligned cave sediment bars with upstream-imbricated gravel.
 * @param path Passage centerline carrying local radius and paleoflow direction.
 * @param barCount Maximum number of deterministic longitudinal bars.
 * @param strength Normalized transported sediment supply.
 * @param paragenesis Normalized sediment-constrained upward dissolution strength.
 * @param seed Deterministic generator seed.
 * @return Bars, clasts, and observable deposition statistics.
 */
CaveSedimentSet createCaveSediment(const std::vector<CaveSedimentPathPoint>& path, int barCount, float strength,
                                   float paragenesis, uint32_t seed);

/** @brief Carve flow-aligned antigravitative ceiling channels above generated sediment bars. */
float carveCaveParagenesis(float x, float y, float z, float current, const CaveSedimentSet& sediment);

/** @brief Union sediment bars and imbricated clasts into a cave SDF. */
float addCaveSediment(float x, float y, float z, float current, const CaveSedimentSet& sediment);

/** @brief Test whether a point lies on a generated sediment surface. */
bool isCaveSedimentSurface(float x, float y, float z, float tolerance, const CaveSedimentSet& sediment);

}  // namespace eve::procgen
