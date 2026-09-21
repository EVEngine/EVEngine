#include "asset/procgen/TerrainMaterialAtlas.h"

#include "graphics/IResourceFactory.h"
#include "graphics/Shader.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace eve::asset_procgen {
namespace {
template <class T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(
        Diagnostic::error(code, std::move(message), std::move(path), {}, "asset.procgen.terrain-atlas"));
}

TerrainAtlasImage solid(std::array<std::uint8_t, 4> color) { return {1, 1, {color[0], color[1], color[2], color[3]}}; }

std::array<std::uint8_t, 4> sample(const asset::DecodedEvpackImage& image, std::uint32_t x, std::uint32_t y,
                                   std::uint32_t width, std::uint32_t height) {
    const float                 sx = (float(x) + .5f) * float(image.width) / float(width) - .5f;
    const float                 sy = (float(y) + .5f) * float(image.height) / float(height) - .5f;
    const int                   x0 = std::clamp(int(std::floor(sx)), 0, int(image.width) - 1);
    const int                   y0 = std::clamp(int(std::floor(sy)), 0, int(image.height) - 1);
    const int                   x1 = std::min(x0 + 1, int(image.width) - 1);
    const int                   y1 = std::min(y0 + 1, int(image.height) - 1);
    const float                 fx = std::clamp(sx - std::floor(sx), 0.f, 1.f);
    const float                 fy = std::clamp(sy - std::floor(sy), 0.f, 1.f);
    std::array<std::uint8_t, 4> result{};
    for (std::size_t channel = 0; channel < 4; ++channel) {
        auto at = [&](int px, int py) {
            return float(image.pixels[(std::size_t(py) * image.width + px) * 4 + channel]);
        };
        const float top    = std::lerp(at(x0, y0), at(x1, y0), fx);
        const float bottom = std::lerp(at(x0, y1), at(x1, y1), fx);
        result[channel]    = std::uint8_t(std::clamp(std::lround(std::lerp(top, bottom, fy)), 0l, 255l));
    }
    return result;
}

void place(TerrainAtlasImage& atlas, const asset::DecodedEvpackImage* source, std::uint32_t slot,
           std::array<std::uint8_t, 4> fallback, const RuntimeTerrainLayer* layer, bool normal, bool mask) {
    const std::uint32_t tileWidth = atlas.width / 2, tileHeight = atlas.height / 2;
    const std::uint32_t ox = (slot & 1u) * tileWidth, oy = (slot >> 1u) * tileHeight;
    for (std::uint32_t y = 0; y < tileHeight; ++y) {
        for (std::uint32_t x = 0; x < tileWidth; ++x) {
            auto value = source ? sample(*source, x, y, tileWidth, tileHeight) : fallback;
            if (normal && layer && layer->normalConvention == "directx") value[1] = std::uint8_t(255 - value[1]);
            if (mask && layer) {
                for (std::size_t channel = 0; channel < 4; ++channel) {
                    const float unit = float(value[channel]) / 255.f;
                    const float remapped =
                        std::lerp(layer->maskRemapMinimum[channel], layer->maskRemapMaximum[channel], unit);
                    value[channel] = std::uint8_t(std::clamp(std::lround(remapped * 255.f), 0l, 255l));
                }
            }
            const std::size_t destination = (std::size_t(oy + y) * atlas.width + ox + x) * 4;
            std::copy(value.begin(), value.end(), atlas.pixels.begin() + std::ptrdiff_t(destination));
        }
    }
}

Result<std::optional<asset::DecodedEvpackImage>> decodeOptional(const asset::EvpackResourceReader&    reader,
                                                                const std::optional<AssetRef>&        reference,
                                                                const asset::EvpackCapabilities&      capabilities,
                                                                const asset::EvpackImageDecodeLimits& limits) {
    if (!reference) return Result<std::optional<asset::DecodedEvpackImage>>::success(std::nullopt);
    auto decoded = asset::decodeEvpackImage(reader, *reference, capabilities, limits);
    if (!decoded) return Result<std::optional<asset::DecodedEvpackImage>>::failure(decoded.status());
    return Result<std::optional<asset::DecodedEvpackImage>>::success(std::move(decoded).takeValue());
}

