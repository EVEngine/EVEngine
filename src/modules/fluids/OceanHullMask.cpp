#include "fluids/OceanHullMask.h"

#include <cmath>

namespace eve::fluids {
namespace {
bool finite(glm::dvec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
Result<OceanHullMaskFrame> invalid() {
    return Result<OceanHullMaskFrame>::failure(Diagnostic::error(
        DiagnosticCode::InvalidArgument, "Invalid ocean hull volume or render origin", "fluids.ocean.hull"));
}
}  // namespace
Result<OceanHullMaskFrame> OceanHullMaskBuilder::build() const {
    if (volumes_.size() > 32 || !finite(origin) || glm::any(glm::greaterThan(glm::abs(origin), glm::dvec3(1e9))))
        return invalid();
    OceanHullMaskFrame out;
    out.origin_ = origin;
    out.rows_.reserve(volumes_.size() * 17);
    for (const auto& volume : volumes_) {
        const auto   relative = volume.center - origin;
        const double qLength  = glm::length(volume.rotation);
        if (!finite(volume.center) || !finite(relative) ||
            glm::any(glm::greaterThan(glm::abs(relative), glm::dvec3(1e6))) || !finite(volume.halfExtent) ||
            glm::any(glm::lessThanEqual(volume.halfExtent, glm::dvec3(0.0))) ||
            glm::any(glm::greaterThan(volume.halfExtent, glm::dvec3(10000.0))) || !std::isfinite(qLength) ||
            std::abs(qLength - 1.0) > 1e-6 || volume.bevels.size() > 10)
            return invalid();
        const auto rotation = glm::normalize(volume.rotation);
        const auto first    = out.rows_.size();
        out.rows_.resize(first + 17, glm::vec4(0));
        out.rows_[first].x = static_cast<float>(6 + volume.bevels.size());
        auto append        = [&](unsigned index, glm::dvec3 normal, double offset) {
            const auto worldNormal       = rotation * normal;
            out.rows_[first + 1 + index] = glm::vec4(worldNormal, offset + glm::dot(worldNormal, relative));
        };
        for (unsigned axis = 0; axis < 3; ++axis) {
            glm::dvec3 normal(0);
            normal[axis] = 1;
            append(axis * 2, normal, volume.halfExtent[axis]);
            append(axis * 2 + 1, -normal, volume.halfExtent[axis]);
        }
        for (unsigned i = 0; i < volume.bevels.size(); ++i) {
            const auto   plane  = volume.bevels[i];
            const double length = glm::length(glm::dvec3(plane));
            if (!finite(glm::dvec3(plane)) || !std::isfinite(plane.w) || length < 1e-12 || !std::isfinite(length) ||
                std::abs(plane.w / length) > 10000.0)
                return invalid();
            append(i + 6, glm::dvec3(plane) / length, plane.w / length);
        }
    }
    return Result<OceanHullMaskFrame>::success(std::move(out));
}
bool OceanHullMaskFrame::contains(glm::dvec3 worldPoint) const {
    if (!finite(worldPoint)) return false;
    const auto relative = worldPoint - origin_;
    if (!finite(relative) || glm::any(glm::greaterThan(glm::abs(relative), glm::dvec3(2e6)))) return false;
    const glm::vec3 point(relative);
    for (std::size_t first = 0; first < rows_.size(); first += 17) {
        bool       inside = true;
        const auto count  = static_cast<unsigned>(rows_[first].x);
        for (unsigned i = 0; i < count; ++i) {
            const auto plane = rows_[first + 1 + i];
            if (glm::dot(glm::vec3(plane), point) > plane.w) {
                inside = false;
                break;
            }
        }
        if (inside) return true;
    }
    return false;
}
}  // namespace eve::fluids
