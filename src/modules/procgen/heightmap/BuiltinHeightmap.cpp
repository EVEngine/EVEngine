#include "procgen/GeneratorRegistry.h"
#include "procgen/Semantic.h"
#include "procgen/heightmap/Heightmap.h"

#include <string>

namespace eve::procgen {
namespace {

bool genTerrainHeightmap(const Params &params, Grid2D &out, std::string &error) {
    const int w = params.getWidth();
    const int h = params.getHeight();
    if (w < 1 || h < 1) {
        error = "terrain.heightmap: width/height must be positive";
        return false;
    }
    const TerrainSampler sampler = TerrainSampler::fromParams(params);
    const TerrainBands   bands   = TerrainBands::fromParams(params);

    const Heightmap hm = Heightmap::generate(sampler, w, h);
    if (!hm.toGrid(out, bands)) {
        error = "terrain.heightmap: failed to classify heightmap";
        return false;
    }

    out.setMeta("algorithm", "terrain.heightmap");
    out.setMeta("scale", std::to_string(sampler.getScale()));
    out.setMeta("octaves", std::to_string(sampler.getOctaves()));
    return true;
}

}  // namespace

void registerTerrainHeightmapAlgorithm(GeneratorRegistry &registry) {
    registry.registerAlgorithm("terrain.heightmap", genTerrainHeightmap);
}

}  // namespace eve::procgen