std::array<std::uint8_t, 4> sampleImage(const TerrainAtlasImage& image, float u, float v) {
    const auto x      = std::min(std::uint32_t(u * float(image.width)), image.width - 1);
    const auto y      = std::min(std::uint32_t(v * float(image.height)), image.height - 1);
    const auto offset = (std::size_t(y) * image.width + x) * 4;
    return {image.pixels[offset], image.pixels[offset + 1], image.pixels[offset + 2], image.pixels[offset + 3]};
}

void writePixel(TerrainAtlasImage& image, std::uint32_t x, std::uint32_t y, const std::array<std::uint8_t, 4>& value) {
    const auto offset = (std::size_t(y) * image.width + x) * 4;
    std::copy(value.begin(), value.end(), image.pixels.begin() + std::ptrdiff_t(offset));
}

void writeFloat(TerrainAtlasImage& image, std::uint32_t x, std::uint32_t y, float value) {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    writePixel(image, x, y,
               {std::uint8_t(bits), std::uint8_t(bits >> 8), std::uint8_t(bits >> 16), std::uint8_t(bits >> 24)});
}
}  // namespace

Result<TerrainMaterialAtlases> buildTerrainMaterialAtlases(const asset::EvpackResourceReader& reader,
                                                           const LoadedTerrainMaterial&       material,
                                                           const asset::EvpackCapabilities&   capabilities,
                                                           const TerrainMaterialAtlasLimits&  limits) {
    if (material.layers.empty() || material.layers.size() > limits.maximumLayers || limits.maximumLayers > 16)
        return failure<TerrainMaterialAtlases>(DiagnosticCode::InvalidArgument,
                                               "terrain atlas layer count is outside [1,16]");
    TerrainMaterialAtlases result;
    auto                   holes = decodeOptional(reader, material.holesAsset, capabilities, limits.image);
    if (!holes) return Result<TerrainMaterialAtlases>::failure(holes.status());
    if (holes.value()) {
        const auto& image = *holes.value();
        result.holes      = {image.width, image.height,
                             std::vector<std::uint8_t>(
                            image.pixels.begin(),
                            image.pixels.begin() + std::ptrdiff_t(std::uint64_t(image.width) * image.height * 4))};
    } else {
        result.holes = solid({255, 255, 255, 255});
    }
    const std::size_t groupCount = (material.layers.size() + 3) / 4;
    result.groups.reserve(groupCount);
    std::uint64_t outputBytes = result.holes.pixels.size();
    for (std::size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex) {
        struct Images {
            std::optional<asset::DecodedEvpackImage> albedo, normal, mask;
        } images[4];
        const std::size_t first     = groupIndex * 4;
        const std::size_t count     = std::min<std::size_t>(4, material.layers.size() - first);
        std::uint32_t     tileWidth = 1, tileHeight = 1;
        for (std::size_t slot = 0; slot < count; ++slot) {
            const auto& layer  = material.layers[first + slot];
            auto        albedo = decodeOptional(reader, layer.diffuseAsset, capabilities, limits.image);
            auto        normal = decodeOptional(reader, layer.normalAsset, capabilities, limits.image);
            auto        mask   = decodeOptional(reader, layer.maskAsset, capabilities, limits.image);
            if (!albedo || !normal || !mask)
                return Result<TerrainMaterialAtlases>::failure(!albedo   ? albedo.status()
                                                               : !normal ? normal.status()
                                                                         : mask.status());
            images[slot] = {std::move(albedo).takeValue(), std::move(normal).takeValue(), std::move(mask).takeValue()};
            for (const auto* image : {&images[slot].albedo, &images[slot].normal, &images[slot].mask}) {
                if (!*image) continue;
                tileWidth  = std::max(tileWidth, (*image)->width);
                tileHeight = std::max(tileHeight, (*image)->height);
            }
        }
        if (tileWidth > limits.maximumTileDimension || tileHeight > limits.maximumTileDimension)
            return failure<TerrainMaterialAtlases>(DiagnosticCode::InvalidArgument,
                                                   "terrain atlas tile exceeds dimension budget",
                                                   std::to_string(groupIndex));
        const std::uint64_t atlasBytes = std::uint64_t(tileWidth) * tileHeight * 16;
        if (atlasBytes > limits.maximumOutputBytes || outputBytes > limits.maximumOutputBytes - atlasBytes * 3)
            return failure<TerrainMaterialAtlases>(DiagnosticCode::InvalidArgument,
                                                   "terrain atlas output exceeds byte budget");
        TerrainMaterialAtlasGroup group;
        group.firstLayer = std::uint32_t(first);
        group.layerCount = std::uint32_t(count);
        group.tileWidth  = tileWidth;
        group.tileHeight = tileHeight;
        for (auto* atlas : {&group.albedo, &group.normal, &group.mask}) {
            atlas->width  = tileWidth * 2;
            atlas->height = tileHeight * 2;
            atlas->pixels.resize(std::size_t(atlas->width) * atlas->height * 4);
        }
        for (std::size_t slot = 0; slot < 4; ++slot) {
            const RuntimeTerrainLayer* layer = slot < count ? &material.layers[first + slot] : nullptr;
            place(group.albedo, images[slot].albedo ? &*images[slot].albedo : nullptr, std::uint32_t(slot),
                  {255, 255, 255, 255}, layer, false, false);
            place(group.normal, images[slot].normal ? &*images[slot].normal : nullptr, std::uint32_t(slot),
                  {128, 128, 255, 255}, layer, true, false);
            place(group.mask, images[slot].mask ? &*images[slot].mask : nullptr, std::uint32_t(slot),
                  {255, 255, 255, 255}, layer, false, true);
        }
        auto control = decodeOptional(reader, material.controlAssets[groupIndex], capabilities, limits.image);
        if (!control) return Result<TerrainMaterialAtlases>::failure(control.status());
        if (control.value()) {
            const auto& image = *control.value();
            group.control     = {image.width, image.height,
                                 std::vector<std::uint8_t>(
                                 image.pixels.begin(),
                                 image.pixels.begin() + std::ptrdiff_t(std::uint64_t(image.width) * image.height * 4))};
        } else if (groupIndex == 0) {
            group.control = solid({255, 0, 0, 0});
        } else {
            return failure<TerrainMaterialAtlases>(DiagnosticCode::NotFound, "terrain layer group has no control image",
                                                   std::to_string(groupIndex));
        }
        outputBytes += atlasBytes * 3 + group.control.pixels.size();
        if (outputBytes > limits.maximumOutputBytes)
            return failure<TerrainMaterialAtlases>(DiagnosticCode::InvalidArgument,
                                                   "terrain atlas output exceeds byte budget");
        result.groups.push_back(std::move(group));
    }
    return Result<TerrainMaterialAtlases>::success(std::move(result));
}

