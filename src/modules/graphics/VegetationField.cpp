#include "graphics/VegetationField.h"

#include <algorithm>
#include <cmath>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <limits>
#include <new>

namespace eve::graphics {
namespace {
bool finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool finite(glm::vec4 v) { return finite(glm::vec3(v)) && std::isfinite(v.w); }
bool unit(float v) { return std::isfinite(v) && v >= 0.f && v <= 1.f; }
bool layersValid(std::array<uint8_t, 4> layers) {
    return std::all_of(layers.begin(), layers.end(), [](uint8_t layer) { return layer <= 8; });
}
Diagnostic invalid(std::string message) {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), {}, {}, "graphics.vegetation");
}
Diagnostic exhausted() {
    return Diagnostic::error(DiagnosticCode::Failed, "vegetation allocation failed", {}, {}, "graphics.vegetation");
}
glm::vec4 maskAt(const VegetationMask& mask, float u, float v) {
    if (mask.pixels.empty()) return glm::vec4(1.f);
    const float x  = std::clamp(u * mask.width - 0.5f, 0.f, float(mask.width - 1));
    const float y  = std::clamp(v * mask.height - 0.5f, 0.f, float(mask.height - 1));
    const auto  x0 = uint32_t(x), y0 = uint32_t(y);
    const auto  x1 = std::min(x0 + 1, mask.width - 1), y1 = std::min(y0 + 1, mask.height - 1);
    return glm::mix(
        glm::mix(mask.pixels[size_t(y0) * mask.width + x0], mask.pixels[size_t(y0) * mask.width + x1], x - x0),
        glm::mix(mask.pixels[size_t(y1) * mask.width + x0], mask.pixels[size_t(y1) * mask.width + x1], x - x0), y - y0);
}
glm::vec4 seasonalValue(const VegetationElement& e, float season) {
    if (!e.seasonal) return e.value;
    const float wrapped = season == 4.f ? 0.f : season;
    const auto  i       = unsigned(wrapped);
    const float f       = wrapped - i;
    return glm::mix(e.seasons[i], e.seasons[(i + 1) % 4], f * f * (3.f - 2.f * f));
}
glm::vec4 blend(glm::vec4 previous, glm::vec4 target, float weight, VegetationBlend mode) {
    switch (mode) {
        case VegetationBlend::Replace: return glm::mix(previous, target, weight);
        case VegetationBlend::Add: return previous + target * weight;
        case VegetationBlend::Multiply: return previous * glm::mix(glm::vec4(1.f), target, weight);
        case VegetationBlend::Minimum: return glm::mix(previous, glm::min(previous, target), weight);
        case VegetationBlend::Maximum: return glm::mix(previous, glm::max(previous, target), weight);
    }
    return previous;
}
}  // namespace

Result<std::vector<VegetationElement>> VegetationField::snapshotElements() const {
    try {
        return Result<std::vector<VegetationElement>>::success(elements_);
    } catch (const std::bad_alloc&) {
        return Result<std::vector<VegetationElement>>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "vegetation element snapshot allocation failed", {},
                              {}, "graphics.vegetation"));
    }
}

Result<void> VegetationField::replace(const VegetationGlobals& globals, std::span<const VegetationElement> elements) {
    if (revision_ == std::numeric_limits<uint64_t>::max())
        return Result<void>::failure(invalid("field publication revision exhausted"));
    if (!finite(globals.color) || !finite(globals.extras) || !finite(globals.motion) || !finite(globals.vertex) ||
        !std::isfinite(globals.season) || globals.season < 0.f || globals.season > 4.f || elements.size() > 4096)
        return Result<void>::failure(invalid("invalid globals or element count exceeds 4096"));
    size_t totalPixels = 0;
    for (const auto& e : elements) {
        if (unsigned(e.channel) > unsigned(VegetationChannel::Vertex) ||
            unsigned(e.blend) > unsigned(VegetationBlend::Maximum) ||
            unsigned(e.shape) > unsigned(VegetationShape::Box) || !finite(e.center) || !finite(e.extents) ||
            e.extents.x < 1e-4f || e.extents.y < 1e-4f || e.extents.z < 1e-4f || !std::isfinite(e.yaw) ||
            !unit(e.opacity) || !unit(e.edgeFade) || !finite(e.value) || e.layers == 0 || (e.layers & ~0x1ffu))
            return Result<void>::failure(invalid("invalid element shape, channel, blend, layers or parameters"));
        for (const auto& value : e.seasons)
            if (!finite(value)) return Result<void>::failure(invalid("non-finite season value"));
        const auto& m = e.mask;
        if ((m.pixels.empty() && (m.width != 0 || m.height != 0)) ||
            (!m.pixels.empty() && (m.width == 0 || m.height == 0 || m.width > 2048 || m.height > 2048 ||
                                   m.pixels.size() != size_t(m.width) * m.height)))
            return Result<void>::failure(invalid("invalid mask dimensions or pixel count"));
        totalPixels += m.pixels.size();
        if (totalPixels > 4 * 1024 * 1024) return Result<void>::failure(invalid("field mask pixel budget exceeded"));
        for (auto pixel : m.pixels)
            if (!finite(pixel) || !unit(pixel.a)) return Result<void>::failure(invalid("invalid mask pixel"));
    }
    try {
        std::vector<VegetationElement> candidate(elements.begin(), elements.end());
        std::stable_sort(candidate.begin(), candidate.end(),
                         [](const auto& a, const auto& b) { return a.priority < b.priority; });
        elements_.swap(candidate);
        globals_ = globals;
        ++revision_;
        return Result<void>::success();
    } catch (const std::bad_alloc&) {
        return Result<void>::failure(exhausted());
    }
}

