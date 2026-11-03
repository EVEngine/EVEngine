#pragma once

#include "procgen/MeshBuild.h"
#include "procgen/Params.h"

#include <string>

namespace eve::procgen {

/**
 * @brief Build a deterministic stylized water-splash mesh of nested flared crowns.
 *
 * Registered as the `mesh.water_splash` recipe. Layers share one vertical axis:
 * a tall central spire plus outward-flaring crown shells with jagged top rims.
 * Foam tips are encoded as white vertex colors; the body uses cyan.
 *
 * @param params Recipe parameters (seed, height, width, layers, jaggedness, …).
 * @param out Destination mesh; cleared before generation.
 * @param error Filled when generation fails.
 * @return True when `out` holds a non-empty mesh.
 * @thread Main / job thread that owns `out`; no shared mutable state.
 */
EVENGINE_API_DOMAINS bool generateWaterSplashMesh(const Params &params, MeshBuild &out, std::string &error);

}  // namespace eve::procgen