Result<PackedTerrainMaterialAtlases> packTerrainMaterialAtlases(const TerrainMaterialAtlases&     atlases,
                                                                const LoadedTerrainMaterial&      material,
                                                                const TerrainMaterialAtlasLimits& limits) {
    if (material.layers.empty() || material.layers.size() > 16 || material.layers.size() > limits.maximumLayers ||
        atlases.groups.size() != (material.layers.size() + 3) / 4 || atlases.holes.width == 0 ||
        atlases.holes.height == 0 ||
        atlases.holes.pixels.size() != std::size_t(atlases.holes.width) * atlases.holes.height * 4)
        return failure<PackedTerrainMaterialAtlases>(DiagnosticCode::InvalidArgument,
                                                     "terrain atlas set is incomplete");
    std::uint32_t tileWidth = 1, tileHeight = 1, controlWidth = atlases.holes.width,
                  controlHeight = atlases.holes.height;
    for (const auto& group : atlases.groups) {
        const auto groupIndex     = std::size_t(&group - atlases.groups.data());
        const auto expectedLayers = std::min<std::size_t>(4, material.layers.size() - groupIndex * 4);
        if (group.tileWidth == 0 || group.tileHeight == 0 || group.control.width == 0 || group.control.height == 0 ||
            group.firstLayer != groupIndex * 4 || group.layerCount != expectedLayers ||
            group.control.pixels.size() != std::size_t(group.control.width) * group.control.height * 4)
            return failure<PackedTerrainMaterialAtlases>(DiagnosticCode::InvalidArgument,
                                                         "terrain atlas group is incomplete");
        for (const auto* image : {&group.albedo, &group.normal, &group.mask})
            if (image->width != group.tileWidth * 2 || image->height != group.tileHeight * 2 ||
                image->pixels.size() != std::size_t(image->width) * image->height * 4)
                return failure<PackedTerrainMaterialAtlases>(DiagnosticCode::InvalidArgument,
                                                             "terrain layer atlas dimensions are invalid");
        tileWidth     = std::max(tileWidth, group.tileWidth);
        tileHeight    = std::max(tileHeight, group.tileHeight);
        controlWidth  = std::max(controlWidth, group.control.width);
        controlHeight = std::max(controlHeight, group.control.height);
    }
    const std::uint64_t outputBytes = std::uint64_t(tileWidth) * tileHeight * 4 * 4 * 4 * 3 +
                                      std::uint64_t(controlWidth) * controlHeight * 3 * 2 * 4 + 7 * 16 * 4;
    if (tileWidth > limits.maximumTileDimension || tileHeight > limits.maximumTileDimension ||
        controlWidth > limits.maximumTileDimension || controlHeight > limits.maximumTileDimension ||
        outputBytes > limits.maximumOutputBytes)
        return failure<PackedTerrainMaterialAtlases>(DiagnosticCode::InvalidArgument,
                                                     "packed terrain atlas exceeds output limits");
    PackedTerrainMaterialAtlases result;
    result.layerCount = std::uint32_t(material.layers.size());
    result.hasHoles   = material.holesAsset.has_value();
    for (auto* image : {&result.albedo, &result.normal, &result.mask}) {
        image->width  = tileWidth * 4;
        image->height = tileHeight * 4;
        image->pixels.resize(std::size_t(image->width) * image->height * 4);
    }
    for (std::uint32_t layer = 0; layer < result.layerCount; ++layer) {
        const auto& group      = atlases.groups[layer / 4];
        const auto  sourceSlot = layer % 4;
        for (std::uint32_t y = 0; y < tileHeight; ++y) {
            for (std::uint32_t x = 0; x < tileWidth; ++x) {
                const float sourceU      = (float(sourceSlot & 1u) + (float(x) + .5f) / tileWidth) * .5f;
                const float sourceV      = (float(sourceSlot >> 1u) + (float(y) + .5f) / tileHeight) * .5f;
                const auto  destinationX = (layer & 3u) * tileWidth + x;
                const auto  destinationY = (layer >> 2u) * tileHeight + y;
                writePixel(result.albedo, destinationX, destinationY, sampleImage(group.albedo, sourceU, sourceV));
                writePixel(result.normal, destinationX, destinationY, sampleImage(group.normal, sourceU, sourceV));
                writePixel(result.mask, destinationX, destinationY, sampleImage(group.mask, sourceU, sourceV));
            }
        }
    }
    result.controls = {controlWidth * 3, controlHeight * 2,
                       std::vector<std::uint8_t>(std::size_t(controlWidth) * controlHeight * 3 * 2 * 4)};
    for (std::uint32_t cell = 0; cell < 5; ++cell) {
        const TerrainAtlasImage& source = cell < atlases.groups.size() ? atlases.groups[cell].control : atlases.holes;
        for (std::uint32_t y = 0; y < controlHeight; ++y)
            for (std::uint32_t x = 0; x < controlWidth; ++x)
                writePixel(result.controls, (cell % 3) * controlWidth + x, (cell / 3) * controlHeight + y,
                           sampleImage(source, (float(x) + .5f) / controlWidth, (float(y) + .5f) / controlHeight));
    }
    result.parameters = {7, 16, std::vector<std::uint8_t>(7 * 16 * 4)};
    for (std::uint32_t layer = 0; layer < result.layerCount; ++layer) {
        const auto& source = material.layers[layer];
        if (!std::isfinite(source.tileScaleMeters[0]) || !std::isfinite(source.tileScaleMeters[1]) ||
            source.tileScaleMeters[0] == 0.f || source.tileScaleMeters[1] == 0.f ||
            !std::isfinite(source.tileOffsetMeters[0]) || !std::isfinite(source.tileOffsetMeters[1]) ||
            !std::isfinite(source.metallic) || !std::isfinite(source.normalScale) || !std::isfinite(source.smoothness))
            return failure<PackedTerrainMaterialAtlases>(DiagnosticCode::InvalidArgument,
                                                         "terrain layer parameters are invalid", std::to_string(layer));
        const std::array<float, 7> values{1.f / source.tileScaleMeters[0],
                                          1.f / source.tileScaleMeters[1],
                                          source.tileOffsetMeters[0] / source.tileScaleMeters[0],
                                          source.tileOffsetMeters[1] / source.tileScaleMeters[1],
                                          source.metallic,
                                          source.normalScale,
                                          source.smoothness};
        for (std::uint32_t parameter = 0; parameter < values.size(); ++parameter)
            writeFloat(result.parameters, parameter, layer, values[parameter]);
    }
    return Result<PackedTerrainMaterialAtlases>::success(std::move(result));
}

