#include "graphics/PrimitiveTypes.h"

#include "common/Assert.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace eve::graphics {

std::uint32_t resolveRadialSegments(const RadialTessellation& tessellation, float projectedRadiusPixels) {
    const bool validError = std::isfinite(tessellation.maxScreenErrorPixels) && tessellation.maxScreenErrorPixels > 0.f;
    EV_PARAM_CHECK(validError, "radial tessellation screen error must be finite and positive");
    if (tessellation.customSegments != 0) {
        const bool validCustom = tessellation.customSegments >= 3 && tessellation.customSegments <= 4096;
        EV_PARAM_CHECK(validCustom, "custom radial segment count must be in [3, 4096]");
        return tessellation.customSegments;
    }
    switch (tessellation.quality) {
        case PrimitiveQuality::Low: return 16;
        case PrimitiveQuality::Medium: return 32;
        case PrimitiveQuality::High: return 64;
        case PrimitiveQuality::Adaptive: break;
    }
    const bool validRadius = std::isfinite(projectedRadiusPixels) && projectedRadiusPixels >= 0.f;
    EV_PARAM_CHECK(validRadius, "adaptive radial projected radius must be finite and non-negative");
    if (projectedRadiusPixels <= tessellation.maxScreenErrorPixels) return 8;
    const float cosine    = glm::clamp(1.f - tessellation.maxScreenErrorPixels / projectedRadiusPixels, -1.f, 1.f);
    const float halfAngle = std::acos(cosine);
    if (halfAngle <= 1e-6f) return 256;
    return std::clamp(static_cast<std::uint32_t>(std::ceil(glm::pi<float>() / halfAngle)), 8u, 256u);
}

void DashPattern::validate() const {
    EV_PARAM_CHECK(!intervals.empty(), "dash intervals must not be empty");
    EV_PARAM_CHECK(intervals.size() % 2 == 0, "dash intervals must contain draw/gap pairs");
    EV_PARAM_CHECK(std::isfinite(phase), "dash phase must be finite");
    for (float interval : intervals) {
        const bool validInterval = std::isfinite(interval) && interval > 0.f;
        EV_PARAM_CHECK(validInterval, "dash intervals must be finite and greater than zero");
    }
}

float DashPattern::period() const {
    validate();
    return std::accumulate(intervals.begin(), intervals.end(), 0.f);
}

void StrokeStyle::validate() const {
    const bool validWidth = std::isfinite(width) && width >= 0.f;
    EV_PARAM_CHECK(validWidth, "stroke width must be finite and non-negative");
    const bool validMiterLimit = std::isfinite(miterLimit) && miterLimit > 0.f;
    EV_PARAM_CHECK(validMiterLimit, "stroke miter limit must be finite and greater than zero");
    if (dash) dash->validate();
}

void PrimitivePaint::validate() const { stroke.validate(); }

void SceneDrawContext::validate() const {
    const bool validViewport = viewportSize.x > 0 && viewportSize.y > 0;
    EV_PARAM_CHECK(validViewport, "scene primitive viewport must be positive");
    const bool validNearPlane = std::isfinite(nearPlane) && nearPlane > 0.f;
    EV_PARAM_CHECK(validNearPlane, "scene primitive near plane must be finite and positive");
    const bool validFarPlane = std::isfinite(farPlane) && farPlane > nearPlane;
    EV_PARAM_CHECK(validFarPlane, "scene primitive far plane must be finite and greater than near plane");
}

}  // namespace eve::graphics
