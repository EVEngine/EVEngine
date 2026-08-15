#include "procgen/heightmap/TerrainSampler.h"

#include "procgen/Semantic.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen {

void TerrainSampler::setSeed(uint32_t seed) { field_.seed = seed; }
uint32_t TerrainSampler::getSeed() const { return field_.seed; }
void TerrainSampler::setScale(float scale) { frequency_ = std::max(0.0001f, scale); }
float TerrainSampler::getScale() const { return frequency_; }
void TerrainSampler::setFrequency(float frequency) { setScale(frequency); }
float TerrainSampler::getFrequency() const { return frequency_; }
void TerrainSampler::setWavelength(float wavelength) {
    setFrequency(1.f / std::max(0.001f, wavelength));
}
float TerrainSampler::getWavelength() const { return 1.f / frequency_; }
void TerrainSampler::setOctaves(int octaves) { octaves_ = std::max(1, octaves); }
int TerrainSampler::getOctaves() const { return octaves_; }
void TerrainSampler::setLacunarity(float lacunarity) { lacunarity_ = std::max(0.1f, lacunarity); }
float TerrainSampler::getLacunarity() const { return lacunarity_; }
void TerrainSampler::setGain(float gain) { gain_ = std::max(0.01f, gain); }
float TerrainSampler::getGain() const { return gain_; }
void TerrainSampler::setRidge(float ridge) { ridge_ = std::clamp(ridge, 0.f, 1.f); }
float TerrainSampler::getRidge() const { return ridge_; }
void TerrainSampler::setWarp(float warp) { warp_ = std::max(0.f, warp); }
float TerrainSampler::getWarp() const { return warp_; }
void TerrainSampler::setExponent(float exponent) { exponent_ = std::max(0.1f, exponent); }
float TerrainSampler::getExponent() const { return exponent_; }
void TerrainSampler::setContinent(float continent) { continent_ = std::clamp(continent, 0.f, 1.f); }
float TerrainSampler::getContinent() const { return continent_; }
void TerrainSampler::setIsland(float island) { island_ = std::clamp(island, 0.f, 1.f); }
float TerrainSampler::getIsland() const { return island_; }
void TerrainSampler::setCoastSoftness(float softness) { coastSoft_ = std::clamp(softness, 0.02f, 0.4f); }
float TerrainSampler::getCoastSoftness() const { return coastSoft_; }
void TerrainSampler::setWorldSize(int width, int height) {
    worldW_ = width > 0 ? width : 0;
    worldH_ = height > 0 ? height : 0;
}
int TerrainSampler::getWorldWidth() const { return worldW_; }
int TerrainSampler::getWorldHeight() const { return worldH_; }
void TerrainSampler::setBase(float base) { base_ = base; }
float TerrainSampler::getBase() const { return base_; }
void TerrainSampler::setAmplitude(float amplitude) { amplitude_ = std::max(0.f, amplitude); }
float TerrainSampler::getAmplitude() const { return amplitude_; }
void TerrainSampler::setClamp(bool enabled, float minHeight, float maxHeight) {
    clamp_    = enabled;
    clampMin_ = std::min(minHeight, maxHeight);
    clampMax_ = std::max(minHeight, maxHeight);
}
bool  TerrainSampler::isClamped() const { return clamp_; }
float TerrainSampler::getClampMin() const { return clampMin_; }
float TerrainSampler::getClampMax() const { return clampMax_; }

