#include "procgen/mesh/GeometryStroke.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace eve::procgen {
namespace {

struct V3 {
    float x, y, z;
};

V3    operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3    operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3    operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3    cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float length(V3 a) { return std::sqrt(dot(a, a)); }
V3    normalized(V3 a) {
    const float magnitude = length(a);
    return magnitude > 1e-8f ? a * (1.f / magnitude) : V3{1.f, 0.f, 0.f};
}

template <typename T>
Result<T> strokeFailure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(
        Diagnostic::error(code, std::move(message), std::move(path), {}, "procgen.geometryStroke"));
}

void emitTriangle(MeshBuild& mesh, V3 a, V3 b, V3 c, float u0 = 0.f, float u1 = 1.f, float u2 = 1.f) {
    const V3       normal = normalized(cross(b - a, c - a));
    const uint32_t base   = static_cast<uint32_t>(mesh.getVertexCount());
    mesh.addVertex(a.x, a.y, a.z, normal.x, normal.y, normal.z, u0, 0.f);
    mesh.addVertex(b.x, b.y, b.z, normal.x, normal.y, normal.z, u1, 0.f);
    mesh.addVertex(c.x, c.y, c.z, normal.x, normal.y, normal.z, u2, 1.f);
    mesh.addTriangle(base, base + 1, base + 2);
}

void emitQuad(MeshBuild& mesh, V3 a, V3 b, V3 c, V3 d) {
    emitTriangle(mesh, a, b, c, 0.f, 1.f, 1.f);
    emitTriangle(mesh, a, c, d, 0.f, 1.f, 0.f);
}

}  // namespace

Result<void> GeometryStroke::setShapeResult(std::string_view shape) {
    if (shape != "quad" && shape != "triangularPrism" && shape != "cube")
        return strokeFailure<void>(DiagnosticCode::InvalidArgument,
                                   "geometry stroke shape must be quad, triangularPrism, or cube", "shape");
    shape_ = shape;
    ++revision_;
    return Result<void>::success();
}

Result<void> GeometryStroke::setInputSpaceResult(std::string_view inputSpace, float planeY) {
    if (inputSpace != "spatial" && inputSpace != "planar")
        return strokeFailure<void>(DiagnosticCode::InvalidArgument,
                                   "geometry stroke input space must be spatial or planar", "inputSpace");
    if (!std::isfinite(planeY))
        return strokeFailure<void>(DiagnosticCode::InvalidArgument, "geometry stroke planeY must be finite", "planeY");
    inputSpace_ = inputSpace;
    planeY_     = planeY;
    if (inputSpace_ == "planar")
        for (auto& point : points_) point.y = planeY_;
    ++revision_;
    return Result<void>::success();
}

Result<void> GeometryStroke::setSizeResult(float width, float depth) {
    if (!std::isfinite(width) || !std::isfinite(depth) || width <= 0.f || depth <= 0.f)
        return strokeFailure<void>(DiagnosticCode::InvalidArgument,
                                   "geometry stroke width and depth must be finite and positive", "size");
    width_ = width;
    depth_ = depth;
    ++revision_;
    return Result<void>::success();
}

Result<void> GeometryStroke::setMinimumSpacingResult(float spacing) {
    if (!std::isfinite(spacing) || spacing < 0.f)
        return strokeFailure<void>(DiagnosticCode::InvalidArgument,
                                   "geometry stroke minimum spacing must be finite and non-negative", "spacing");
    spacing_ = spacing;
    ++revision_;
    return Result<void>::success();
}

Result<bool> GeometryStroke::addPointResult(float x, float y, float z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return strokeFailure<bool>(DiagnosticCode::InvalidArgument, "geometry stroke point must be finite", "point");
    if (points_.size() >= 65536)
        return strokeFailure<bool>(DiagnosticCode::InvalidArgument, "geometry stroke point budget exceeded", "point");
    GeometryStrokePoint point{x, inputSpace_ == "planar" ? planeY_ : y, z};
    if (!points_.empty()) {
        const auto& previous = points_.back();
        if (length({point.x - previous.x, point.y - previous.y, point.z - previous.z}) < spacing_)
            return Result<bool>::success(false);
    }
    points_.push_back(point);
    ++revision_;
    return Result<bool>::success(true);
}

