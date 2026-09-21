#include "graphics/VegetationFieldGpu.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
#include <vector>
#include "graphics/IResourceFactory.h"
#include "graphics/PbrSurface.h"
#include "graphics/VegetationField.h"

namespace eve::graphics {
namespace {
constexpr uint32_t layerCount       = 9;
constexpr uint64_t uploadByteBudget = 256ull * 1024 * 1024;

Diagnostic invalid(std::string message) {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), {}, {},
                             "graphics.vegetation.extras-atlas");
}

uint16_t floatToHalf(float value) {
    const uint32_t bits     = std::bit_cast<uint32_t>(value);
    const uint32_t sign     = (bits >> 16) & 0x8000u;
    const uint32_t mantissa = bits & 0x7fffffu;
    const int32_t  exponent = int32_t((bits >> 23) & 0xffu) - 127;
    if (exponent < -24) return uint16_t(sign);
    if (exponent < -14) {
        const uint32_t shift        = uint32_t(-exponent - 1);
        uint32_t       halfMantissa = (mantissa | 0x800000u) >> shift;
        const uint32_t remainder    = (mantissa | 0x800000u) & ((1u << shift) - 1u);
        const uint32_t halfway      = 1u << (shift - 1u);
        if (remainder > halfway || (remainder == halfway && (halfMantissa & 1u))) ++halfMantissa;
        return uint16_t(sign | halfMantissa);
    }
    uint32_t       halfExponent = uint32_t(exponent + 15);
    uint32_t       halfMantissa = mantissa >> 13;
    const uint32_t remainder    = mantissa & 0x1fffu;
    if (remainder > 0x1000u || (remainder == 0x1000u && (halfMantissa & 1u))) {
        if (++halfMantissa == 0x400u) {
            halfMantissa = 0;
            ++halfExponent;
        }
    }
    return uint16_t(sign | (halfExponent << 10) | halfMantissa);
}

bool finite(glm::vec3 value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
}  // namespace

Result<VegetationGpuAtlas> uploadAtlas(IResourceFactory& factory, const VegetationField& field,
                                       VegetationChannel channel, glm::vec3 center, glm::vec3 extent, uint32_t width,
                                       uint32_t height, bool snapToTexel) {
    const uint64_t texels = uint64_t(width) * height * layerCount;
    if (!finite(center) || !finite(extent) || extent.x < 1e-4f || extent.y < 1e-4f || extent.z < 1e-4f || !width ||
        !height || width > 2048 || height > 2048 || texels > uploadByteBudget / (4 * sizeof(uint16_t)))
        return Result<VegetationGpuAtlas>::failure(invalid("invalid atlas geometry or upload budget exceeded"));

    const double dx = 2.0 * extent.x / width;
    const double dz = 2.0 * extent.z / height;
    if (snapToTexel) {
        center.x = float(std::round(double(center.x) / dx) * dx);
        center.z = float(std::round(double(center.z) / dz) * dz);
    }
    if (!finite(center) || !finite(center + extent) || !finite(center - extent))
        return Result<VegetationGpuAtlas>::failure(invalid("atlas bounds overflow"));

    const uint64_t sourceRevision = field.revision();
    try {
        std::vector<uint16_t> pixels(size_t(texels) * 4);
        for (uint32_t layer = 0; layer < layerCount; ++layer) {
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < width; ++x) {
                    const glm::vec3        position(float(double(center.x) - extent.x + (x + 0.5) * dx), center.y,
                                                    float(double(center.z) - extent.z + (y + 0.5) * dz));
                    std::array<uint8_t, 4> selected{};
                    selected[size_t(channel)] = uint8_t(layer);
                    auto sample               = field.sample(position, selected);
                    if (!sample.ok()) return Result<VegetationGpuAtlas>::failure(sample.status());
                    const auto&     sampled = sample.value();
                    const glm::vec4 value   = channel == VegetationChannel::Color    ? sampled.color
                                              : channel == VegetationChannel::Extras ? sampled.extras
                                              : channel == VegetationChannel::Motion ? sampled.motion
                                                                                     : sampled.vertex;
                    if (std::abs(value.x) > 65504.f || std::abs(value.y) > 65504.f || std::abs(value.z) > 65504.f ||
                        std::abs(value.w) > 65504.f)
                        return Result<VegetationGpuAtlas>::failure(
                            invalid("vegetation value exceeds finite RGBA16F range"));
                    const size_t offset = ((size_t(layer) * height + y) * width + x) * 4;
                    pixels[offset + 0]  = floatToHalf(value.x);
                    pixels[offset + 1]  = floatToHalf(value.y);
                    pixels[offset + 2]  = floatToHalf(value.z);
                    pixels[offset + 3]  = floatToHalf(value.w);
                }
            }
        }
        if (field.revision() != sourceRevision)
            return Result<VegetationGpuAtlas>::failure(invalid("field changed while baking vegetation atlas"));
        auto uploaded = factory.newTextureArrayRgba16f(width, height, layerCount, pixels);
        if (!uploaded.ok()) return Result<VegetationGpuAtlas>::failure(uploaded.status());
        return Result<VegetationGpuAtlas>::success(
            {uploaded.value(), sourceRevision, width, height, layerCount, center, extent});
    } catch (const std::bad_alloc&) {
        return Result<VegetationGpuAtlas>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "vegetation atlas allocation failed", {}, {}, "graphics.vegetation.atlas"));
    }
}

