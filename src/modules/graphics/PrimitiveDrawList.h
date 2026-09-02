#pragma once

#include "common/Result.h"
#include "graphics/PrimitivePath.h"
#include "graphics/PrimitiveTypes.h"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace eve::graphics {

/** @brief One owning 2D polyline command recorded for the current frame. */
struct PolylineCommand2D {
    std::vector<glm::vec2> points;
    std::vector<float>     cumulativeLengths;
    glm::mat3              transform{1.f};
    PrimitivePaint         paint;
    bool                   closed = false;
};

/** @brief One owning filled 2D triangle command. */
struct TriangleCommand2D {
    std::array<glm::vec2, 3>            points;
    glm::mat3                           transform{1.f};
    PrimitivePaint                      paint;
    std::optional<std::array<Color, 3>> vertexColors;
};

/** @brief One owning 3D polyline command recorded for the current frame. */
struct PolylineCommand3D {
    std::vector<glm::vec3> points;
    std::vector<float>     cumulativeWorldLengths;
    glm::mat4              transform{1.f};
    ScenePrimitivePaint    paint;
    bool                   closed = false;
};

/** @brief One owning filled 3D triangle command. */
struct TriangleCommand3D {
    std::array<glm::vec3, 3> points;
    glm::mat4                transform{1.f};
    ScenePrimitivePaint      paint;
};

/** @brief Frame-local Skia-style recorder for 2D line primitives. */
class PrimitiveCanvas2D {
public:
    explicit PrimitiveCanvas2D(std::size_t hardCommandLimit = 65536);

    /** @brief Pushes the complete current transform state. */
    void save();
    /** @brief Restores the most recently saved state. */
    void restore();
    /** @brief Post-concatenates a local transform. */
    void concat(const glm::mat3& transform);
    /** @brief Records a two-point line. Input data is copied. */
    void drawLine(glm::vec2 a, glm::vec2 b, const PrimitivePaint& paint);
    /** @brief Draws a point using stroke width and cap semantics. */
    void drawPoint(glm::vec2 point, const PrimitivePaint& paint);
    /** @brief Records an owning polyline snapshot. */
    void drawPolyline(std::span<const glm::vec2> points, bool closed, const PrimitivePaint& paint);
    /** @brief Atomically records an owning polyline or returns a structured budget failure. */
    [[nodiscard]] eve::Result<PrimitiveRecordStatus> tryDrawPolyline(std::span<const glm::vec2> points, bool closed,
                                                                     const PrimitivePaint& paint);
    /** @brief Flattens and records every non-empty contour in path. */
    void drawPath(const Path2D& path, const PrimitivePaint& paint, float tolerance = 0.25f);
    /** @brief Draws an axis-aligned rectangle with fill/stroke paint semantics. */
    void drawRect(glm::vec2 minimum, glm::vec2 maximum, const PrimitivePaint& paint);
    /** @brief Draws a rounded axis-aligned rectangle with per-axis corner radii. */
    void drawRoundedRect(glm::vec2 minimum, glm::vec2 maximum, glm::vec2 radii, const PrimitivePaint& paint,
                         std::uint32_t cornerSegments = 8);
    /** @brief Draws a circle using the requested segment quality. */
    void drawCircle(glm::vec2 center, float radius, const PrimitivePaint& paint, std::uint32_t segments = 48);
    /** @brief Draws a circle using a fixed quality or adaptive pixel-error policy. */
    void drawCircle(glm::vec2 center, float radius, const PrimitivePaint& paint,
                    const RadialTessellation& tessellation);
    /** @brief Draws an axis-aligned ellipse using the requested segment quality. */
    void drawEllipse(glm::vec2 center, glm::vec2 radii, const PrimitivePaint& paint, std::uint32_t segments = 48);
    /** @brief Draws an arc or filled sector. Angles are radians. */
    void drawArc(glm::vec2 center, glm::vec2 radii, float startRadians, float sweepRadians, const PrimitivePaint& paint,
                 std::uint32_t segments = 32);
    /** @brief Clears commands and state while retaining allocated capacity. */
    void reset();

    [[nodiscard]] const std::vector<PolylineCommand2D>& commands() const noexcept { return commands_; }
    [[nodiscard]] const std::vector<TriangleCommand2D>& triangles() const noexcept { return triangles_; }
    [[nodiscard]] const PrimitiveDrawStatistics&        statistics() const noexcept { return statistics_; }

private:
    std::size_t                    hardCommandLimit_;
    glm::mat3                      transform_{1.f};
    std::vector<glm::mat3>         stack_;
    std::vector<PolylineCommand2D> commands_;
    std::vector<TriangleCommand2D> triangles_;
    PrimitiveDrawStatistics        statistics_;

