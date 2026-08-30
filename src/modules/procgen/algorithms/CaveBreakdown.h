#pragma once

#include <cstdint>
#include <vector>

namespace eve::procgen {

struct CaveBreakdownChamber {
    float x = 0.f, y = 0.f, z = 0.f;
    float rx = 0.2f, ry = 0.15f, rz = 0.2f;
};

struct CaveBreakdownBlock {
    float x = 0.f, y = 0.f, z = 0.f;
    float hx = 0.04f, hy = 0.025f, hz = 0.04f;
    float yaw = 0.f;
};

struct CaveBreakdownEvent {
    float                           x = 0.f, ceilingY = 0.f, z = 0.f;
    float                           scarX = 0.1f, scarY = 0.04f, scarZ = 0.1f;
    float                           yaw = 0.f;
    std::vector<CaveBreakdownBlock> blocks;
};

struct CaveBreakdownSet {
    std::vector<CaveBreakdownEvent> events;
    int                             blockCount      = 0;
    float                           detachedVolume  = 0.f;
    float                           depositedVolume = 0.f;
};

/**
 * @brief Create paired ceiling-spall scars and floor blocks from host chambers.
 * @param chambers Candidate chambers supplying ceiling and floor geometry.
 * @param eventCount Maximum number of deterministic breakdown events.
 * @param strength Normalized chemical-mechanical weakening intensity.
 * @param seed Deterministic generator seed.
 * @return Events and approximate detached/deposited volume statistics.
 */
CaveBreakdownSet createCaveBreakdown(const std::vector<CaveBreakdownChamber>& chambers, int eventCount, float strength,
                                     uint32_t seed);

/** @brief Carve the event's shallow ceiling detachment scars from a cave SDF. */
float carveCaveBreakdownScars(float x, float y, float z, float current, const CaveBreakdownSet& breakdown);

/** @brief Union the event's landed breakdown blocks into a cave SDF. */
float addCaveBreakdownBlocks(float x, float y, float z, float current, const CaveBreakdownSet& breakdown);

/** @brief Test whether a point lies on a generated breakdown block surface. */
bool isCaveBreakdownBlockSurface(float x, float y, float z, float tolerance, const CaveBreakdownSet& breakdown);

}  // namespace eve::procgen
