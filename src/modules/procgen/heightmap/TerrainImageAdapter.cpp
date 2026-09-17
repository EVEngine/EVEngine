#include "procgen/heightmap/TerrainImageAdapter.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include <algorithm>
#include <cmath>
#include "common/SquirrelBinding.h"
#include "image/ImageData.h"
#include "procgen/heightmap/TerrainImageMask.h"
#include "procgen/heightmap/TerrainSplatmap.h"
#include "procgen/heightmap/TerrainRasterInternal.h"
#include "procgen/heightmap/TerrainStamp.h"

namespace eve::procgen {
struct GtsPackedLayerSet::Impl {
    struct Layer {
        image::ImageData albedoHeight;
        image::ImageData normalMask;
        GtsPackedLayerSettings settings;
    };
    std::vector<Layer> layers;
};
namespace {
bool validImage(const image::ImageData& value, bool writable = false) {
    if (value.getWidth() <= 0 || value.getHeight() <= 0 || !value.getData() || !value.getPixelGetFunction() ||
        (writable && !value.getPixelSetFunction())) return false;
    const std::size_t pixelSize = value.getPixelSize();
    return pixelSize > 0 && pixelSize <= sizeof(image::ImageData::Pixel) &&
           std::size_t(value.getWidth()) * value.getHeight() <= value.getSize() / pixelSize;
}
image::ImageData::Colorf repeatBilinear(const image::ImageData& image, double u, double v) {
    u -= std::floor(u); v -= std::floor(v);
    const double px = u * image.getWidth() - 0.5, py = v * image.getHeight() - 0.5;
    const int x0 = static_cast<int>(std::floor(px)), y0 = static_cast<int>(std::floor(py));
    const float tx = static_cast<float>(px - x0), ty = static_cast<float>(py - y0);
    auto sample = [&](int x, int y) {
        x = (x % image.getWidth() + image.getWidth()) % image.getWidth();
        y = (y % image.getHeight() + image.getHeight()) % image.getHeight();
        return image.getPixel(x, y);
    };
    const auto a = sample(x0, y0), b = sample(x0 + 1, y0), c = sample(x0, y0 + 1), d = sample(x0 + 1, y0 + 1);
    auto blend = [&](float av, float bv, float cv, float dv) {
        return (av + (bv - av) * tx) * (1 - ty) + (cv + (dv - cv) * tx) * ty;
    };
    return {blend(a.r,b.r,c.r,d.r), blend(a.g,b.g,c.g,d.g), blend(a.b,b.b,c.b,d.b), blend(a.a,b.a,c.a,d.a)};
}
float smoothStep(float low, float high, float value) {
    if (low == high) return value >= high ? 1.0F : 0.0F;
    const float t = std::clamp((value - low) / (high - low), 0.0F, 1.0F);
    return t * t * (3 - 2 * t);
}
struct Vec3 { float x, y, z; };
Vec3 normalize(Vec3 value) {
    const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    return length > 0 ? Vec3{value.x / length, value.y / length, value.z / length} : Vec3{0, 1, 0};
}
Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
void terrainFrame(Vec3 terrain, Vec3& tangent, Vec3& bitangent) {
    tangent = normalize(cross(terrain, {0, 0, 1}));
    bitangent = normalize(cross(tangent, terrain));
}
Vec3 blendWorldNormal(Vec3 terrain, Vec3 surface) {
    const Vec3 tangent = cross(terrain, {0, 0, 1});
    const Vec3 bitangent = cross(tangent, terrain);
    return {surface.x * tangent.x + surface.y * bitangent.x + surface.z * terrain.x,
            surface.x * tangent.y + surface.y * bitangent.y + surface.z * terrain.y,
            surface.x * tangent.z + surface.y * bitangent.z + surface.z * terrain.z};
}
Vec3 terrainNormal(const Heightmap& heights, int x, int z, double spacingX, double spacingZ) {
    const int xl = std::max(0, x - 1), xr = std::min(heights.getWidth() - 1, x + 1);
    const int zb = std::max(0, z - 1), zf = std::min(heights.getHeight() - 1, z + 1);
    const double dx = (heights.height(xr, z) - heights.height(xl, z)) / ((xr - xl) * spacingX + (xr == xl));
    const double dz = (heights.height(x, zf) - heights.height(x, zb)) / ((zf - zb) * spacingZ + (zf == zb));
    return normalize({static_cast<float>(-dx), 1, static_cast<float>(-dz)});
}
double fraction(double value) { return value - std::floor(value); }
std::array<double, 2> gtsHash(double x, double y) {
    return {fraction(std::sin(127.1 * x + 311.7 * y) * 43758.5453),
            fraction(std::sin(269.5 * x + 183.3 * y) * 43758.5453)};
}
struct TriangleSample { float weights[3]; std::array<double, 2> vertices[3]; };
TriangleSample triangleGrid(double u, double v) {
    u *= 3.464; v *= 3.464;
    const double sx = u - 0.57735027 * v, sy = 1.15470054 * v;
    const double bx = std::floor(sx), by = std::floor(sy), fx = fraction(sx), fy = fraction(sy);
    const double third = 1 - fx - fy;
    TriangleSample result{};
    if (third > 0) {
        result.weights[0]=static_cast<float>(third);result.weights[1]=static_cast<float>(fy);
        result.weights[2]=static_cast<float>(fx);result.vertices[0]={bx,by};
        result.vertices[1]={bx,by+1};result.vertices[2]={bx+1,by};
    } else {
        result.weights[0]=static_cast<float>(-third);result.weights[1]=static_cast<float>(1-fy);
        result.weights[2]=static_cast<float>(1-fx);result.vertices[0]={bx+1,by+1};
        result.vertices[1]={bx+1,by};result.vertices[2]={bx,by+1};
    }
    return result;
}
image::ImageData::Colorf stochasticSample(const image::ImageData& image, double u, double v) {
    const auto grid = triangleGrid(u, v);
    image::ImageData::Colorf result{0, 0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        const auto offset = gtsHash(grid.vertices[i][0], grid.vertices[i][1]);
        const auto value = repeatBilinear(image, u + offset[0], v + offset[1]);
        result.r += grid.weights[i] * value.r; result.g += grid.weights[i] * value.g;
        result.b += grid.weights[i] * value.b; result.a += grid.weights[i] * value.a;
    }
    return result;
}
image::ImageData::Colorf projectedSample(const image::ImageData& image,
                                          const GtsPackedLayerSettings& settings,
                                          Vec3 position, Vec3 normal, double planarU, double planarV) {
    if (!settings.triPlanar)
        return settings.stochastic ? stochasticSample(image, planarU, planarV)
                                   : repeatBilinear(image, planarU, planarV);
    float wx = std::pow(std::abs(normal.x), 4.0F), wy = std::pow(std::abs(normal.y), 15.0F);
    float wz = std::pow(std::abs(normal.z), 4.0F), total = std::max(wx + wy + wz, 0.0001F);
    wx /= total; wy /= total; wz /= total;
    const double sizeX = settings.triPlanarSizeX * 10.0, sizeZ = settings.triPlanarSizeZ * 10.0;
    const double signX = normal.x < 0 ? -1 : 1, signY = normal.y < 0 ? -1 : 1;
    const double signZ = normal.z < 0 ? -1 : 1;
    const auto sampleX = repeatBilinear(image, position.z / sizeX * signX, position.y / sizeZ);
    const auto sampleY = repeatBilinear(image, (position.x / sizeX + 0.33) * signY,
                                         position.z / sizeZ + 0.33);
    const auto sampleZ = repeatBilinear(image, -(position.x / sizeX + 0.67) * signZ,
                                         position.y / sizeZ + 0.67);
    return {sampleX.r*wx+sampleY.r*wy+sampleZ.r*wz, sampleX.g*wx+sampleY.g*wy+sampleZ.g*wz,
            sampleX.b*wx+sampleY.b*wy+sampleZ.b*wz, sampleX.a*wx+sampleY.a*wy+sampleZ.a*wz};
}
}  // namespace
Result<int> TerrainSplatPalette::addColor(float red, float green, float blue, float alpha) {
    const auto valid = [](float value) { return std::isfinite(value) && value >= 0 && value <= 1; };
    if (!valid(red) || !valid(green) || !valid(blue) || !valid(alpha))
        return raster_detail::invalid("terrain.splatPalette: normalized finite color required");
    colors_.push_back({red, green, blue, alpha});
    return Result<int>::success(static_cast<int>(colors_.size()));
}
GtsPackedLayerSet::GtsPackedLayerSet() : impl_(std::make_unique<Impl>()) {}
GtsPackedLayerSet::~GtsPackedLayerSet() = default;
GtsPackedLayerSet::GtsPackedLayerSet(GtsPackedLayerSet&&) noexcept = default;
GtsPackedLayerSet& GtsPackedLayerSet::operator=(GtsPackedLayerSet&&) noexcept = default;
Result<int> GtsPackedLayerSet::addLayer(const image::ImageData& albedoHeight,
                                        const image::ImageData& normalMask,
                                        const GtsPackedLayerSettings& settings) {
    using namespace raster_detail;
    const float values[] = {settings.tileSizeX, settings.tileSizeZ, settings.offsetX, settings.offsetZ,
                            settings.triPlanarSizeX, settings.triPlanarSizeZ,
                            settings.tintR, settings.tintG, settings.tintB, settings.normalStrength,
                            settings.aoMin, settings.smoothnessMin, settings.aoMax, settings.smoothnessMax,
                            settings.geoAmount, settings.detailAmount, settings.heightContrast,
                            settings.heightBrightness, settings.heightIncrease, settings.displacementContrast,
                            settings.displacementBrightness, settings.displacementIncrease,
                            settings.tessellationAmount};
    if (!validImage(albedoHeight) || !validImage(normalMask) ||
        !std::all_of(std::begin(values), std::end(values), [](float v) { return std::isfinite(v); }) ||
        settings.tileSizeX <= 0 || settings.tileSizeZ <= 0 || settings.triPlanarSizeX <= 0 ||
        settings.triPlanarSizeZ <= 0 || settings.normalStrength < 0 ||
        settings.tintR < 0 || settings.tintR > 1 || settings.tintG < 0 || settings.tintG > 1 ||
        settings.tintB < 0 || settings.tintB > 1 || settings.geoAmount < 0 || settings.geoAmount > 1 ||
        settings.detailAmount < 0 || settings.detailAmount > 1 || settings.heightContrast < 0 ||
        settings.displacementContrast < 0)
        return invalid("terrain.gtsPackedLayers: readable textures and finite normalized settings required");
    if (!impl_) impl_ = std::make_unique<Impl>();
    if (impl_->layers.size() >= 8) return invalid("terrain.gtsPackedLayers: at most eight layers supported");
    impl_->layers.push_back({albedoHeight, normalMask, settings});
    return Result<int>::success(static_cast<int>(impl_->layers.size()));
}

Result<int> bakeGtsPackedLayerDisplacement(Heightmap& displacement, Heightmap& tessellation,
                                            const Heightmap& heights, const TerrainSplatmap& splatmap,
                                            const GtsPackedLayerSet& layers, double cameraX, double cameraY,
                                            double cameraZ, double tessellationMultiplier, double originX,
                                            double originY, double originZ, double spacingX, double spacingZ) {
    using namespace raster_detail;
    const int width = splatmap.getWidth(), height = splatmap.getHeight(), count = splatmap.getLayerCount();
    auto matches = [&](const Heightmap& map) {
        return validRaster(map) && map.getWidth() == width && map.getHeight() == height;
    };
    const double values[]{cameraX, cameraY, cameraZ, tessellationMultiplier, originX, originY, originZ,
                          spacingX, spacingZ};
    if (!layers.impl_ || layers.impl_->layers.size() != static_cast<std::size_t>(count) || count < 1 ||
        count > 8 || !matches(displacement) || !matches(tessellation) || !matches(heights) ||
        !std::all_of(std::begin(values), std::end(values), [](double value) { return std::isfinite(value); }) ||
        spacingX <= 0 || spacingZ <= 0)
        return invalid("terrain.gtsLayerDisplacement: matching splat, layers, heights and finite settings required");
    Heightmap nextDisplacement(displacement), nextTessellation(tessellation);
    int changed = 0;
    for (int z = 0; z < height; ++z) for (int x = 0; x < width; ++x) {
        const double worldX = originX + x * spacingX, worldY = originY + heights.height(x, z);
        const double worldZ = originZ + z * spacingZ;
        const double dx = cameraX - worldX, dy = cameraY - worldY, dz = cameraZ - worldZ;
        float displaced = 0, tessellated = 0;
        if (dx * dx + dy * dy + dz * dz < 100000.0) {
            std::array<std::pair<float, int>, 8> ranked{};
            for (int layer = 0; layer < count; ++layer)
                ranked[static_cast<std::size_t>(layer)] = {splatmap.sample(layer, x, z).value(), layer};
            std::stable_sort(ranked.begin(), ranked.begin() + count,
                             [](const auto& a, const auto& b) { return a.first > b.first; });
            for (int rank = 0; rank < std::min(4, count); ++rank) {
                const auto [weight, layer] = ranked[static_cast<std::size_t>(rank)];
                if (weight <= 0) continue;
                const auto& source = layers.impl_->layers[static_cast<std::size_t>(layer)];
                const auto& settings = source.settings;
                const double u = x * spacingX / settings.tileSizeX + settings.offsetX;
                const double v = z * spacingZ / settings.tileSizeZ + settings.offsetZ;
                const Vec3 position{static_cast<float>(worldX), static_cast<float>(worldY),
                                    static_cast<float>(worldZ)};
                const Vec3 normal = terrainNormal(heights, x, z, spacingX, spacingZ);
                const float rawHeight = projectedSample(source.albedoHeight, settings, position, normal, u, v).a;
                const float layerDisplacement = std::pow(std::abs(rawHeight), settings.displacementContrast) *
                                                    settings.displacementBrightness +
                                                settings.displacementIncrease;
                displaced += layerDisplacement * weight;
                tessellated += settings.tessellationAmount * weight;
            }
        }
        tessellated *= static_cast<float>(tessellationMultiplier);
        if (!std::isfinite(displaced) || !std::isfinite(tessellated))
            return invalid("terrain.gtsLayerDisplacement: profile produced a nonfinite output");
        changed += displacement.height(x, z) != displaced;
        changed += tessellation.height(x, z) != tessellated;
        nextDisplacement.setHeight(x, z, displaced);
        nextTessellation.setHeight(x, z, tessellated);
    }
    displacement.data().swap(nextDisplacement.data());
    tessellation.data().swap(nextTessellation.data());
    return Result<int>::success(changed);
}
int GtsPackedLayerSet::getLayerCount() const noexcept { return impl_ ? static_cast<int>(impl_->layers.size()) : 0; }

Result<int> bakeGtsPackedLayers(image::ImageData& albedo, image::ImageData& packedNormal,
                                Heightmap& geoStrength, Heightmap& detailStrength,
                                const Heightmap& heights, const TerrainSplatmap& splatmap,
                                const GtsPackedLayerSet& layers, double originX, double originY,
                                double originZ, double spacingX, double spacingZ) {
    using namespace raster_detail;
    const int width = splatmap.getWidth(), height = splatmap.getHeight(), count = splatmap.getLayerCount();
    auto rasterMatches = [&](const Heightmap& map) { return validRaster(map) && map.getWidth() == width && map.getHeight() == height; };
    auto imageMatches = [&](const image::ImageData& image) { return validImage(image, true) && image.getWidth() == width && image.getHeight() == height; };
    if (!layers.impl_ || layers.impl_->layers.size() != static_cast<std::size_t>(count) || count < 1 || count > 8 ||
        !imageMatches(albedo) || !imageMatches(packedNormal) || !rasterMatches(geoStrength) ||
        !rasterMatches(detailStrength) || !rasterMatches(heights) || !std::isfinite(originX) ||
        !std::isfinite(originY) || !std::isfinite(originZ) ||
        !std::isfinite(spacingX) || spacingX <= 0 || !std::isfinite(spacingZ) || spacingZ <= 0)
        return invalid("terrain.gtsPackedLayers: matching splat, layers and outputs required");
    image::ImageData nextAlbedo(albedo), nextNormal(packedNormal);
    Heightmap nextGeo(geoStrength), nextDetail(detailStrength);
    int changed = 0;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        float r=0,g=0,b=0,nx=0,ny=0,ao=0,smooth=0,geo=0,detail=0;
        const Vec3 position{static_cast<float>(originX + x * spacingX),
                            static_cast<float>(originY + heights.height(x, y)),
                            static_cast<float>(originZ + y * spacingZ)};
        const Vec3 surfaceNormal = terrainNormal(heights, x, y, spacingX, spacingZ);
        for (int layer = 0; layer < count; ++layer) {
            const float weight = splatmap.sample(layer, x, y).value();
            const auto& source = layers.impl_->layers[static_cast<std::size_t>(layer)];
            const auto& s = source.settings;
            const double u = x * spacingX / s.tileSizeX + s.offsetX;
            const double v = y * spacingZ / s.tileSizeZ + s.offsetZ;
            const auto color = projectedSample(source.albedoHeight, s, position, surfaceNormal, u, v);
            const auto normal = projectedSample(source.normalMask, s, position, surfaceNormal, u, v);
            r += color.r*s.tintR*weight; g += color.g*s.tintG*weight; b += color.b*s.tintB*weight;
            nx += (normal.r*2-1)*s.normalStrength*weight; ny += (normal.g*2-1)*s.normalStrength*weight;
            ao += (s.aoMin+(s.aoMax-s.aoMin)*normal.b)*weight;
            smooth += (s.smoothnessMin+(s.smoothnessMax-s.smoothnessMin)*normal.a)*weight;
            geo += s.geoAmount*weight; detail += s.detailAmount*weight;
        }
        const float nz = std::sqrt(1-std::clamp(nx*nx+ny*ny,0.0F,1.0F));
        const float len = std::sqrt(nx*nx+ny*ny+nz*nz); nx/=len; ny/=len;
        const auto oldA=albedo.getPixel(x,y), oldN=packedNormal.getPixel(x,y);
        image::ImageData::Colorf outA{r,g,b,1}, outN{nx*0.5F+0.5F,ny*0.5F+0.5F,ao,smooth};
        changed += oldA.r!=r||oldA.g!=g||oldA.b!=b; changed += oldN.r!=outN.r||oldN.g!=outN.g||oldN.b!=ao||oldN.a!=smooth;
        changed += geoStrength.height(x,y)!=geo; changed += detailStrength.height(x,y)!=detail;
        nextAlbedo.setPixel(x,y,outA); nextNormal.setPixel(x,y,outN); nextGeo.setHeight(x,y,geo); nextDetail.setHeight(x,y,detail);
    }
    albedo.adopt(nextAlbedo); packedNormal.adopt(nextNormal); geoStrength.data().swap(nextGeo.data()); detailStrength.data().swap(nextDetail.data());
    return Result<int>::success(changed);
}

