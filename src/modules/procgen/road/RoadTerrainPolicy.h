#pragma once
namespace eve::procgen::road {
/** @brief Owning per-road terrain author policy; copied with styles, presets and history.
 * Owner-thread value with no callbacks or external references. Execution budgets belong to the write request.
 * Disabled policies preserve every channel. Missing historical policy fields use these defaults.
 */
struct RoadTerrainPolicy {
    bool  enabled       = true;
    float blendDistance = 2.f, maxVerticalDelta = 2.f;
    int   splatLayer   = -1;
    float splatOpacity = 1.f;
    bool  clearDetails = true, clearTrees = true, cutTunnelHoles = false;
    bool  tunnelHolesAtPortalsOnly = true;
};
}  // namespace eve::procgen::road
