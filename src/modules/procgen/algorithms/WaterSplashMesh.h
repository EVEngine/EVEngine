#pragma once

#include "procgen/MeshBuild.h"
#include "procgen/Params.h"

#include <string>

namespace eve::procgen {

/**
 * @brief Build deterministic clay-sheet crowns for a stylized water splash.
 *
 * Registered as `mesh.water_splash`. Layers share one vertical axis: a tall
 * central spire plus nested outward-flaring continuous sheets (UV.v = 0 at the
 * ground, 1 at the open lip). Jagged lips, foam, and semi-transparency belong
 * to the material (see `examples/water-splash-vfx/shaders/splash.frag`), not
 * the mesh.
 *
 * @param params Recipe parameters (seed, height, width, layers, flare, …).
 * @param out Destination mesh; cleared before generation.
 * @param error Filled when generation fails.
 * @return True when `out` holds a non-empty mesh.
 * @thread Main / job thread that owns `out`; no shared mutable state.
 */
EVENGINE_API_DOMAINS bool generateWaterSplashMesh(const Params &params, MeshBuild &out, std::string &error);

}  // namespace eve::procgen