namespace {
float smoother(float edge0, float edge1, float x) {
    if (edge1 <= edge0) return x < edge0 ? 0.f : 1.f;
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
}  // namespace

float TerrainSampler::sample(float x, float y) const {
    const float mx = x * frequency_;
    const float my = y * frequency_;

    // Continent: 3 low/mid octaves (~100 / 50 / 25 px bays on a 512 map).
    // Warp at the same scale so the silhouette twists without pixel breakup.
    float cx = mx * 0.36f;
    float cy = my * 0.36f;
    const float maskWarp = 0.70f;
    cx += (field_.perlinNoise(mx * 0.18f + 2.4f, my * 0.18f + 9.1f) - 0.5f) * maskWarp;
    cy += (field_.perlinNoise(mx * 0.18f + 7.6f, my * 0.18f + 1.3f) - 0.5f) * maskWarp;
    float macro = field_.fbmPerlin(cx, cy, 3, 2.f, 0.42f);

    if (island_ > 0.f && worldW_ > 0 && worldH_ > 0) {
        const float nx = x / float(worldW_) * 2.f - 1.f;
        const float ny = y / float(worldH_) * 2.f - 1.f;
        const float dSq = 1.f - (1.f - nx * nx) * (1.f - ny * ny);
        const float dEu = std::sqrt(std::min(1.f, nx * nx + ny * ny));
        const float d   = std::clamp(dSq * 0.5f + dEu * 0.5f, 0.f, 1.f);
        // Multiply, don't lerp: corners sink, the noise silhouette stays.
        macro *= 1.f - island_ * std::pow(d, 1.25f);
    }

    // Medium-scale shore wiggle (~70 px). Amplitude stays below the smoothstep
    // width so it cannot punch isolated pixels through the waterline.
    const float wiggle =
        (field_.perlinNoise(mx * 0.42f + 11.f, my * 0.42f + 4.f) - 0.5f) * coastSoft_ * 0.55f;
    const float sea    = 0.54f - continent_ * 0.14f + wiggle;
    const float land   = smoother(sea - coastSoft_ * 0.45f, sea + coastSoft_ * 0.55f, macro);
    const float inland = land * land;

    float dx = mx;
    float dy = my;
    if (warp_ > 0.f) {
        dx += (field_.fbmPerlin(mx + 19.1f, my + 7.3f, 2) - 0.5f) * warp_;
        dy += (field_.fbmPerlin(mx + 5.2f, my + 31.7f, 2) - 0.5f) * warp_;
    }

    float detail = field_.fbmPerlin(dx, dy, octaves_, lacunarity_, gain_);
    if (ridge_ > 0.f) {
        const float mountains =
            field_.ridgedPerlin(dx * 1.4f, dy * 1.4f, octaves_, lacunarity_, gain_);
        detail = detail + (mountains - detail) * ridge_;
    }
    detail = std::pow(std::clamp(detail, 0.f, 1.f), exponent_);

    // land=0 stays below waterMax (0.25). Detail cannot open holes in the sea.
    const float shelf = 0.06f + land * 0.32f;
    const float e     = shelf + inland * detail * 0.62f;

    float h = base_ + e * amplitude_;
    if (clamp_) h = std::clamp(h, clampMin_, clampMax_);
    return h;
}

float TerrainSampler::sampleTile(int tileX, int tileY) const {
    return sample(float(tileX) + 0.5f, float(tileY) + 0.5f);
}

std::function<float(float, float)> TerrainSampler::asFunction() const {
    return [self = *this](float x, float y) { return self.sample(x, y); };
}

TerrainSampler TerrainSampler::fromParams(const Params &params) {
    TerrainSampler s;
    s.setSeed(params.getSeed());
    s.setWorldSize(params.getWidth(), params.getHeight());

    const float wavelength = params.getFloat("wavelength", 0.f);
    if (wavelength > 0.f) {
        s.setWavelength(wavelength);
    } else if (params.has("frequency")) {
        s.setFrequency(params.getFloat("frequency", 1.f / 32.f));
    } else {
        s.setScale(params.getFloat("scale", 1.f / 32.f));
    }

    s.setOctaves(params.getInt("octaves", 5));
    s.setLacunarity(params.getFloat("lacunarity", 2.f));
    s.setGain(params.getFloat("gain", 0.5f));
    s.setRidge(params.getFloat("ridge", 0.35f));
    s.setWarp(params.getFloat("warp", 0.35f));
    s.setExponent(params.getFloat("exponent", 2.f));
    s.setContinent(params.getFloat("continent", 0.55f));
    s.setIsland(params.getFloat("island", 0.38f));
    s.setCoastSoftness(params.getFloat("coast", 0.12f));
    s.setBase(params.getFloat("base", 0.f));
    s.setAmplitude(params.getFloat("amplitude", 1.f));
    const float lo = params.getFloat("heightMin", 0.f);
    const float hi = params.getFloat("heightMax", 1.f);
    s.setClamp(params.getInt("clamp", 1) != 0, lo, hi);
    return s;
}

uint32_t TerrainBands::semanticAt(float height) const {
    if (height < waterMax) return Semantic::Water;
    if (height < sandMax) return Semantic::Sand;
    if (height < grassMax) return Semantic::Grass;
    if (height < dirtMax) return Semantic::Dirt;
    if (height < stoneMax) return Semantic::Stone;
    return Semantic::Snow;
}

TerrainBands TerrainBands::fromParams(const Params &params) {
    TerrainBands bands;
    bands.waterMax = std::clamp(params.getFloat("waterMax", 0.25f), 0.f, 1.f);
    bands.sandMax  = std::clamp(params.getFloat("sandMax", 0.35f), 0.f, 1.f);
    bands.grassMax = std::clamp(params.getFloat("grassMax", 0.65f), 0.f, 1.f);
    bands.dirtMax  = std::clamp(params.getFloat("dirtMax", 0.80f), 0.f, 1.f);
    bands.stoneMax = std::clamp(params.getFloat("stoneMax", 0.92f), 0.f, 1.f);
    return bands;
}

}  // namespace eve::procgen