VegetationSample VegetationField::evaluate(glm::vec3 position, std::array<uint8_t, 4> layers) const {
    std::array<glm::vec4, 4> values{globals_.color, globals_.extras, globals_.motion, globals_.vertex};
    for (const auto& e : elements_) {
        const size_t channel = size_t(e.channel);
        if (!(e.layers & (1u << layers[channel]))) continue;
        const auto      d = position - e.center;
        const float     c = std::cos(e.yaw), s = std::sin(e.yaw);
        const glm::vec3 q = glm::vec3(c * d.x - s * d.z, d.y, s * d.x + c * d.z) / e.extents;
        if (!finite(q)) {
            const glm::vec4 invalidValue(std::numeric_limits<float>::quiet_NaN());
            return {invalidValue, invalidValue, invalidValue, invalidValue};
        }
        const float distance = e.shape == VegetationShape::Ellipsoid
                                   ? glm::length(q)
                                   : std::max({std::abs(q.x), std::abs(q.y), std::abs(q.z)});
        if (distance >= 1.f) continue;
        const float t      = e.edgeFade > 0.f ? std::clamp((1.f - distance) / e.edgeFade, 0.f, 1.f) : 1.f;
        const auto  mask   = maskAt(e.mask, q.x * 0.5f + 0.5f, q.z * 0.5f + 0.5f);
        auto        target = seasonalValue(e, globals_.season);
        target *= glm::vec4(glm::vec3(mask), 1.f);
        values[channel] = blend(values[channel], target, t * t * (3.f - 2.f * t) * e.opacity * mask.a, e.blend);
    }
    return {values[0], values[1], values[2], values[3]};
}

Result<VegetationSample> VegetationField::sample(glm::vec3 position, std::array<uint8_t, 4> layers) const {
    if (!finite(position) || !layersValid(layers))
        return Result<VegetationSample>::failure(invalid("invalid sample position or layer"));
    auto result = evaluate(position, layers);
    if (!finite(result.color) || !finite(result.extras) || !finite(result.motion) || !finite(result.vertex))
        return Result<VegetationSample>::failure(invalid("field composition overflow"));
    return Result<VegetationSample>::success(result);
}

Result<VegetationAtlas> VegetationField::bake(glm::vec3 center, glm::vec3 extent, uint32_t width, uint32_t height,
                                              std::array<uint8_t, 4> layers, bool snapToTexel) const {
    if (!finite(center) || !finite(extent) || extent.x < 1e-4f || extent.y < 1e-4f || extent.z < 1e-4f || width == 0 ||
        height == 0 || width > 2048 || height > 2048 || !layersValid(layers))
        return Result<VegetationAtlas>::failure(invalid("invalid atlas geometry, dimensions or layer"));
    const double dx = 2.0 * extent.x / width, dz = 2.0 * extent.z / height;
    if (snapToTexel) {
        center.x = float(std::round(double(center.x) / dx) * dx);
        center.z = float(std::round(double(center.z) / dz) * dz);
    }
    if (!finite(center) || !finite(center + extent) || !finite(center - extent))
        return Result<VegetationAtlas>::failure(invalid("atlas bounds overflow"));
    try {
        VegetationAtlas atlas;
        atlas.width  = width;
        atlas.height = height;
        atlas.center = center;
        atlas.extent = extent;
        for (auto& channel : atlas.channels) channel.resize(size_t(width) * height);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                const glm::vec3 p(float(double(center.x) - extent.x + (x + 0.5) * dx), center.y,
                                  float(double(center.z) - extent.z + (y + 0.5) * dz));
                auto            result = sample(p, layers);
                if (!result.ok()) return Result<VegetationAtlas>::failure(result.status());
                const auto&  v           = result.value();
                const size_t index       = size_t(y) * width + x;
                atlas.channels[0][index] = v.color;
                atlas.channels[1][index] = v.extras;
                atlas.channels[2][index] = v.motion;
                atlas.channels[3][index] = v.vertex;
            }
        }
        return Result<VegetationAtlas>::success(std::move(atlas));
    } catch (const std::bad_alloc&) {
        return Result<VegetationAtlas>::failure(exhausted());
    }
}
}  // namespace eve::graphics