Result<int> bakeTerrainSplatAlbedo(image::ImageData& output, const TerrainSplatmap& splatmap,
                                   const TerrainSplatPalette& palette) {
    if (splatmap.getWidth() <= 0 || splatmap.getHeight() <= 0 ||
        output.getWidth() != splatmap.getWidth() || output.getHeight() != splatmap.getHeight() ||
        !output.getPixelSetFunction() || palette.colors_.size() != static_cast<std::size_t>(splatmap.getLayerCount()))
        return raster_detail::invalid("terrain.splatAlbedo: matching writable image, splatmap and palette required");
    image::ImageData next(output);
    for (int y = 0; y < splatmap.getHeight(); ++y) for (int x = 0; x < splatmap.getWidth(); ++x) {
        image::ImageData::Colorf color{0, 0, 0, 0};
        for (int layer = 0; layer < splatmap.getLayerCount(); ++layer) {
            auto weight = splatmap.sample(layer, x, y);
            if (!weight.ok()) return Result<int>::failure(weight.status());
            const auto& source = palette.colors_[static_cast<std::size_t>(layer)];
            color.r += source[0] * weight.value(); color.g += source[1] * weight.value();
            color.b += source[2] * weight.value(); color.a += source[3] * weight.value();
        }
        next.setPixel(x, y, color);
    }
    output.adopt(next);
    return Result<int>::success(splatmap.getWidth() * splatmap.getHeight());
}