Result<VegetationExtrasGpuAtlas> uploadVegetationExtrasAtlas(IResourceFactory& factory, const VegetationField& field,
                                                             glm::vec3 center, glm::vec3 extent, uint32_t width,
                                                             uint32_t height, bool snapToTexel) {
    return uploadAtlas(factory, field, VegetationChannel::Extras, center, extent, width, height, snapToTexel);
}

Result<VegetationColorsGpuAtlas> uploadVegetationColorsAtlas(IResourceFactory& factory, const VegetationField& field,
                                                             glm::vec3 center, glm::vec3 extent, uint32_t width,
                                                             uint32_t height, bool snapToTexel) {
    return uploadAtlas(factory, field, VegetationChannel::Color, center, extent, width, height, snapToTexel);
}

Result<VegetationMotionGpuAtlas> uploadVegetationMotionAtlas(IResourceFactory& factory, const VegetationField& field,
                                                             glm::vec3 center, glm::vec3 extent, uint32_t width,
                                                             uint32_t height, bool snapToTexel) {
    return uploadAtlas(factory, field, VegetationChannel::Motion, center, extent, width, height, snapToTexel);
}

Result<VegetationVertexGpuAtlas> uploadVegetationVertexAtlas(IResourceFactory& factory, const VegetationField& field,
                                                             glm::vec3 center, glm::vec3 extent, uint32_t width,
                                                             uint32_t height, bool snapToTexel) {
    return uploadAtlas(factory, field, VegetationChannel::Vertex, center, extent, width, height, snapToTexel);
}

Result<void> releaseVegetationGpuFieldSet(IResourceFactory& factory, VegetationGpuFieldSet& set) {
    bool failed = false;
    for (auto* atlas : {&set.extras, &set.colors, &set.motion, &set.vertex}) {
        if (!atlas->texture) continue;
        if (factory.releaseTexture(atlas->texture))
            atlas->texture = nullptr;
        else
            failed = true;
    }
    if (failed)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed,
                                                       "vegetation GPU field texture release failed", {}, {},
                                                       "graphics.vegetation.atlas"));
    return Result<void>::success();
}

