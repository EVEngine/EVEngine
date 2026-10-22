#include "fluids/OceanWaterView.h"
#include "fluids/OceanWake.h"

#include <cmath>

namespace eve::fluids {
namespace {
template <class T>
Result<T> staleTime() {
    return Result<T>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "Ocean and wake timestamps differ", "fluids.ocean.time"));
}
}  // namespace
Result<OceanSurfaceSample> OceanWaterView::sample(glm::dvec2 worldXZ) const {
    auto base = spectrum_.get().sample(worldXZ);
    if (!base) return base;
    if (!wake_) return base;
    if (std::abs(wake_->get().time() - time()) > 1e-8) return staleTime<OceanSurfaceSample>();
    auto local = wake_->get().sample(worldXZ);
    if (!local) return Result<OceanSurfaceSample>::failure(local.status());
    auto value = base.value();
    value.height += local.value().height;
    value.velocity.y +=
        local.value().velocity + glm::dot(glm::dvec2(value.velocity.x, value.velocity.z), local.value().gradient);
    const auto gradient = glm::dvec2(-value.normal.x, -value.normal.z) / value.normal.y + local.value().gradient;
    value.normal        = glm::normalize(glm::dvec3(-gradient.x, 1.0, -gradient.y));
    return Result<OceanSurfaceSample>::success(value);
}
Result<glm::dvec3> OceanWaterView::position(glm::dvec2 parameterXZ) const {
    auto base = spectrum_.get().position(parameterXZ);
    if (!base) return base;
    if (!wake_) return base;
    if (std::abs(wake_->get().time() - time()) > 1e-8) return staleTime<glm::dvec3>();
    auto value = base.value();
    auto local = wake_->get().sample({value.x, value.z});
    if (!local) return Result<glm::dvec3>::failure(local.status());
    value.y += local.value().height;
    return Result<glm::dvec3>::success(value);
}
Result<OceanSurfaceGeometry> OceanWaterView::geometry(glm::dvec2 parameterXZ) const {
    auto base = spectrum_.get().geometry(parameterXZ);
    if (!base || !wake_) return base;
    if (std::abs(wake_->get().time() - time()) > 1e-8) return staleTime<OceanSurfaceGeometry>();
    auto value = base.value();
    auto local = wake_->get().sample({value.position.x, value.position.z});
    if (!local) return Result<OceanSurfaceGeometry>::failure(local.status());
    value.position.y += local.value().height;
    value.foam = local.value().foam;
    value.tangentX.y += glm::dot(local.value().gradient, glm::dvec2(value.tangentX.x, value.tangentX.z));
    value.tangentZ.y += glm::dot(local.value().gradient, glm::dvec2(value.tangentZ.x, value.tangentZ.z));
    return Result<OceanSurfaceGeometry>::success(value);
}
}  // namespace eve::fluids