Result<int> generateGtsGlobalBlendDistance(Heightmap& output, const Heightmap& heights, double cameraX,
                                            double cameraY, double cameraZ, double blendDistance,
                                            double blendRange, double originX, double originY, double originZ,
                                            double spacingX, double spacingZ) {
    using namespace raster_detail;
    if (!validRaster(output) || !validRaster(heights) || output.getWidth() != heights.getWidth() ||
        output.getHeight() != heights.getHeight() || !std::isfinite(cameraX) || !std::isfinite(cameraY) ||
        !std::isfinite(cameraZ) || !std::isfinite(blendDistance) || blendDistance < 0 ||
        !std::isfinite(blendRange) || blendRange <= 0 || !std::isfinite(originX) ||
        !std::isfinite(originY) || !std::isfinite(originZ) || !std::isfinite(spacingX) || spacingX <= 0 ||
        !std::isfinite(spacingZ) || spacingZ <= 0)
        return invalid("terrain.gtsGlobalBlend: matching rasters and finite camera/profile settings required");
    Heightmap next(output);
    int changed = 0;
    for (int z = 0; z < heights.getHeight(); ++z) for (int x = 0; x < heights.getWidth(); ++x) {
        const double dx = cameraX - (originX + x * spacingX);
        const double dy = cameraY - (originY + heights.height(x, z));
        const double dz = cameraZ - (originZ + z * spacingZ);
        const double scaled = std::clamp((dx * dx + dy * dy + dz * dz) * blendDistance / 10000.0, 0.0, 1.0);
        const float value = static_cast<float>(std::clamp(std::pow(scaled, blendRange), 0.0, 1.0));
        changed += next.height(x, z) != value;
        next.setHeight(x, z, value);
    }
    output.data().swap(next.data());
    return Result<int>::success(changed);
}

Result<int> bakeGtsColorMapAlbedo(image::ImageData& output, const image::ImageData& colorMap,
                                  const Heightmap& globalBlendDistance, const GtsColorMapSettings& settings) {
    using namespace raster_detail;
    const auto unit = [](float value) { return std::isfinite(value) && value >= 0 && value <= 1; };
    if (!validImage(output, true) || !validImage(colorMap) || !validRaster(globalBlendDistance) ||
        output.getWidth() != colorMap.getWidth() || output.getHeight() != colorMap.getHeight() ||
        output.getWidth() != globalBlendDistance.getWidth() || output.getHeight() != globalBlendDistance.getHeight() ||
        !std::isfinite(settings.alphaIntensity) || settings.alphaIntensity < 0 ||
        !std::isfinite(settings.colorIntensity) || settings.colorIntensity < 0 ||
        !unit(settings.nearIntensity) || !unit(settings.farIntensity))
        return invalid("terrain.gtsColorMap: matching images, normalized blend and finite settings required");
    image::ImageData next(output);
    int changed = 0;
    for (int y = 0; y < output.getHeight(); ++y) for (int x = 0; x < output.getWidth(); ++x) {
        const float distance = globalBlendDistance.height(x, y);
        if (!unit(distance)) return invalid("terrain.gtsColorMap: normalized blend raster required");
        const auto base = output.getPixel(x, y), map = colorMap.getPixel(x, y);
        const float alpha = std::clamp(map.a * settings.alphaIntensity, 0.0F, 1.0F);
        const float distanceIntensity = settings.nearIntensity +
                                        (settings.farIntensity - settings.nearIntensity) * distance;
        const float blend = alpha * distanceIntensity;
        auto color = base;
        color.r += (std::clamp(map.r * settings.colorIntensity, 0.0F, 1.0F) - color.r) * blend;
        color.g += (std::clamp(map.g * settings.colorIntensity, 0.0F, 1.0F) - color.g) * blend;
        color.b += (std::clamp(map.b * settings.colorIntensity, 0.0F, 1.0F) - color.b) * blend;
        changed += color.r != base.r || color.g != base.g || color.b != base.b;
        next.setPixel(x, y, color);
    }
    output.adopt(next);
    return Result<int>::success(changed);
}

Result<int> bakeGtsMacroVariationAlbedo(image::ImageData& output, const image::ImageData& variationMap,
                                         const GtsMacroVariationSettings& settings, double originX,
                                         double originZ, double spacingX, double spacingZ) {
    using namespace raster_detail;
    if (!validImage(output, true) || !validImage(variationMap) || !std::isfinite(settings.sizeA) ||
        !std::isfinite(settings.sizeB) || !std::isfinite(settings.sizeC) || settings.sizeA <= 0 ||
        settings.sizeB <= 0 || settings.sizeC <= 0 || !std::isfinite(settings.intensity) ||
        settings.intensity < 0 || settings.intensity > 1 || !std::isfinite(originX) ||
        !std::isfinite(originZ) || !std::isfinite(spacingX) || spacingX <= 0 ||
        !std::isfinite(spacingZ) || spacingZ <= 0)
        return invalid("terrain.gtsMacroVariation: readable images and finite profile settings required");
    image::ImageData next(output);
    int changed = 0;
    for (int y = 0; y < output.getHeight(); ++y) for (int x = 0; x < output.getWidth(); ++x) {
        const double px = (settings.objectSpace ? 0.0 : originX) + x * spacingX;
        const double pz = (settings.objectSpace ? 0.0 : originZ) + y * spacingZ;
        const float a = repeatBilinear(variationMap, px * settings.sizeA / 1000.0, pz * settings.sizeA / 1000.0).r;
        const float b = repeatBilinear(variationMap, px * settings.sizeB / 10000.0, pz * settings.sizeB / 10000.0).r;
        const float c = repeatBilinear(variationMap, px * settings.sizeC / 100000.0, pz * settings.sizeC / 100000.0).r;
        const float variation = std::clamp((a + 0.5F) * (b + 0.5F) * (c + 0.5F), 0.0F, 1.0F);
        const float multiplier = settings.intensity + (1 - settings.intensity) * variation;
        const auto base = output.getPixel(x, y);
        auto color = base; color.r *= multiplier; color.g *= multiplier; color.b *= multiplier;
        changed += color.r != base.r || color.g != base.g || color.b != base.b;
        next.setPixel(x, y, color);
    }
    output.adopt(next);
    return Result<int>::success(changed);
}