Result<VegetationGpuFieldSet> uploadVegetationGpuFieldSet(IResourceFactory& factory, const VegetationField& field,
                                                          glm::vec3 center, glm::vec3 extent, uint32_t width,
                                                          uint32_t height, bool snapToTexel) {
    VegetationGpuFieldSet candidate;
    auto                  fail = [&](const Status& status) {
        auto released = releaseVegetationGpuFieldSet(factory, candidate);
        if (!released)
            return Result<VegetationGpuFieldSet>::failure(
                Diagnostic::error(DiagnosticCode::Failed, "vegetation GPU field upload rollback failed", {}, {},
                                                   "graphics.vegetation.atlas"));
        return Result<VegetationGpuFieldSet>::failure(status);
    };
    auto extras = uploadVegetationExtrasAtlas(factory, field, center, extent, width, height, snapToTexel);
    if (!extras) return Result<VegetationGpuFieldSet>::failure(extras.status());
    candidate.extras = std::move(extras).takeValue();
    auto colors = uploadVegetationColorsAtlas(factory, field, candidate.extras.center, extent, width, height, false);
    if (!colors) return fail(colors.status());
    candidate.colors = std::move(colors).takeValue();
    auto motion = uploadVegetationMotionAtlas(factory, field, candidate.extras.center, extent, width, height, false);
    if (!motion) return fail(motion.status());
    candidate.motion = std::move(motion).takeValue();
    auto vertex = uploadVegetationVertexAtlas(factory, field, candidate.extras.center, extent, width, height, false);
    if (!vertex) return fail(vertex.status());
    candidate.vertex = std::move(vertex).takeValue();
    if (candidate.extras.fieldRevision != field.revision()) {
        auto released = releaseVegetationGpuFieldSet(factory, candidate);
        if (!released)
            return Result<VegetationGpuFieldSet>::failure(
                Diagnostic::error(DiagnosticCode::Failed, "vegetation GPU field upload rollback failed", {}, {},
                                  "graphics.vegetation.atlas"));
        return Result<VegetationGpuFieldSet>::failure(invalid("field changed during set upload"));
    }
    return Result<VegetationGpuFieldSet>::success(std::move(candidate));
}

namespace {
Result<VegetationGpuAtlas> uploadBakedChannel(IResourceFactory& factory,
                                               std::span<const VegetationChannelAtlas, layerCount> layers,
                                               std::uint64_t sourceRevision) {
    const auto& geometry = layers.front();
    const std::uint64_t texels = std::uint64_t(geometry.width) * geometry.height * layerCount;
    try {
        std::vector<std::uint16_t> pixels(std::size_t(texels) * 4);
        for (std::size_t layer = 0; layer < layerCount; ++layer) {
            const auto& values = layers[layer].pixels;
            for (std::size_t index = 0; index < values.size(); ++index) {
                const auto value = values[index];
                if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z) ||
                    !std::isfinite(value.w) || std::abs(value.x) > 65504.f || std::abs(value.y) > 65504.f ||
                    std::abs(value.z) > 65504.f || std::abs(value.w) > 65504.f)
                    return Result<VegetationGpuAtlas>::failure(invalid("pre-baked vegetation value exceeds RGBA16F range"));
                const std::size_t offset = (layer * values.size() + index) * 4;
                pixels[offset] = floatToHalf(value.x);
                pixels[offset + 1] = floatToHalf(value.y);
                pixels[offset + 2] = floatToHalf(value.z);
                pixels[offset + 3] = floatToHalf(value.w);
            }
        }
        auto uploaded = factory.newTextureArrayRgba16f(geometry.width, geometry.height, layerCount, pixels);
        if (!uploaded) return Result<VegetationGpuAtlas>::failure(uploaded.status());
        return Result<VegetationGpuAtlas>::success({uploaded.value(), sourceRevision, geometry.width, geometry.height,
                                                    layerCount, geometry.center, geometry.extent});
    } catch (const std::bad_alloc&) {
        return Result<VegetationGpuAtlas>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "pre-baked vegetation atlas allocation failed", {}, {},
                              "graphics.vegetation.atlas"));
    }
}

