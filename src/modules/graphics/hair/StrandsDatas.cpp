#include "graphics/hair/StrandsDatas.h"

#include "common/Diagnostic.h"

#include <cmath>
#include <limits>
#include <string>

#include <glm/glm.hpp>

namespace eve::graphics::hair {
namespace {

bool finiteVec(const glm::vec3 &v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

}  // namespace

void StrandsDatas::setPoints(std::vector<StrandPoint> points) { points_ = std::move(points); }

void StrandsDatas::setCurves(std::vector<StrandCurve> curves) { curves_ = std::move(curves); }

uint32_t StrandsDatas::addPoint(const StrandPoint &point) {
    const auto index = static_cast<uint32_t>(points_.size());
    points_.push_back(point);
    return index;
}

uint32_t StrandsDatas::addCurve(const StrandCurve &curve) {
    const auto index = static_cast<uint32_t>(curves_.size());
    curves_.push_back(curve);
    return index;
}

void StrandsDatas::clear() {
    points_.clear();
    curves_.clear();
}

Result<void> StrandsDatas::validate() const {
    if (curves_.empty()) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "StrandsDatas: no curves", "hair.strands.curves"));
    }
    if (points_.empty()) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "StrandsDatas: no points", "hair.strands.points"));
    }
    for (size_t ci = 0; ci < curves_.size(); ++ci) {
        const StrandCurve &c = curves_[ci];
        if (c.pointCount < 2u) {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvariantViolation, "StrandsDatas: curve needs at least 2 points",
                "hair.strands.curves[" + std::to_string(ci) + "].pointCount"));
        }
        const uint64_t end = uint64_t(c.pointOffset) + uint64_t(c.pointCount);
        if (end > points_.size()) {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvariantViolation, "StrandsDatas: curve range exceeds points",
                "hair.strands.curves[" + std::to_string(ci) + "]"));
        }
        if (!std::isfinite(c.length) || c.length < 0.f) {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "StrandsDatas: invalid curve length",
                "hair.strands.curves[" + std::to_string(ci) + "].length"));
        }
        for (uint32_t pi = 0; pi < c.pointCount; ++pi) {
            const StrandPoint &p = points_[c.pointOffset + pi];
            if (!finiteVec(p.position) || !std::isfinite(p.radius) || p.radius < 0.f ||
                !std::isfinite(p.u)) {
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "StrandsDatas: non-finite or negative point",
                    "hair.strands.points"));
            }
        }
    }
    return Result<void>::success();
}

std::span<const StrandPoint> StrandsDatas::curvePoints(size_t curveIndex) const {
    if (curveIndex >= curves_.size()) return {};
    const StrandCurve &c = curves_[curveIndex];
    if (uint64_t(c.pointOffset) + uint64_t(c.pointCount) > points_.size()) return {};
    return std::span<const StrandPoint>(points_.data() + c.pointOffset, c.pointCount);
}

void StrandsDatas::computeBounds(glm::vec3 &outMin, glm::vec3 &outMax) const {
    if (points_.empty()) {
        outMin = glm::vec3(0.f);
        outMax = glm::vec3(0.f);
        return;
    }
    outMin = glm::vec3(std::numeric_limits<float>::max());
    outMax = glm::vec3(std::numeric_limits<float>::lowest());
    for (const StrandPoint &p : points_) {
        outMin = glm::min(outMin, p.position);
        outMax = glm::max(outMax, p.position);
    }
}

}  // namespace eve::graphics::hair