Result<int> bakeGtsGeologicalSurface(image::ImageData& albedo, image::ImageData& packedNormal,
                                     const Heightmap& heights, const Heightmap& layerStrength,
                                     const Heightmap& globalBlendDistance, const image::ImageData& geoAlbedo,
                                     const image::ImageData& geoNormal, const GtsGeologicalSettings& settings,
                                     double originY) {
    using namespace raster_detail;
    const int width = heights.getWidth(), height = heights.getHeight();
    const auto sameRaster = [&](const Heightmap& value) {
        return validRaster(value) && value.getWidth() == width && value.getHeight() == height;
    };
    const auto sameImage = [&](const image::ImageData& value, bool writable) {
        return validImage(value, writable) && value.getWidth() == width && value.getHeight() == height;
    };
    const auto unit = [](float value) { return std::isfinite(value) && value >= 0 && value <= 1; };
    if (!validRaster(heights) || !sameRaster(layerStrength) || !sameRaster(globalBlendDistance) ||
        !sameImage(albedo, true) || !sameImage(packedNormal, true) || !validImage(geoAlbedo) ||
        !validImage(geoNormal) || !std::isfinite(originY) || !unit(settings.nearStrength) ||
        !unit(settings.farStrength) || !std::isfinite(settings.nearNormalStrength) ||
        settings.nearNormalStrength < 0 || !std::isfinite(settings.farNormalStrength) ||
        settings.farNormalStrength < 0 || !std::isfinite(settings.nearScale) || settings.nearScale <= 0 ||
        !std::isfinite(settings.farScale) || settings.farScale <= 0 ||
        !std::isfinite(settings.nearOffset) || !std::isfinite(settings.farOffset))
        return invalid("terrain.gtsGeological: matching outputs, rasters, textures and finite settings required");
    image::ImageData nextAlbedo(albedo), nextNormal(packedNormal);
    int changed = 0;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const float strength = layerStrength.height(x, y), distance = globalBlendDistance.height(x, y);
        if (!unit(strength) || !unit(distance))
            return invalid("terrain.gtsGeological: normalized strength and blend rasters required");
        if (!settings.enabled) continue;
        const double vertical = heights.height(x, y) + (settings.objectSpace ? 0.0 : originY);
        const auto nearColor = repeatBilinear(geoAlbedo, 0, vertical / settings.nearScale + settings.nearOffset);
        const auto farColor = repeatBilinear(geoAlbedo, 0, vertical / settings.farScale + settings.farOffset);
        const auto nearNormal = repeatBilinear(geoNormal, 0, vertical / settings.nearScale + settings.nearOffset);
        const auto farNormal = repeatBilinear(geoNormal, 0, vertical / settings.farScale + settings.farOffset);
        const auto baseColor = albedo.getPixel(x, y);
        auto color = baseColor;
        const auto overlay = [&](float nearValue, float farValue) {
            const float a = (nearValue * 2 - 0.3F) * settings.nearStrength;
            const float b = (farValue * 2 - 0.3F) * settings.farStrength;
            return (a + (b - a) * distance) * strength;
        };
        color.r = std::clamp(color.r + overlay(nearColor.r, farColor.r), 0.0F, 1.0F);
        color.g = std::clamp(color.g + overlay(nearColor.g, farColor.g), 0.0F, 1.0F);
        color.b = std::clamp(color.b + overlay(nearColor.b, farColor.b), 0.0F, 1.0F);
        const auto basePacked = packedNormal.getPixel(x, y);
        const float nearNx = (nearNormal.a * 2 - 1) * settings.nearNormalStrength;
        const float nearNy = (nearNormal.g * 2 - 1) * settings.nearNormalStrength;
        const float farNx = (farNormal.a * 2 - 1) * settings.farNormalStrength;
        const float farNy = (farNormal.g * 2 - 1) * settings.farNormalStrength;
        float nx = basePacked.r * 2 - 1 + (nearNx + (farNx - nearNx) * distance) * strength;
        float ny = basePacked.g * 2 - 1 + (nearNy + (farNy - nearNy) * distance) * strength;
        const float nz = std::sqrt(1 - std::clamp(nx * nx + ny * ny, 0.0F, 1.0F));
        const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
        nx /= length; ny /= length;
        auto normal = basePacked; normal.r = nx * 0.5F + 0.5F; normal.g = ny * 0.5F + 0.5F;
        changed += color.r != baseColor.r || color.g != baseColor.g || color.b != baseColor.b;
        changed += normal.r != basePacked.r || normal.g != basePacked.g;
        nextAlbedo.setPixel(x, y, color); nextNormal.setPixel(x, y, normal);
    }
    albedo.adopt(nextAlbedo); packedNormal.adopt(nextNormal);
    return Result<int>::success(changed);
}

Result<int> bakeGtsDetailSurface(image::ImageData& albedo, image::ImageData& packedNormal,
                                 Heightmap& detailGreyscale, const Heightmap& layerStrength,
                                 const Heightmap& globalBlendDistance, const image::ImageData& detailNormal,
                                 const GtsDetailNormalSettings& settings, double originX, double originZ,
                                 double spacingX, double spacingZ) {
    using namespace raster_detail;
    const int width = albedo.getWidth(), height = albedo.getHeight();
    const auto rasterMatches = [&](const Heightmap& value) {
        return validRaster(value) && value.getWidth() == width && value.getHeight() == height;
    };
    const auto unit = [](float value) { return std::isfinite(value) && value >= 0 && value <= 1; };
    if (!validImage(albedo, true) || !validImage(packedNormal, true) || packedNormal.getWidth() != width ||
        packedNormal.getHeight() != height || !rasterMatches(detailGreyscale) || !rasterMatches(layerStrength) ||
        !rasterMatches(globalBlendDistance) || !validImage(detailNormal) || !std::isfinite(settings.nearTiling) ||
        settings.nearTiling <= 0 || !std::isfinite(settings.farTiling) || settings.farTiling <= 0 ||
        !std::isfinite(settings.nearStrength) || settings.nearStrength < 0 ||
        !std::isfinite(settings.farStrength) || settings.farStrength < 0 || !std::isfinite(originX) ||
        !std::isfinite(originZ) || !std::isfinite(spacingX) || spacingX <= 0 ||
        !std::isfinite(spacingZ) || spacingZ <= 0)
        return invalid("terrain.gtsDetail: matching outputs, rasters, texture and finite settings required");
    image::ImageData nextAlbedo(albedo), nextNormal(packedNormal);
    Heightmap nextGreyscale(detailGreyscale);
    int changed = 0;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const float strength = layerStrength.height(x, y), distance = globalBlendDistance.height(x, y);
        if (!unit(strength) || !unit(distance))
            return invalid("terrain.gtsDetail: normalized strength and blend rasters required");
        const double px = (settings.objectSpace ? 0.0 : originX) + x * spacingX;
        const double pz = (settings.objectSpace ? 0.0 : originZ) + y * spacingZ;
        float nx = 0, ny = 0, greyscale = 0;
        if (settings.enabled) {
            const auto nearSample = repeatBilinear(detailNormal, px / settings.nearTiling, pz / settings.nearTiling);
            const auto farSample = repeatBilinear(detailNormal, px / settings.farTiling, pz / settings.farTiling);
            const float nearX = (nearSample.r * 2 - 1) * settings.nearStrength;
            const float nearY = (nearSample.g * 2 - 1) * settings.nearStrength;
            const float farX = (farSample.r * 2 - 1) * settings.farStrength;
            const float farY = (farSample.g * 2 - 1) * settings.farStrength;
            nx = nearX + (farX - nearX) * distance;
            ny = nearY + (farY - nearY) * distance;
            greyscale = std::clamp(nx * nx + ny * ny, 0.0F, 1.0F);
        }
        const auto baseColor = albedo.getPixel(x, y), basePacked = packedNormal.getPixel(x, y);
        auto color = baseColor;
        const float shade = 1 - 0.5F * greyscale * strength;
        color.r *= shade; color.g *= shade; color.b *= shade;
        float combinedX = basePacked.r * 2 - 1 + nx * strength;
        float combinedY = basePacked.g * 2 - 1 + ny * strength;
        const float combinedZ = std::sqrt(1 - std::clamp(combinedX * combinedX + combinedY * combinedY, 0.0F, 1.0F));
        const float length = std::sqrt(combinedX * combinedX + combinedY * combinedY + combinedZ * combinedZ);
        combinedX /= length; combinedY /= length;
        auto normal = basePacked;
        normal.r = combinedX * 0.5F + 0.5F; normal.g = combinedY * 0.5F + 0.5F;
        changed += color.r != baseColor.r || color.g != baseColor.g || color.b != baseColor.b;
        changed += normal.r != basePacked.r || normal.g != basePacked.g;
        changed += detailGreyscale.height(x, y) != greyscale;
        nextAlbedo.setPixel(x, y, color); nextNormal.setPixel(x, y, normal);
        nextGreyscale.setHeight(x, y, greyscale);
    }
    albedo.adopt(nextAlbedo); packedNormal.adopt(nextNormal); detailGreyscale.data().swap(nextGreyscale.data());
    return Result<int>::success(changed);
}