Result<void> releaseTerrainMaterialAtlases(graphics::IResourceFactory& factory, TerrainMaterialGpuSet& set) {
    bool released = true;
    for (auto& group : set.groups) {
        for (auto* handle : {&group.albedo, &group.normal, &group.mask, &group.control}) {
            if (*handle && !factory.releaseTexture(*handle)) released = false;
            *handle = nullptr;
        }
    }
    if (set.holes && !factory.releaseTexture(set.holes)) released = false;
    set.holes = nullptr;
    set.groups.clear();
    if (!released) return failure<void>(DiagnosticCode::Failed, "terrain atlas backend release failed");
    return Result<void>::success();
}

Result<TerrainMaterialGpuSet> uploadTerrainMaterialAtlases(graphics::IResourceFactory&   factory,
                                                           const TerrainMaterialAtlases& atlases) {
    TerrainMaterialGpuSet result;
    auto                  upload = [&](const TerrainAtlasImage& image) {
        return factory.newTexture(int(image.width), int(image.height), image.pixels.data(), false, false);
    };
    result.holes = upload(atlases.holes);
    if (!result.holes) return failure<TerrainMaterialGpuSet>(DiagnosticCode::Failed, "terrain holes upload failed");
    result.groups.reserve(atlases.groups.size());
    for (const auto& source : atlases.groups) {
        TerrainMaterialGpuGroup group;
        group.albedo  = upload(source.albedo);
        group.normal  = upload(source.normal);
        group.mask    = upload(source.mask);
        group.control = upload(source.control);
        result.groups.push_back(group);
        if (!group.albedo || !group.normal || !group.mask || !group.control) {
            auto cleanup = releaseTerrainMaterialAtlases(factory, result);
            if (!cleanup) return Result<TerrainMaterialGpuSet>::failure(cleanup.status());
            return failure<TerrainMaterialGpuSet>(DiagnosticCode::Failed, "terrain atlas upload failed");
        }
    }
    return Result<TerrainMaterialGpuSet>::success(std::move(result));
}