Result<void> validateBakedChannel(std::span<const VegetationChannelAtlas, layerCount> layers) {
    const auto& first = layers.front();
    const std::uint64_t pixels = std::uint64_t(first.width) * first.height;
    if (first.width == 0 || first.height == 0 || first.width > 2048 || first.height > 2048 ||
        !finite(first.center) || !finite(first.extent) || first.extent.x < 1e-4f || first.extent.y < 1e-4f ||
        first.extent.z < 1e-4f)
        return Result<void>::failure(invalid("pre-baked vegetation channel geometry is invalid"));
    for (const auto& layer : layers) {
        if (layer.width != first.width || layer.height != first.height || layer.center != first.center ||
            layer.extent != first.extent || layer.pixels.size() != pixels)
            return Result<void>::failure(invalid("pre-baked vegetation channel layers are incoherent"));
    }
    return Result<void>::success();
}
}  // namespace

Result<VegetationGpuFieldSet> uploadVegetationGpuFieldSet(
    IResourceFactory& factory, std::span<const VegetationAtlas, 9> layers, std::uint64_t sourceRevision) {
    const auto& first = layers.front();
    const std::uint64_t pixels = std::uint64_t(first.width) * first.height;
    if (sourceRevision == 0 || first.width == 0 || first.height == 0 || first.width > 2048 || first.height > 2048 ||
        !finite(first.center) || !finite(first.extent) || first.extent.x < 1e-4f || first.extent.y < 1e-4f ||
        first.extent.z < 1e-4f || pixels * layerCount > uploadByteBudget / (4 * sizeof(std::uint16_t)))
        return Result<VegetationGpuFieldSet>::failure(invalid("pre-baked vegetation atlas geometry is invalid"));
    for (const auto& layer : layers) {
        if (layer.width != first.width || layer.height != first.height || layer.center != first.center ||
            layer.extent != first.extent || std::any_of(layer.channels.begin(), layer.channels.end(),
                                                        [&](const auto& values) { return values.size() != pixels; }))
            return Result<VegetationGpuFieldSet>::failure(invalid("pre-baked vegetation atlas layers are incoherent"));
    }
    try {
        std::array<VegetationChannelAtlas, layerCount> colors, extras, motion, vertex;
        for (std::size_t layer = 0; layer < layerCount; ++layer) {
            auto copy = [&](VegetationChannelAtlas& destination, std::size_t channel) {
                destination.width = layers[layer].width;
                destination.height = layers[layer].height;
                destination.center = layers[layer].center;
                destination.extent = layers[layer].extent;
                destination.pixels = layers[layer].channels[channel];
            };
            copy(colors[layer], 0);
            copy(extras[layer], 1);
            copy(motion[layer], 2);
            copy(vertex[layer], 3);
        }
        return uploadVegetationGpuFieldSet(factory, colors, extras, motion, vertex, sourceRevision);
    } catch (const std::bad_alloc&) {
        return Result<VegetationGpuFieldSet>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "pre-baked vegetation channel projection allocation failed",
                              {}, {}, "graphics.vegetation.atlas"));
    }
}