Result<int> bakeGtsWeatherAlbedo(image::ImageData& output, const Heightmap& heights,
                                 const image::ImageData& snowAlbedo, const image::ImageData& snowMask,
                                 const GtsSnowSurfaceSettings& snow, const GtsRainSurfaceSettings& rain,
                                 double originX, double originZ, double spacingX, double spacingZ,
                                 const Heightmap* detailGreyscale) {
    using namespace raster_detail;
    auto unit = [](float value) { return std::isfinite(value) && value >= 0 && value <= 1; };
    if (!validImage(output, true) || !validImage(snowAlbedo) || !validImage(snowMask) || !validRaster(heights) ||
        output.getWidth() != heights.getWidth() || output.getHeight() != heights.getHeight() ||
        !std::isfinite(originX) || !std::isfinite(originZ) || !std::isfinite(spacingX) || spacingX <= 0 ||
        !std::isfinite(spacingZ) || spacingZ <= 0 || !unit(snow.power) || !unit(snow.age) ||
        !std::isfinite(snow.minimumHeight) || !std::isfinite(snow.blendRange) || snow.blendRange < 0 ||
        !std::isfinite(snow.slopeBlend) || snow.slopeBlend < 0 || !std::isfinite(snow.scale) || snow.scale <= 0 ||
        !unit(snow.colorR) || !unit(snow.colorG) || !unit(snow.colorB) || !unit(rain.power) ||
        (detailGreyscale && (!validRaster(*detailGreyscale) || detailGreyscale->getWidth() != output.getWidth() ||
                             detailGreyscale->getHeight() != output.getHeight())) ||
        !std::isfinite(rain.minimumHeight) || !std::isfinite(rain.maximumHeight) ||
        rain.maximumHeight < rain.minimumHeight || !unit(rain.darkness))
        return invalid("terrain.gtsWeather: finite matching images, heights and profile settings required");
    image::ImageData next(output);
    int changed = 0;
    for (int z = 0; z < heights.getHeight(); ++z) for (int x = 0; x < heights.getWidth(); ++x) {
        const float h = heights.height(x, z);
        const float normalY = terrainNormal(heights, x, z, spacingX, spacingZ).y;
        auto color = output.getPixel(x, z);
        if (snow.enabled) {
            const auto sampled = repeatBilinear(snowAlbedo, (originX + x * spacingX) / snow.scale,
                                                (originZ + z * spacingZ) / snow.scale);
            const float detail = detailGreyscale ? detailGreyscale->height(x, z) : 0;
            if (!unit(detail)) return invalid("terrain.gtsWeather: normalized detail greyscale required");
            const float snowDetail = 1 - 0.8F * detail;
            const float start = smoothStep(-snow.blendRange, snow.blendRange, h - snow.minimumHeight);
            const float slopeAge = 1 + 9 * snow.age;
            const float slope = std::clamp(std::pow(std::abs(normalY), snow.slopeBlend * slopeAge) * 4, 0.0F, 1.0F);
            const float mask = start * slope * (1 - snow.age) * snow.power;
            color.r += (sampled.r * snowDetail * snow.colorR - color.r) * mask;
            color.g += (sampled.g * snowDetail * snow.colorG - color.g) * mask;
            color.b += (sampled.b * snowDetail * snow.colorB - color.b) * mask;
        }
        if (rain.enabled) {
            const float start = smoothStep(-30, 30, h - rain.minimumHeight);
            const float end = smoothStep(-30, 30, h - rain.maximumHeight);
            const float mask = std::min(rain.power, 0.9F) * start * (1 - end);
            const float dark = 1 - rain.darkness;
            color.r += (color.r * dark - color.r) * mask;
            color.g += (color.g * dark - color.g) * mask;
            color.b += (color.b * dark - color.b) * mask;
        }
        const auto old = output.getPixel(x, z);
        changed += old.r != color.r || old.g != color.g || old.b != color.b || old.a != color.a;
        next.setPixel(x, z, color);
    }
    output.adopt(next);
    return Result<int>::success(changed);
}

Result<int> bakeGtsWeatherPbr(image::ImageData& normalOutput, image::ImageData& maskOutput,
                              Heightmap& displacement, Heightmap& tessellation, const Heightmap& heights,
                              const image::ImageData& snowNormal, const image::ImageData& snowMask,
                              const image::ImageData& rainData, const GtsSnowSurfaceSettings& snow,
                              const GtsRainSurfaceSettings& rain, double timeSeconds, double originX,
                              double originZ, double spacingX, double spacingZ) {
    using namespace raster_detail;
    const int width = heights.getWidth(), height = heights.getHeight();
    auto matches = [&](const image::ImageData& image) {
        return validImage(image, true) && image.getWidth() == width && image.getHeight() == height;
    };
    auto finiteArray = [](const auto& values) {
        return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
    };
    if (!validRaster(heights) || !validRaster(displacement) || !validRaster(tessellation) ||
        displacement.getWidth() != width || displacement.getHeight() != height ||
        tessellation.getWidth() != width || tessellation.getHeight() != height || !matches(normalOutput) ||
        !matches(maskOutput) || !validImage(snowNormal) || !validImage(snowMask) || !validImage(rainData) ||
        !std::isfinite(timeSeconds) || !std::isfinite(originX) || !std::isfinite(originZ) ||
        !std::isfinite(spacingX) || spacingX <= 0 || !std::isfinite(spacingZ) || spacingZ <= 0 ||
        !std::isfinite(snow.normalStrength) || snow.normalStrength < 0 || !finiteArray(snow.maskRemapMin) ||
        !finiteArray(snow.maskRemapMax) || !std::isfinite(snow.heightContrast) || snow.heightContrast < 0 ||
        !std::isfinite(snow.heightBrightness) || !std::isfinite(snow.heightIncrease) ||
        !std::isfinite(snow.displacementContrast) || snow.displacementContrast < 0 ||
        !std::isfinite(snow.displacementBrightness) || !std::isfinite(snow.displacementIncrease) ||
        !std::isfinite(snow.tessellationAmount) || !std::isfinite(rain.speed) ||
        !std::isfinite(rain.smoothness) || rain.smoothness < 0 || rain.smoothness > 1 ||
        !std::isfinite(rain.scale) || rain.scale <= 0)
        return invalid("terrain.gtsWeatherPbr: finite matching outputs, textures and profile settings required");
    GtsSnowSurfaceSettings albedoSnow = snow;
    GtsRainSurfaceSettings albedoRain = rain;
    if (!std::isfinite(albedoSnow.scale) || albedoSnow.scale <= 0 || !std::isfinite(albedoSnow.minimumHeight) ||
        !std::isfinite(albedoSnow.blendRange) || albedoSnow.blendRange < 0 ||
        !std::isfinite(albedoSnow.slopeBlend) || albedoSnow.slopeBlend < 0 || albedoSnow.power < 0 ||
        albedoSnow.power > 1 || albedoSnow.age < 0 || albedoSnow.age > 1 || albedoRain.power < 0 ||
        albedoRain.power > 1 || !std::isfinite(albedoRain.minimumHeight) ||
        !std::isfinite(albedoRain.maximumHeight) || albedoRain.maximumHeight < albedoRain.minimumHeight)
        return invalid("terrain.gtsWeatherPbr: invalid snow or rain range");

    image::ImageData nextNormal(normalOutput), nextMask(maskOutput);
    Heightmap nextDisplacement(displacement), nextTessellation(tessellation);
    int changed = 0;
    auto rainSample = [&](double u, double v) {
        const auto points = repeatBilinear(rainData, u, v);
        const float phase[4]{points.r, points.g, points.b, points.a};
        const float offsets[4]{0, 0.5F, 0.75F, 0.25F};
        float sum = 0;
        for (int i = 0; i < 4; ++i) {
            const double cycle = timeSeconds * rain.speed + offsets[i];
            const float t = static_cast<float>(cycle - std::floor(cycle));
            sum += std::clamp(std::sin(-20 * t * phase[i]) * std::sin(t * 3.1415F), 0.0F, 1.0F);
        }
        return std::clamp(sum, 0.0F, 1.0F);
    };
    for (int z = 0; z < height; ++z) for (int x = 0; x < width; ++x) {
        const float h = heights.height(x, z);
        const double worldX = originX + x * spacingX, worldZ = originZ + z * spacingZ;
        const Vec3 terrain = terrainNormal(heights, x, z, spacingX, spacingZ);
        const auto encoded = normalOutput.getPixel(x, z);
        Vec3 tangent, bitangent;
        terrainFrame(terrain, tangent, bitangent);
        const float baseX = encoded.r * 2 - 1, baseY = encoded.g * 2 - 1;
        const Vec3 baseTangent = normalize({baseX, baseY,
                                            std::sqrt(1 - std::clamp(baseX * baseX + baseY * baseY, 0.0F, 1.0F))});
        Vec3 normal = normalize({baseTangent.x * tangent.x + baseTangent.y * bitangent.x + baseTangent.z * terrain.x,
                                 baseTangent.x * tangent.y + baseTangent.y * bitangent.y + baseTangent.z * terrain.y,
                                 baseTangent.x * tangent.z + baseTangent.y * bitangent.z + baseTangent.z * terrain.z});
        auto packed = maskOutput.getPixel(x, z);
        float displaced = displacement.height(x, z), tessellated = tessellation.height(x, z);
        if (snow.enabled) {
            const double u = worldX / snow.scale, v = worldZ / snow.scale;
            auto sampledNormal = repeatBilinear(snowNormal, u, v);
            const float nx = (sampledNormal.r * sampledNormal.a) * 2 - 1;
            const float ny = sampledNormal.g * 2 - 1;
            Vec3 snowN{nx * snow.normalStrength, ny * snow.normalStrength,
                       std::sqrt(1 - std::clamp(nx * nx + ny * ny, 0.0F, 1.0F))};
            auto sampledMask = repeatBilinear(snowMask, u, v);
            float channels[4]{sampledMask.r, sampledMask.g, sampledMask.b, sampledMask.a};
            for (int i = 0; i < 4; ++i)
                channels[i] = snow.maskRemapMin[i] + channels[i] * (snow.maskRemapMax[i] - snow.maskRemapMin[i]);
            channels[2] = std::pow(std::abs(channels[2]), snow.heightContrast) * snow.heightBrightness +
                          snow.heightIncrease;
            const float start = smoothStep(-snow.blendRange, snow.blendRange, h - snow.minimumHeight);
            const float slopeAge = 1 + 9 * snow.age;
            const float slope = std::clamp(std::pow(std::abs(terrain.y), snow.slopeBlend * slopeAge) * 4, 0.0F, 1.0F);
            const float snowBlend = start * slope * (1 - snow.age) * snow.power;
            packed.r += (channels[0] - packed.r) * snowBlend;
            packed.g += (channels[1] - packed.g) * snowBlend;
            packed.b += (channels[2] - packed.b) * snowBlend;
            packed.a += (channels[3] - packed.a) * snowBlend;
            snowN = blendWorldNormal(terrain, snowN);
            normal = {normal.x + (snowN.x - normal.x) * snowBlend,
                      normal.y + (snowN.y - normal.y) * snowBlend,
                      normal.z + (snowN.z - normal.z) * snowBlend};
            const float slopeLod = std::clamp(std::pow(std::abs(terrain.y), snow.slopeBlend) * 4, 0.0F, 1.0F);
            const float lodBlend = start * slopeLod * (1 - snow.age) * snow.power;
            const float snowDisplacement = std::pow(std::abs(channels[2]), snow.displacementContrast) *
                                               snow.displacementBrightness + snow.displacementIncrease;
            displaced += (snowDisplacement * snow.power - displaced) * lodBlend;
            tessellated += (snow.tessellationAmount * snow.power - tessellated) * lodBlend;
        }
        if (rain.enabled) {
            const float start = smoothStep(-30, 30, h - rain.minimumHeight);
            const float end = smoothStep(-30, 30, h - rain.maximumHeight);
            const float rainMask = std::min(rain.power, 0.9F) * start * (1 - end);
            const float normalY2 = terrain.y * terrain.y;
            const float topMask = std::clamp(normalY2 * normalY2, 0.0F, 1.0F);
            const double u = worldX / rain.scale, v = worldZ / rain.scale;
            const float center = rainSample(u, v), xd = center - rainSample(u + 0.001, v);
            const float yd = center - rainSample(u, v + 0.001), strength = 0.6F * rainMask;
            Vec3 rainN{yd * strength, -xd * yd * strength, 1 + (xd - 1) * std::clamp(strength, 0.0F, 1.0F)};
            const Vec3 rainBlend = blendWorldNormal(normal, rainN);
            const float amount = rainMask * topMask;
            normal = {normal.x + (rainBlend.x - normal.x) * amount,
                      normal.y + (rainBlend.y - normal.y) * amount,
                      normal.z + (rainBlend.z - normal.z) * amount};
            const float rainSmoothness = 0.4F + (rain.smoothness - 0.4F) * topMask;
            packed.a += (rainSmoothness - packed.a) * rainMask;
        }
        const Vec3 finalWorld = normalize(normal);
        const Vec3 finalTangent{dot(finalWorld, tangent), dot(finalWorld, bitangent), dot(finalWorld, terrain)};
        const image::ImageData::Colorf nextEncoded{finalTangent.x * 0.5F + 0.5F,
                                                    finalTangent.y * 0.5F + 0.5F,
                                                    packed.g, packed.a};
        if (!std::isfinite(nextEncoded.r) || !std::isfinite(nextEncoded.g) || !std::isfinite(nextEncoded.b) ||
            !std::isfinite(packed.r) || !std::isfinite(packed.g) || !std::isfinite(packed.b) ||
            !std::isfinite(packed.a) || !std::isfinite(displaced) || !std::isfinite(tessellated))
            return invalid("terrain.gtsWeatherPbr: profile produced a nonfinite output");
        const auto oldMask = maskOutput.getPixel(x, z);
        changed += encoded.r != nextEncoded.r || encoded.g != nextEncoded.g || encoded.b != nextEncoded.b;
        changed += oldMask.r != packed.r || oldMask.g != packed.g || oldMask.b != packed.b || oldMask.a != packed.a;
        changed += displacement.height(x, z) != displaced;
        changed += tessellation.height(x, z) != tessellated;
        nextNormal.setPixel(x, z, nextEncoded); nextMask.setPixel(x, z, packed);
        nextDisplacement.setHeight(x, z, displaced); nextTessellation.setHeight(x, z, tessellated);
    }
    normalOutput.adopt(nextNormal); maskOutput.adopt(nextMask);
    displacement = std::move(nextDisplacement); tessellation = std::move(nextTessellation);
    return Result<int>::success(changed);
}

