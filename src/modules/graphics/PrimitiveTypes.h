#pragma once

#include "graphics/BlendMode.h"
#include "graphics/Color.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace eve::graphics {

/** @brief Whether a primitive paints its interior, boundary, or both. */
enum class PaintMode : std::uint8_t { Fill, Stroke, FillAndStroke };
/** @brief Shape applied to each open line endpoint. */
enum class LineCap : std::uint8_t { Butt, Square, Round };
/** @brief Shape used where adjacent line segments meet. */
enum class LineJoin : std::uint8_t { Miter, Bevel, Round };
/** @brief Coordinate space in which stroke width is measured. */
enum class WidthSpace : std::uint8_t { ScreenPixels, WorldUnits };
/** @brief Coordinate space in which dash intervals are measured. */
enum class DashSpace : std::uint8_t { ScreenPixels, WorldUnits };
/** @brief Depth behavior of one 3D primitive. */
enum class PrimitiveDepthMode : std::uint8_t { TestAndWrite, TestOnly, Ignore };
/** @brief Face culling used by filled 3D primitives. */
enum class PrimitiveCullMode : std::uint8_t { None, Back, Front };
/** @brief Reusable quality policy for radial primitive tessellation. */
enum class PrimitiveQuality : std::uint8_t { Low, Medium, High, Adaptive };
/** @brief Successful outcome of one atomic primitive recording operation. */
enum class PrimitiveRecordStatus : std::uint8_t { Recorded };

/** @brief Quality and screen-error policy used by circles and curved spatial primitives. */
struct RadialTessellation {
    PrimitiveQuality quality              = PrimitiveQuality::Medium;
    float            maxScreenErrorPixels = 0.75f;
    std::uint32_t    customSegments       = 0;
};

/** @brief Resolves a radial tessellation policy to a validated segment count. */
[[nodiscard]] std::uint32_t resolveRadialSegments(const RadialTessellation& tessellation, float projectedRadiusPixels);

/** @brief Owning alternating draw/gap pattern for stroked primitives. */
struct DashPattern {
    std::vector<float> intervals;
    float              phase = 0.f;
    DashSpace          space = DashSpace::ScreenPixels;

    /** @brief Validates finite positive intervals and an even interval count. */
    void validate() const;
    /** @brief Returns the sum of all draw and gap intervals. */
    [[nodiscard]] float period() const;
};

/** @brief Backend-neutral stroke state shared by 2D and 3D canvases. */
struct StrokeStyle {
    float                      width      = 1.f;
    WidthSpace                 widthSpace = WidthSpace::ScreenPixels;
    LineCap                    cap        = LineCap::Butt;
    LineJoin                   join       = LineJoin::Miter;
    float                      miterLimit = 4.f;
    std::optional<DashPattern> dash;

    /** @brief Validates all scalar and optional dash state. */
    void validate() const;
};

/** @brief Backend-neutral fill and stroke paint. */
struct PrimitivePaint {
    Color       color = Color(1.f, 1.f, 1.f, 1.f);
    PaintMode   mode  = PaintMode::Fill;
    StrokeStyle stroke{};
    BlendMode   blend     = BlendMode::Alpha;
    bool        antialias = true;

    /** @brief Validates paint state used by a draw command. */
    void validate() const;
};

/** @brief Additional scene-pass state carried by every 3D command. */
struct ScenePrimitivePaint : PrimitivePaint {
    PrimitiveDepthMode depth    = PrimitiveDepthMode::TestOnly;
    PrimitiveCullMode  cull     = PrimitiveCullMode::None;
    std::uint32_t      layer    = 0;
    std::uint64_t      objectId = 0;
};

/** @brief Immutable camera and viewport facts used to resolve one 3D draw list. */
struct SceneDrawContext {
    glm::mat4  view{1.f};
    glm::mat4  projection{1.f};
    glm::vec3  cameraPosition{0.f};
    glm::ivec2 viewportSize{0};
    float      nearPlane = 0.1f;
    float      farPlane  = 1000.f;

    /** @brief Validates viewport and clip-plane values. */
    void validate() const;
};

/** @brief Per-frame observable primitive workload. */
struct PrimitiveDrawStatistics {
    std::size_t commandCount    = 0;
    std::size_t segmentCount    = 0;
    std::size_t triangleCount   = 0;
    std::size_t batchCount      = 0;
    std::size_t uploadBytes     = 0;
    std::size_t cacheHits       = 0;
    std::size_t droppedCommands = 0;
};

}  // namespace eve::graphics