Result<VegetationGpuFieldSet> uploadVegetationGpuFieldSet(
    IResourceFactory& factory, std::span<const VegetationChannelAtlas, 9> colors,
    std::span<const VegetationChannelAtlas, 9> extras, std::span<const VegetationChannelAtlas, 9> motion,
    std::span<const VegetationChannelAtlas, 9> vertex, std::uint64_t sourceRevision) {
    if (sourceRevision == 0)
        return Result<VegetationGpuFieldSet>::failure(invalid("pre-baked vegetation revision must be nonzero"));
    for (const auto channel : {colors, extras, motion, vertex}) {
        auto valid = validateBakedChannel(channel);
        if (!valid) return Result<VegetationGpuFieldSet>::failure(valid.status());
    }
    std::uint64_t combinedTexels = 0;
    for (const auto channel : {colors, extras, motion, vertex}) {
        const auto& first = channel.front();
        const std::uint64_t channelTexels = std::uint64_t(first.width) * first.height * layerCount;
        if (channelTexels > std::numeric_limits<std::uint64_t>::max() - combinedTexels)
            return Result<VegetationGpuFieldSet>::failure(invalid("pre-baked vegetation upload size overflow"));
        combinedTexels += channelTexels;
    }
    if (combinedTexels > uploadByteBudget / (4 * sizeof(std::uint16_t)))
        return Result<VegetationGpuFieldSet>::failure(invalid("pre-baked vegetation upload budget exceeded"));

    VegetationGpuFieldSet candidate;
    auto rollback = [&](const Status& status) {
        auto released = releaseVegetationGpuFieldSet(factory, candidate);
        if (!released)
            return Result<VegetationGpuFieldSet>::failure(
                Diagnostic::error(DiagnosticCode::Failed, "pre-baked vegetation GPU upload rollback failed", {}, {},
                                  "graphics.vegetation.atlas"));
        return Result<VegetationGpuFieldSet>::failure(status);
    };
    auto extrasUpload = uploadBakedChannel(factory, extras, sourceRevision);
    if (!extrasUpload) return Result<VegetationGpuFieldSet>::failure(extrasUpload.status());
    candidate.extras = std::move(extrasUpload).takeValue();
    auto colorsUpload = uploadBakedChannel(factory, colors, sourceRevision);
    if (!colorsUpload) return rollback(colorsUpload.status());
    candidate.colors = std::move(colorsUpload).takeValue();
    auto motionUpload = uploadBakedChannel(factory, motion, sourceRevision);
    if (!motionUpload) return rollback(motionUpload.status());
    candidate.motion = std::move(motionUpload).takeValue();
    auto vertexUpload = uploadBakedChannel(factory, vertex, sourceRevision);
    if (!vertexUpload) return rollback(vertexUpload.status());
    candidate.vertex = std::move(vertexUpload).takeValue();
    return Result<VegetationGpuFieldSet>::success(std::move(candidate));
}