Result<int> generateTerrainImageMaskFromImage(Heightmap& target, const Heightmap& input, const image::ImageData& image,
                                              const Heightmap& curve, const TerrainImageMaskSettings& settings,
                                              TerrainMaskBlend mode) {
    using namespace raster_detail;
    const int width = image.getWidth(), height = image.getHeight();
    if (!validRaster(target) || !validRaster(input) || !validRaster(curve) || target.getWidth() != input.getWidth() ||
        target.getHeight() != input.getHeight() || curve.getHeight() != 1 || width <= 0 || height <= 0 ||
        width > INT_MAX / height || !image.getData() || !image.getPixelGetFunction())
        return invalid("terrain.imageAdapter: finite masks and readable image storage required");
    const size_t pixelSize = image.getPixelSize();
    if (pixelSize == 0 || pixelSize > sizeof(image::ImageData::Pixel) ||
        size_t(width) * height > image.getSize() / pixelSize)
        return invalid("terrain.imageAdapter: incomplete or unsupported image storage");
    Heightmap red(width, height), green(width, height), blue(width, height), alpha(width, height);
    for (int z = 0; z < height; ++z)
        for (int x = 0; x < width; ++x) {
            const auto color = image.getPixel(x, z);
            if (!std::isfinite(color.r) || !std::isfinite(color.g) || !std::isfinite(color.b) ||
                !std::isfinite(color.a))
                return invalid("terrain.imageAdapter: non-finite image pixel");
            const size_t i  = size_t(z) * width + x;
            red.data()[i]   = color.r;
            green.data()[i] = color.g;
            blue.data()[i]  = color.b;
            alpha.data()[i] = color.a;
        }
    return generateTerrainImageMask(target, input, red, green, blue, alpha, curve, settings, mode);
}
Result<int> combinePcgMaskMapChannels(
    image::ImageData& output, const image::ImageData& red, const image::ImageData& green,
    const image::ImageData& blue, const image::ImageData& alpha, std::uint32_t activeChannels) {
    if (activeChannels > 15 || !validImage(output, true) || !validImage(red) || !validImage(green) ||
        !validImage(blue) || !validImage(alpha))
        return raster_detail::invalid("terrain.maskMapExport: readable RGBA channel images required");
    const int width = output.getWidth(), height = output.getHeight();
    const image::ImageData* sources[] = {&red, &green, &blue, &alpha};
    for (const auto* source : sources)
        if (source->getWidth() != width || source->getHeight() != height)
            return raster_detail::invalid("terrain.maskMapExport: channel dimensions must match output");
    image::ImageData next(output);
    int changed = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            image::ImageData::Colorf combined{0, 0, 0, 0};
            float* channels[] = {&combined.r, &combined.g, &combined.b, &combined.a};
            for (std::uint32_t channel = 0; channel < 4; ++channel)
                if (activeChannels & (1u << channel))
                    *channels[channel] = std::clamp(sources[channel]->getPixel(x, y).r, 0.0F, 1.0F);
            const auto before = output.getPixel(x, y);
            changed += before.r != combined.r || before.g != combined.g ||
                       before.b != combined.b || before.a != combined.a;
            next.setPixel(x, y, combined);
        }
    output.adopt(next);
    return Result<int>::success(changed);
}

Result<int> placePcgMaskMapTileInto(
    image::ImageData& output, const image::ImageData& tile, int destinationX, int destinationY,
    int destinationWidth, int destinationHeight) {
    if (!validImage(output, true) || !validImage(tile) || destinationX < 0 || destinationY < 0 ||
        destinationWidth <= 0 || destinationHeight <= 0 ||
        destinationX > output.getWidth() - destinationWidth ||
        destinationY > output.getHeight() - destinationHeight)
        return raster_detail::invalid("terrain.maskMapExport: tile rectangle is invalid");
    auto sample = [&](double u, double v) {
        const double px = u * tile.getWidth() - 0.5;
        const double py = v * tile.getHeight() - 0.5;
        const int x0 = std::clamp(static_cast<int>(std::floor(px)), 0, tile.getWidth() - 1);
        const int y0 = std::clamp(static_cast<int>(std::floor(py)), 0, tile.getHeight() - 1);
        const int x1 = std::min(x0 + 1, tile.getWidth() - 1);
        const int y1 = std::min(y0 + 1, tile.getHeight() - 1);
        const float tx = std::clamp(static_cast<float>(px - std::floor(px)), 0.0F, 1.0F);
        const float ty = std::clamp(static_cast<float>(py - std::floor(py)), 0.0F, 1.0F);
        const auto a = tile.getPixel(x0, y0), b = tile.getPixel(x1, y0);
        const auto c = tile.getPixel(x0, y1), d = tile.getPixel(x1, y1);
        auto mix = [&](float av, float bv, float cv, float dv) {
            return (av + (bv - av) * tx) * (1 - ty) + (cv + (dv - cv) * tx) * ty;
        };
        return image::ImageData::Colorf{mix(a.r, b.r, c.r, d.r), mix(a.g, b.g, c.g, d.g),
                                        mix(a.b, b.b, c.b, d.b), mix(a.a, b.a, c.a, d.a)};
    };
    image::ImageData next(output);
    int changed = 0;
    for (int y = 0; y < destinationHeight; ++y)
        for (int x = 0; x < destinationWidth; ++x) {
            const auto color = sample((x + 0.5) / destinationWidth, (y + 0.5) / destinationHeight);
            const int atlasX = destinationX + x, atlasY = destinationY + y;
            const auto before = output.getPixel(atlasX, atlasY);
            changed += before.r != color.r || before.g != color.g ||
                       before.b != color.b || before.a != color.a;
            next.setPixel(atlasX, atlasY, color);
        }
    output.adopt(next);
    return Result<int>::success(changed);
}