Result<void> releasePackedTerrainMaterialAtlases(graphics::IResourceFactory&  factory,
                                                 PackedTerrainMaterialGpuSet& set) {
    bool released = true;
    for (auto* handle : {&set.albedo, &set.normal, &set.mask, &set.controls, &set.parameters}) {
        if (*handle && !factory.releaseTexture(*handle)) released = false;
        *handle = nullptr;
    }
    set.layerCount = 0;
    set.hasHoles   = false;
    if (!released) return failure<void>(DiagnosticCode::Failed, "packed terrain backend release failed");
    return Result<void>::success();
}

Result<PackedTerrainMaterialGpuSet> uploadPackedTerrainMaterialAtlases(graphics::IResourceFactory&         factory,
                                                                       const PackedTerrainMaterialAtlases& atlases) {
    PackedTerrainMaterialGpuSet result;
    auto                        upload = [&](const TerrainAtlasImage& image) {
        return factory.newTexture(int(image.width), int(image.height), image.pixels.data(), false, false);
    };
    result.albedo     = upload(atlases.albedo);
    result.normal     = upload(atlases.normal);
    result.mask       = upload(atlases.mask);
    result.controls   = upload(atlases.controls);
    result.parameters = upload(atlases.parameters);
    result.layerCount = atlases.layerCount;
    result.hasHoles   = atlases.hasHoles;
    if (!result.albedo || !result.normal || !result.mask || !result.controls || !result.parameters) {
        auto cleanup = releasePackedTerrainMaterialAtlases(factory, result);
        if (!cleanup) return Result<PackedTerrainMaterialGpuSet>::failure(cleanup.status());
        return failure<PackedTerrainMaterialGpuSet>(DiagnosticCode::Failed, "packed terrain atlas upload failed");
    }
    return Result<PackedTerrainMaterialGpuSet>::success(std::move(result));
}