Result<void> GeometryStroke::undoResult() {
    if (points_.empty())
        return strokeFailure<void>(DiagnosticCode::NotFound, "geometry stroke has no point to undo", "points");
    points_.pop_back();
    ++revision_;
    return Result<void>::success();
}

void GeometryStroke::clear() noexcept {
    if (points_.empty()) return;
    points_.clear();
    ++revision_;
}

Result<MeshBuild> GeometryStroke::buildMeshResult() const {
    if (points_.size() < 2)
        return strokeFailure<MeshBuild>(DiagnosticCode::InvalidArgument,
                                        "geometry stroke requires at least two accepted points", "points");
    MeshBuild output;
    output.setActiveGroup(shape_);
    std::vector<std::vector<V3>> rings;
    rings.reserve(points_.size());
    for (std::size_t index = 0; index < points_.size(); ++index) {
        const auto& point = points_[index];
        const V3    center{point.x, point.y, point.z};
        const auto& previousPoint = points_[index == 0 ? 0 : index - 1];
        const auto& nextPoint     = points_[std::min(index + 1, points_.size() - 1)];
        V3 forward{nextPoint.x - previousPoint.x, nextPoint.y - previousPoint.y, nextPoint.z - previousPoint.z};
        forward               = normalized(forward);
        const V3    reference = std::abs(forward.y) < 0.95f ? V3{0.f, 1.f, 0.f} : V3{1.f, 0.f, 0.f};
        const V3    side      = normalized(cross(reference, forward));
        const V3    up        = normalized(cross(forward, side));
        const float halfWidth = width_ * 0.5f;
        const float halfDepth = depth_ * 0.5f;
        if (shape_ == "quad") {
            rings.push_back({center - side * halfWidth, center + side * halfWidth});
        } else if (shape_ == "triangularPrism") {
            rings.push_back({center - side * halfWidth - up * halfDepth, center + side * halfWidth - up * halfDepth,
                             center + up * halfDepth});
        } else {
            rings.push_back({center - side * halfWidth - up * halfDepth, center + side * halfWidth - up * halfDepth,
                             center + side * halfWidth + up * halfDepth, center - side * halfWidth + up * halfDepth});
        }
    }
    const std::size_t ringSize = rings.front().size();
    for (std::size_t index = 1; index < rings.size(); ++index) {
        if (shape_ == "quad") {
            emitQuad(output, rings[index - 1][0], rings[index][0], rings[index][1], rings[index - 1][1]);
            emitQuad(output, rings[index - 1][1], rings[index][1], rings[index][0], rings[index - 1][0]);
            continue;
        }
        for (std::size_t side = 0; side < ringSize; ++side) {
            const std::size_t next = (side + 1) % ringSize;
            emitQuad(output, rings[index - 1][side], rings[index][side], rings[index][next], rings[index - 1][next]);
        }
    }
    if (shape_ == "triangularPrism") {
        emitTriangle(output, rings.front()[2], rings.front()[1], rings.front()[0]);
        emitTriangle(output, rings.back()[0], rings.back()[1], rings.back()[2]);
    } else if (shape_ == "cube") {
        emitQuad(output, rings.front()[3], rings.front()[2], rings.front()[1], rings.front()[0]);
        emitQuad(output, rings.back()[0], rings.back()[1], rings.back()[2], rings.back()[3]);
    }
    output.setMeta("generator", "mesh.geometryStroke");
    output.setMeta("shape", shape_);
    output.setMeta("inputSpace", inputSpace_);
    output.setMeta("pointCount", std::to_string(points_.size()));
    return Result<MeshBuild>::success(std::move(output));
}

}  // namespace eve::procgen