void exposeTerrainImageAdapter(ssq::Table& table) {
    auto snow = table.addClass("GtsSnowSurfaceSettings", ssq::Class::Ctor<GtsSnowSurfaceSettings()>());
    snow.addVar("enabled", &GtsSnowSurfaceSettings::enabled);
    snow.addVar("power", &GtsSnowSurfaceSettings::power);
    snow.addVar("minimumHeight", &GtsSnowSurfaceSettings::minimumHeight);
    snow.addVar("blendRange", &GtsSnowSurfaceSettings::blendRange);
    snow.addVar("slopeBlend", &GtsSnowSurfaceSettings::slopeBlend);
    snow.addVar("age", &GtsSnowSurfaceSettings::age);
    snow.addVar("scale", &GtsSnowSurfaceSettings::scale);
    snow.addVar("colorR", &GtsSnowSurfaceSettings::colorR);
    snow.addVar("colorG", &GtsSnowSurfaceSettings::colorG);
    snow.addVar("colorB", &GtsSnowSurfaceSettings::colorB);
    snow.addVar("normalStrength", &GtsSnowSurfaceSettings::normalStrength);
    snow.addVar("heightContrast", &GtsSnowSurfaceSettings::heightContrast);
    snow.addVar("heightBrightness", &GtsSnowSurfaceSettings::heightBrightness);
    snow.addVar("heightIncrease", &GtsSnowSurfaceSettings::heightIncrease);
    snow.addVar("displacementContrast", &GtsSnowSurfaceSettings::displacementContrast);
    snow.addVar("displacementBrightness", &GtsSnowSurfaceSettings::displacementBrightness);
    snow.addVar("displacementIncrease", &GtsSnowSurfaceSettings::displacementIncrease);
    snow.addVar("tessellationAmount", &GtsSnowSurfaceSettings::tessellationAmount);
    snow.addFunc("setMaskRemapMin", &GtsSnowSurfaceSettings::setMaskRemapMin);
    snow.addFunc("setMaskRemapMax", &GtsSnowSurfaceSettings::setMaskRemapMax);
    auto rain = table.addClass("GtsRainSurfaceSettings", ssq::Class::Ctor<GtsRainSurfaceSettings()>());
    rain.addVar("enabled", &GtsRainSurfaceSettings::enabled);
    rain.addVar("power", &GtsRainSurfaceSettings::power);
    rain.addVar("minimumHeight", &GtsRainSurfaceSettings::minimumHeight);
    rain.addVar("maximumHeight", &GtsRainSurfaceSettings::maximumHeight);
    rain.addVar("darkness", &GtsRainSurfaceSettings::darkness);
    rain.addVar("speed", &GtsRainSurfaceSettings::speed);
    rain.addVar("smoothness", &GtsRainSurfaceSettings::smoothness);
    rain.addVar("scale", &GtsRainSurfaceSettings::scale);
    auto colorMap = table.addClass("GtsColorMapSettings", ssq::Class::Ctor<GtsColorMapSettings()>());
    colorMap.addVar("alphaIntensity", &GtsColorMapSettings::alphaIntensity);
    colorMap.addVar("colorIntensity", &GtsColorMapSettings::colorIntensity);
    colorMap.addVar("nearIntensity", &GtsColorMapSettings::nearIntensity);
    colorMap.addVar("farIntensity", &GtsColorMapSettings::farIntensity);
    auto variation = table.addClass("GtsMacroVariationSettings", ssq::Class::Ctor<GtsMacroVariationSettings()>());
    variation.addVar("sizeA", &GtsMacroVariationSettings::sizeA);
    variation.addVar("sizeB", &GtsMacroVariationSettings::sizeB);
    variation.addVar("sizeC", &GtsMacroVariationSettings::sizeC);
    variation.addVar("intensity", &GtsMacroVariationSettings::intensity);
    variation.addVar("objectSpace", &GtsMacroVariationSettings::objectSpace);
    auto geological = table.addClass("GtsGeologicalSettings", ssq::Class::Ctor<GtsGeologicalSettings()>());
    geological.addVar("enabled", &GtsGeologicalSettings::enabled);
    geological.addVar("objectSpace", &GtsGeologicalSettings::objectSpace);
    geological.addVar("nearStrength", &GtsGeologicalSettings::nearStrength);
    geological.addVar("nearNormalStrength", &GtsGeologicalSettings::nearNormalStrength);
    geological.addVar("nearScale", &GtsGeologicalSettings::nearScale);
    geological.addVar("nearOffset", &GtsGeologicalSettings::nearOffset);
    geological.addVar("farStrength", &GtsGeologicalSettings::farStrength);
    geological.addVar("farNormalStrength", &GtsGeologicalSettings::farNormalStrength);
    geological.addVar("farScale", &GtsGeologicalSettings::farScale);
    geological.addVar("farOffset", &GtsGeologicalSettings::farOffset);
    auto detail = table.addClass("GtsDetailNormalSettings", ssq::Class::Ctor<GtsDetailNormalSettings()>());
    detail.addVar("enabled", &GtsDetailNormalSettings::enabled);
    detail.addVar("objectSpace", &GtsDetailNormalSettings::objectSpace);
    detail.addVar("nearTiling", &GtsDetailNormalSettings::nearTiling);
    detail.addVar("nearStrength", &GtsDetailNormalSettings::nearStrength);
    detail.addVar("farTiling", &GtsDetailNormalSettings::farTiling);
    detail.addVar("farStrength", &GtsDetailNormalSettings::farStrength);
    auto palette = table.addClass("TerrainSplatPalette", ssq::Class::Ctor<TerrainSplatPalette()>());
    palette.addFunc("addColor", [vm = table.getHandle()](TerrainSplatPalette* self, float r, float g, float b,
                                                           float a) {
        return eve::script::projectResult(vm, self->addColor(r, g, b, a), [](int n) { return eve::Value(n); });
    });
    palette.addFunc("getColorCount", [](const TerrainSplatPalette* self) { return self->getColorCount(); });
    auto packedSettings = table.addClass("GtsPackedLayerSettings", ssq::Class::Ctor<GtsPackedLayerSettings()>());
    packedSettings.addVar("triPlanar", &GtsPackedLayerSettings::triPlanar);
    packedSettings.addVar("stochastic", &GtsPackedLayerSettings::stochastic);
    packedSettings.addVar("tileSizeX", &GtsPackedLayerSettings::tileSizeX); packedSettings.addVar("tileSizeZ", &GtsPackedLayerSettings::tileSizeZ);
    packedSettings.addVar("offsetX", &GtsPackedLayerSettings::offsetX); packedSettings.addVar("offsetZ", &GtsPackedLayerSettings::offsetZ);
    packedSettings.addVar("triPlanarSizeX", &GtsPackedLayerSettings::triPlanarSizeX);
    packedSettings.addVar("triPlanarSizeZ", &GtsPackedLayerSettings::triPlanarSizeZ);
    packedSettings.addVar("tintR", &GtsPackedLayerSettings::tintR); packedSettings.addVar("tintG", &GtsPackedLayerSettings::tintG);
    packedSettings.addVar("tintB", &GtsPackedLayerSettings::tintB); packedSettings.addVar("normalStrength", &GtsPackedLayerSettings::normalStrength);
    packedSettings.addVar("aoMin", &GtsPackedLayerSettings::aoMin); packedSettings.addVar("smoothnessMin", &GtsPackedLayerSettings::smoothnessMin);
    packedSettings.addVar("aoMax", &GtsPackedLayerSettings::aoMax); packedSettings.addVar("smoothnessMax", &GtsPackedLayerSettings::smoothnessMax);
    packedSettings.addVar("geoAmount", &GtsPackedLayerSettings::geoAmount); packedSettings.addVar("detailAmount", &GtsPackedLayerSettings::detailAmount);
    packedSettings.addVar("heightContrast", &GtsPackedLayerSettings::heightContrast);
    packedSettings.addVar("heightBrightness", &GtsPackedLayerSettings::heightBrightness);
    packedSettings.addVar("heightIncrease", &GtsPackedLayerSettings::heightIncrease);
    packedSettings.addVar("displacementContrast", &GtsPackedLayerSettings::displacementContrast);
    packedSettings.addVar("displacementBrightness", &GtsPackedLayerSettings::displacementBrightness);
    packedSettings.addVar("displacementIncrease", &GtsPackedLayerSettings::displacementIncrease);
    packedSettings.addVar("tessellationAmount", &GtsPackedLayerSettings::tessellationAmount);
    auto packedLayers = table.addClass("GtsPackedLayerSet", ssq::Class::Ctor<GtsPackedLayerSet()>());
    packedLayers.addFunc("addLayer", [vm = table.getHandle()](GtsPackedLayerSet* self, const image::ImageData* albedo,
                                                                const image::ImageData* normal, const GtsPackedLayerSettings* settings) {
        auto result = albedo && normal && settings ? self->addLayer(*albedo, *normal, *settings)
                                                   : raster_detail::invalid("terrain.gtsPackedLayers: missing input");
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    packedLayers.addFunc("getLayerCount", [](const GtsPackedLayerSet* self) { return self->getLayerCount(); });
    table.addFunc("bakeTerrainSplatAlbedo",
                  [vm = table.getHandle()](image::ImageData* output, const TerrainSplatmap* splatmap,
                                           const TerrainSplatPalette* paletteValue) {
        auto result = output && splatmap && paletteValue ? bakeTerrainSplatAlbedo(*output, *splatmap, *paletteValue)
                                                         : raster_detail::invalid("terrain.splatAlbedo: missing input");
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("bakeGtsPackedLayers",
                  [vm = table.getHandle()](image::ImageData* albedo, image::ImageData* normal,
                                           Heightmap* geo, Heightmap* detail, const Heightmap* heights,
                                           const TerrainSplatmap* splat, const GtsPackedLayerSet* layers,
                                           float originX, float originY, float originZ, float spacingX,
                                           float spacingZ) {
        auto result = albedo && normal && geo && detail && heights && splat && layers
                          ? bakeGtsPackedLayers(*albedo,*normal,*geo,*detail,*heights,*splat,*layers,
                                                originX,originY,originZ,spacingX,spacingZ)
                          : raster_detail::invalid("terrain.gtsPackedLayers: missing input");
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("bakeGtsPackedLayerDisplacement",
                  [vm = table.getHandle()](Heightmap* displacement, Heightmap* tessellation,
                                           const Heightmap* heights, const TerrainSplatmap* splat,
                                           const GtsPackedLayerSet* layers, float cameraX, float cameraY,
                                           float cameraZ, float multiplier, float originX, float originY,
                                           float originZ, float spacingX, float spacingZ) {
        auto result = displacement && tessellation && heights && splat && layers
                          ? bakeGtsPackedLayerDisplacement(*displacement, *tessellation, *heights, *splat,
                                                           *layers, cameraX, cameraY, cameraZ, multiplier,
                                                           originX, originY, originZ, spacingX, spacingZ)
                          : raster_detail::invalid("terrain.gtsLayerDisplacement: missing input");
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("generateGtsGlobalBlendDistance",
                  [vm = table.getHandle()](Heightmap* output, const Heightmap* heights, float cameraX,
                                           float cameraY, float cameraZ, float blendDistance, float blendRange,
                                           float originX, float originY, float originZ, float spacingX,
                                           float spacingZ) {
        auto result = output && heights
                          ? generateGtsGlobalBlendDistance(*output, *heights, cameraX, cameraY, cameraZ,
                                                           blendDistance, blendRange, originX, originY, originZ,
                                                           spacingX, spacingZ)
                          : raster_detail::invalid("terrain.gtsGlobalBlend: missing input");
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("placePcgMaskMapTileInto",
                  [vm = table.getHandle()](image::ImageData* output, const image::ImageData* tile,
                                           int x, int y, int width, int height) {
        auto result = output && tile
                          ? placePcgMaskMapTileInto(*output, *tile, x, y, width, height)
                          : raster_detail::invalid("terrain.maskMapExport: missing tile input");
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("combinePcgMaskMapChannels",
                  [vm = table.getHandle()](image::ImageData* output, const image::ImageData* red,
                                           const image::ImageData* green, const image::ImageData* blue,
                                           const image::ImageData* alpha, int activeChannels) {
        auto result = output && red && green && blue && alpha
                          ? combinePcgMaskMapChannels(*output, *red, *green, *blue, *alpha,
                                                       static_cast<std::uint32_t>(activeChannels))
                          : raster_detail::invalid("terrain.maskMapExport: missing input");
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("bakeGtsWeatherAlbedo",
                  [vm = table.getHandle()](image::ImageData* output, const Heightmap* heights,
                                           const image::ImageData* snowAlbedo,
                                           const image::ImageData* snowMask,
                                           const GtsSnowSurfaceSettings* snowSettings,
                                           const GtsRainSurfaceSettings* rainSettings,
                                           float originX, float originZ, float spacingX, float spacingZ) {
        auto result = output && heights && snowAlbedo && snowMask && snowSettings && rainSettings
                          ? bakeGtsWeatherAlbedo(*output, *heights, *snowAlbedo, *snowMask, *snowSettings,
                                                *rainSettings, originX, originZ, spacingX, spacingZ)
                          : raster_detail::invalid("terrain.gtsWeather: missing input");
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("bakeGtsColorMapAlbedo",
                  [vm = table.getHandle()](image::ImageData* output, const image::ImageData* map,
                                           const Heightmap* blend, const GtsColorMapSettings* settings) {
        auto result = output && map && blend && settings
                          ? bakeGtsColorMapAlbedo(*output, *map, *blend, *settings)
                          : raster_detail::invalid("terrain.gtsColorMap: missing input");
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("bakeGtsMacroVariationAlbedo",
                  [vm = table.getHandle()](image::ImageData* output, const image::ImageData* map,
                                           const GtsMacroVariationSettings* settings, float originX,
                                           float originZ, float spacingX, float spacingZ) {
        auto result = output && map && settings
                          ? bakeGtsMacroVariationAlbedo(*output, *map, *settings, originX, originZ, spacingX, spacingZ)
                          : raster_detail::invalid("terrain.gtsMacroVariation: missing input");
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("bakeGtsGeologicalSurface",
                  [vm = table.getHandle()](image::ImageData* albedo, image::ImageData* normal,
                                           const Heightmap* heights, const Heightmap* strength,
                                           const Heightmap* blend, const image::ImageData* geoAlbedo,
                                           const image::ImageData* geoNormal,
                                           const GtsGeologicalSettings* settings, float originY) {
        auto result = albedo && normal && heights && strength && blend && geoAlbedo && geoNormal && settings
                          ? bakeGtsGeologicalSurface(*albedo, *normal, *heights, *strength, *blend,
                                                     *geoAlbedo, *geoNormal, *settings, originY)
                          : raster_detail::invalid("terrain.gtsGeological: missing input");
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("bakeGtsDetailSurface",
                  [vm = table.getHandle()](image::ImageData* albedo, image::ImageData* normal,
                                           Heightmap* greyscale, const Heightmap* strength,
                                           const Heightmap* blend, const image::ImageData* detailNormal,
                                           const GtsDetailNormalSettings* settings, float originX,
                                           float originZ, float spacingX, float spacingZ) {
        auto result = albedo && normal && greyscale && strength && blend && detailNormal && settings
                          ? bakeGtsDetailSurface(*albedo, *normal, *greyscale, *strength, *blend,
                                                 *detailNormal, *settings, originX, originZ, spacingX, spacingZ)
                          : raster_detail::invalid("terrain.gtsDetail: missing input");
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("bakeGtsWeatherAlbedoDetailed",
                  [vm = table.getHandle()](image::ImageData* output, const Heightmap* heights,
                                           const image::ImageData* snowAlbedo, const image::ImageData* snowMask,
                                           const GtsSnowSurfaceSettings* snowSettings,
                                           const GtsRainSurfaceSettings* rainSettings,
                                           const Heightmap* detailGreyscale, float originX, float originZ,
                                           float spacingX, float spacingZ) {
        auto result = output && heights && snowAlbedo && snowMask && snowSettings && rainSettings && detailGreyscale
                          ? bakeGtsWeatherAlbedo(*output, *heights, *snowAlbedo, *snowMask, *snowSettings,
                                                *rainSettings, originX, originZ, spacingX, spacingZ, detailGreyscale)
                          : raster_detail::invalid("terrain.gtsWeather: missing detailed input");
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("bakeGtsWeatherPbr",
                  [vm = table.getHandle()](image::ImageData* normalOutput, image::ImageData* maskOutput,
                                           Heightmap* displacement, Heightmap* tessellation,
                                           const Heightmap* heights, const image::ImageData* snowNormal,
                                           const image::ImageData* snowMask, const image::ImageData* rainData,
                                           const GtsSnowSurfaceSettings* snowSettings,
                                           const GtsRainSurfaceSettings* rainSettings, float timeSeconds,
                                           float originX, float originZ, float spacingX, float spacingZ) {
        auto result = normalOutput && maskOutput && displacement && tessellation && heights && snowNormal &&
                              snowMask && rainData && snowSettings && rainSettings
                          ? bakeGtsWeatherPbr(*normalOutput, *maskOutput, *displacement, *tessellation, *heights,
                                              *snowNormal, *snowMask, *rainData, *snowSettings, *rainSettings,
                                              timeSeconds, originX, originZ, spacingX, spacingZ)
                          : raster_detail::invalid("terrain.gtsWeatherPbr: missing input");
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("generateTerrainImageMaskFromImage",
                  [vm = table.getHandle()](Heightmap* target, const Heightmap* input, const image::ImageData* image,
                                           const Heightmap* curve, const TerrainImageMaskSettings* settings, int filter,
                                           int mode) {
                      auto result = [&]() -> Result<int> {
                          if (!target || !input || !image || !curve || !settings)
                              return raster_detail::invalid("terrain.imageAdapter: missing input");
                          auto copy   = *settings;
                          copy.filter = static_cast<TerrainImageFilter>(filter);
                          return generateTerrainImageMaskFromImage(*target, *input, *image, *curve, copy,
                                                                   static_cast<TerrainMaskBlend>(mode));
                      }();
                      return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
                  });
}
}  // namespace eve::procgen
