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
    auto descriptor = GeneratorDescriptor::grid("terrain.heightmap", "Terrain Heightmap", "Terrain", 1, 1);
    descriptor.params.push_back(ParamDescriptor::floating("scale", "Noise Scale", 1.f / 32.f, 0.0001f, 1.f,
                                                          0.001f));
    descriptor.params.push_back(ParamDescriptor::integer("octaves", "Octaves", 5, 1, 16));
    descriptor.params.push_back(ParamDescriptor::floating("lacunarity", "Lacunarity", 2.f, 1.f, 8.f, 0.05f));
    descriptor.params.push_back(ParamDescriptor::floating("gain", "Gain", 0.5f, 0.f, 1.f, 0.01f));
    descriptor.params.push_back(ParamDescriptor::floating("ridge", "Ridge", 0.35f, 0.f, 1.f, 0.01f));
    descriptor.params.push_back(ParamDescriptor::floating("warp", "Warp", 0.35f, 0.f, 4.f, 0.01f));
    descriptor.params.push_back(ParamDescriptor::floating("exponent", "Exponent", 2.f, 0.1f, 8.f, 0.05f));
    descriptor.params.push_back(ParamDescriptor::floating("continent", "Continent", 0.55f, 0.f, 1.f, 0.01f));
    descriptor.params.push_back(ParamDescriptor::floating("island", "Island", 0.38f, 0.f, 1.f, 0.01f));
    descriptor.params.push_back(ParamDescriptor::floating("coast", "Coast Softness", 0.12f, 0.f, 1.f, 0.01f));
    descriptor.params.push_back(ParamDescriptor::floating("base", "Base Height", 0.f, -10.f, 10.f, 0.01f));
    descriptor.params.push_back(ParamDescriptor::floating("amplitude", "Amplitude", 1.f, 0.f, 1000.f, 0.1f));
    descriptor.params.push_back(ParamDescriptor::boolean("clamp", "Clamp Height", true));
    descriptor.params.push_back(ParamDescriptor::floating("heightMin", "Minimum Height", 0.f, -1000.f, 1000.f,
                                                          0.1f));
    descriptor.params.push_back(ParamDescriptor::floating("heightMax", "Maximum Height", 1.f, -1000.f, 1000.f,
                                                          0.1f));
    registry.registerAlgorithm(std::move(descriptor), genTerrainHeightmap);
}

}  // namespace eve::procgen