    void drawTriangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, const PrimitivePaint& paint);
    void drawColoredTriangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, Color colorA, Color colorB, Color colorC,
                             const PrimitivePaint& paint);
    void drawCoverageFringe(std::span<const glm::vec2> outline, const PrimitivePaint& paint);
};

/** @brief Frame-local scene recorder for 3D line primitives. */
class PrimitiveSceneCanvas3D {
public:
    explicit PrimitiveSceneCanvas3D(SceneDrawContext context, std::size_t hardCommandLimit = 65536);

    void save();
    void restore();
    void concat(const glm::mat4& transform);
    void drawLine(glm::vec3 a, glm::vec3 b, const ScenePrimitivePaint& paint);
    void drawPoint(glm::vec3 point, const ScenePrimitivePaint& paint);
    void drawPolyline(std::span<const glm::vec3> points, bool closed, const ScenePrimitivePaint& paint);
    /** @brief Atomically records an owning 3D polyline or returns a structured budget failure. */
    [[nodiscard]] eve::Result<PrimitiveRecordStatus> tryDrawPolyline(std::span<const glm::vec3> points, bool closed,
                                                                     const ScenePrimitivePaint& paint);
    void drawRay(glm::vec3 origin, glm::vec3 direction, float length, const ScenePrimitivePaint& paint);
    void drawTriangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, const ScenePrimitivePaint& paint);
    void drawQuad(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, const ScenePrimitivePaint& paint);
    void drawDisk(glm::vec3 center, glm::vec3 normal, float radius, const ScenePrimitivePaint& paint,
                  std::uint32_t segments = 32);
    void drawArc(glm::vec3 center, glm::vec3 normal, glm::vec3 zeroDirection, float radius, float startRadians,
                 float sweepRadians, const ScenePrimitivePaint& paint, std::uint32_t segments = 32);
    /** @brief Records the twelve edges of an axis-aligned box. */
    void drawAabb(glm::vec3 minimum, glm::vec3 maximum, const ScenePrimitivePaint& paint);
    void drawObb(glm::vec3 center, const std::array<glm::vec3, 3>& halfAxes, const ScenePrimitivePaint& paint);
    /** @brief Records a planar grid spanned by two caller-provided axes. */
    void drawGrid(glm::vec3 origin, glm::vec3 axisU, glm::vec3 axisV, std::uint32_t cellsU, std::uint32_t cellsV,
                  const ScenePrimitivePaint& paint);
    /** @brief Records three orthogonal great circles. */
    void drawSphere(glm::vec3 center, float radius, const ScenePrimitivePaint& paint, std::uint32_t segments = 32);
    /** @brief Draws a sphere using a fixed quality or adaptive projected error policy. */
    void drawSphere(glm::vec3 center, float radius, const ScenePrimitivePaint& paint,
                    const RadialTessellation& tessellation);
    void drawCapsule(glm::vec3 a, glm::vec3 b, float radius, const ScenePrimitivePaint& paint,
                     std::uint32_t segments = 32);
    /** @brief Records endpoint circles and four side lines of a cylinder. */
    void drawCylinder(glm::vec3 a, glm::vec3 b, float radius, const ScenePrimitivePaint& paint,
                      std::uint32_t segments = 32);
    /** @brief Records the base circle and four side lines of a cone. */
    void drawCone(glm::vec3 apex, glm::vec3 axis, float height, float radius, const ScenePrimitivePaint& paint,
                  std::uint32_t segments = 32);
    /** @brief Records a shaft and four-sided arrow head. */
    void drawArrow(glm::vec3 from, glm::vec3 to, float headLength, float headRadius, const ScenePrimitivePaint& paint);
    void drawFrustum(const std::array<glm::vec3, 8>& corners, const ScenePrimitivePaint& paint);
    void reset();

    [[nodiscard]] const SceneDrawContext&               context() const noexcept { return context_; }
    [[nodiscard]] const std::vector<PolylineCommand3D>& commands() const noexcept { return commands_; }
    [[nodiscard]] const std::vector<TriangleCommand3D>& triangles() const noexcept { return triangles_; }
    [[nodiscard]] const PrimitiveDrawStatistics&        statistics() const noexcept { return statistics_; }

private:
    SceneDrawContext               context_;
    std::size_t                    hardCommandLimit_;
    glm::mat4                      transform_{1.f};
    std::vector<glm::mat4>         stack_;
    std::vector<PolylineCommand3D> commands_;
    std::vector<TriangleCommand3D> triangles_;
    PrimitiveDrawStatistics        statistics_;

    void recordTriangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, const ScenePrimitivePaint& paint);
};

}  // namespace eve::graphics