Result<void> bindPackedTerrainMaterial(graphics::Shader& shader, const PackedTerrainMaterialGpuSet& gpu) {
    if (!gpu.albedo || !gpu.normal || !gpu.mask || !gpu.controls || !gpu.parameters || gpu.layerCount == 0 ||
        gpu.layerCount > 16)
        return failure<void>(DiagnosticCode::InvalidArgument, "packed terrain GPU set is incomplete");
    for (const auto& [slot, texture] : {std::pair<std::size_t, graphics::Texture*>{0, gpu.albedo},
                                       {1, gpu.normal},
                                       {2, gpu.mask},
                                       {3, gpu.parameters}}) {
        auto bound = shader.setMeshTexture(slot, texture);
        if (!bound) return bound;
    }
    shader.sendVec4("terrainFeatures", 2.f, gpu.hasHoles ? 1.f : 0.f, float(gpu.layerCount), 0.f);
    return Result<void>::success();
}

Result<void> bindTerrainMaterialGroup(graphics::Shader& shader, const TerrainMaterialGpuSet& gpu,
                                      const LoadedTerrainMaterial& material, std::size_t groupIndex) {
    if (groupIndex >= gpu.groups.size() || groupIndex * 4 >= material.layers.size() || !gpu.holes)
        return failure<void>(DiagnosticCode::InvalidArgument, "terrain atlas group index is invalid");
    const auto& group = gpu.groups[groupIndex];
    for (const auto& [slot, texture] : {std::pair<std::size_t, graphics::Texture*>{0, group.albedo},
                                       {1, group.normal},
                                       {2, group.mask},
                                       {3, gpu.holes}}) {
        auto bound = shader.setMeshTexture(slot, texture);
        if (!bound) return bound;
    }
    std::array<float, 4> metallic{}, normalScale{1, 1, 1, 1}, smoothness{};
    for (std::size_t slot = 0; slot < 4; ++slot) {
        const std::size_t    layerIndex = groupIndex * 4 + slot;
        std::array<float, 4> st{};
        if (layerIndex < material.layers.size()) {
            const auto& layer = material.layers[layerIndex];
            st                = {1.f / layer.tileScaleMeters[0], 1.f / layer.tileScaleMeters[1],
                                 layer.tileOffsetMeters[0] / layer.tileScaleMeters[0],
                                 layer.tileOffsetMeters[1] / layer.tileScaleMeters[1]};
            metallic[slot]    = layer.metallic;
            normalScale[slot] = layer.normalScale;
            smoothness[slot]  = layer.smoothness;
        }
        shader.sendVec4("terrainLayer" + std::to_string(slot) + "ST", st[0], st[1], st[2], st[3]);
    }
    shader.sendVec4("terrainMetallic", metallic[0], metallic[1], metallic[2], metallic[3]);
    shader.sendVec4("terrainNormalScale", normalScale[0], normalScale[1], normalScale[2], normalScale[3]);
    shader.sendVec4("terrainSmoothness", smoothness[0], smoothness[1], smoothness[2], smoothness[3]);
    shader.sendVec4("terrainFeatures", 1.f, material.holesAsset ? 1.f : 0.f, float(groupIndex),
                    float(std::min<std::size_t>(4, material.layers.size() - groupIndex * 4)));
    return Result<void>::success();
}
}  // namespace eve::asset_procgen