namespace {
Result<void> bindVegetationGpuFieldsAtRevision(PbrSurface& surface, std::uint64_t expectedRevision,
                                     const VegetationExtrasGpuAtlas& extras, const VegetationColorsGpuAtlas& colors,
                                     const VegetationMotionGpuAtlas& motion, const VegetationVertexGpuAtlas& vertex,
                                     const VegetationGpuRuntime& runtime) {
    const auto validAtlas = [&](const VegetationGpuAtlas& atlas) {
        return atlas.texture && atlas.fieldRevision == extras.fieldRevision && atlas.width && atlas.height &&
               atlas.width <= 2048 && atlas.height <= 2048 && atlas.layers == layerCount && finite(atlas.center) &&
               finite(atlas.extent) && atlas.extent.x >= 1e-4f && atlas.extent.y >= 1e-4f && atlas.extent.z >= 1e-4f;
    };
    if (!validAtlas(extras) || !validAtlas(colors) || !validAtlas(motion) || !validAtlas(vertex) ||
        extras.fieldRevision != expectedRevision || extras.layers != layerCount ||
        (runtime.enableMotion && !runtime.motionNoise) ||
        !finite(glm::vec3(runtime.motionDirection[0], 0, runtime.motionDirection[1])) ||
        !finite(glm::vec3(runtime.worldOrigin[0], runtime.worldOrigin[1], runtime.worldOrigin[2])) ||
        !std::isfinite(runtime.time) || std::abs(runtime.time) > 1e12 || !std::isfinite(runtime.globalBending) ||
        !std::isfinite(runtime.globalBranch) || !std::isfinite(runtime.globalFlutter) ||
        !std::isfinite(runtime.noiseTiling) || !std::isfinite(runtime.motionFadeDistance) ||
        !std::isfinite(runtime.cameraFadeMin) || !std::isfinite(runtime.cameraFadeMax) ||
        !std::isfinite(runtime.fadeNoiseTiling) || runtime.globalBending < 0 || runtime.globalBending > 1000000 ||
        runtime.globalBranch < 0 || runtime.globalBranch > 1000000 || runtime.globalFlutter < 0 ||
        runtime.globalFlutter > 1000000 || runtime.noiseTiling < 0 || runtime.noiseTiling > 1000000 ||
        runtime.motionFadeDistance < 0 || runtime.motionFadeDistance > 1000000 || runtime.cameraFadeMin < 0 ||
        runtime.cameraFadeMin > 1000000 || runtime.cameraFadeMax < 0 || runtime.cameraFadeMax > 1000000 ||
        runtime.fadeNoiseTiling < 0 || runtime.fadeNoiseTiling > 1000000)
        return Result<void>::failure(invalid("incoherent, stale or invalid vegetation GPU field binding"));

    const auto coords = [](const VegetationGpuAtlas& atlas) {
        return std::array<float, 4>{1.f / (2.f * atlas.extent.x), 1.f / (2.f * atlas.extent.z),
                                    .5f - atlas.center.x / (2.f * atlas.extent.x),
                                    .5f - atlas.center.z / (2.f * atlas.extent.z)};
    };
    PbrSurface                 candidate = surface;
    candidate.vegetationExtras.texture   = extras.texture;
    candidate.vegetationExtras.coords    = coords(extras);
    candidate.vegetationExtras.usage.fill(1);
    candidate.vegetationColors.texture = colors.texture;
    candidate.vegetationColors.coords  = coords(colors);
    candidate.vegetationColors.usage.fill(1);
    candidate.vegetationMotion.texture         = motion.texture;
    candidate.vegetationMotion.noise           = runtime.motionNoise;
    candidate.vegetationMotion.coords          = coords(motion);
    candidate.vegetationMotion.globalDirection = runtime.motionDirection;
    candidate.vegetationMotion.worldOrigin     = runtime.worldOrigin;
    candidate.vegetationMotion.time            = runtime.time;
    candidate.vegetationMotion.globalBending   = runtime.globalBending;
    candidate.vegetationMotion.globalBranch    = runtime.globalBranch;
    candidate.vegetationMotion.globalFlutter   = runtime.globalFlutter;
    candidate.vegetationMotion.noiseTiling     = runtime.noiseTiling;
    candidate.vegetationMotion.fadeDistance    = runtime.motionFadeDistance;
    candidate.vegetationMotion.mode =
        runtime.enableMotion ? PbrVegetationMotionMode::Object : PbrVegetationMotionMode::Disabled;
    candidate.vegetationMotion.usage.fill(1);
    candidate.vegetationAlpha.noise         = runtime.fadeNoise;
    candidate.vegetationAlpha.cameraFadeMin = runtime.cameraFadeMin;
    candidate.vegetationAlpha.cameraFadeMax = runtime.cameraFadeMax;
    candidate.vegetationAlpha.noiseTiling   = runtime.fadeNoiseTiling;
    candidate.vegetationVertex.texture      = vertex.texture;
    candidate.vegetationVertex.coords       = coords(vertex);
    candidate.vegetationVertex.source       = PbrVegetationDeformationSource::GpuFields;
    candidate.vegetationVertex.usage.fill(1);
    auto valid = validatePbrSurface(candidate);
    if (!valid) return valid;
    surface = std::move(candidate);
    return Result<void>::success();
}
}  // namespace

Result<void> bindVegetationGpuFields(PbrSurface& surface, const VegetationField& field,
                                     const VegetationExtrasGpuAtlas& extras, const VegetationColorsGpuAtlas& colors,
                                     const VegetationMotionGpuAtlas& motion, const VegetationVertexGpuAtlas& vertex,
                                     const VegetationGpuRuntime& runtime) {
    return bindVegetationGpuFieldsAtRevision(surface, field.revision(), extras, colors, motion, vertex, runtime);
}

Result<void> bindVegetationGpuFields(PbrSurface& surface, const VegetationField& field,
                                     const VegetationGpuFieldSet& set, const VegetationGpuRuntime& runtime) {
    return bindVegetationGpuFields(surface, field, set.extras, set.colors, set.motion, set.vertex, runtime);
}

Result<void> bindVegetationGpuFields(PbrSurface& surface, const VegetationGpuFieldSet& set,
                                     std::uint64_t expectedRevision, const VegetationGpuRuntime& runtime) {
    return bindVegetationGpuFieldsAtRevision(surface, expectedRevision, set.extras, set.colors, set.motion, set.vertex,
                                             runtime);
}

}  // namespace eve::graphics
