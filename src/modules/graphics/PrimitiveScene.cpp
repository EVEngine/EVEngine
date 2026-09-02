#include "graphics/PrimitiveScene.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <type_traits>
#include <utility>

namespace eve::graphics {
namespace {

template <class T>
eve::Result<T> primitiveFailure(eve::DiagnosticCode code, std::string message) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), "primitive", {}, "graphics"));
}

bool finite(glm::vec3 value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

eve::Result<void> validateDescriptor(const PrimitiveDescriptor3D& descriptor) {
    try {
        descriptor.paint.validate();
    } catch (const std::exception& error) {
        return primitiveFailure<void>(eve::DiagnosticCode::InvalidArgument, error.what());
    }
    for (glm::length_t column = 0; column < 4; ++column)
        for (glm::length_t row = 0; row < 4; ++row)
            if (!std::isfinite(descriptor.transform[column][row]))
                return primitiveFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                              "primitive transform must be finite");
    const bool valid = std::visit(
        [](const auto& geometry) {
            using Geometry = std::decay_t<decltype(geometry)>;
            if constexpr (std::is_same_v<Geometry, PrimitivePolyline3D>) {
                return geometry.points.size() >= 2 &&
                       std::all_of(geometry.points.begin(), geometry.points.end(), finite);
            } else if constexpr (std::is_same_v<Geometry, PrimitiveAabb3D>) {
                return finite(geometry.minimum) && finite(geometry.maximum);
            } else if constexpr (std::is_same_v<Geometry, PrimitiveObb3D>) {
                return finite(geometry.center) && finite(geometry.halfAxes[0]) && finite(geometry.halfAxes[1]) &&
                       finite(geometry.halfAxes[2]);
            } else if constexpr (std::is_same_v<Geometry, PrimitiveDisk3D>) {
                return finite(geometry.center) && finite(geometry.normal) && std::isfinite(geometry.radius) &&
                       geometry.radius > 0.f && geometry.segments >= 3;
            } else if constexpr (std::is_same_v<Geometry, PrimitiveArc3D>) {
                return finite(geometry.center) && finite(geometry.normal) && finite(geometry.zeroDirection) &&
                       std::isfinite(geometry.radius) && geometry.radius > 0.f &&
                       std::isfinite(geometry.startRadians) && std::isfinite(geometry.sweepRadians) &&
                       geometry.segments >= 1;
            } else if constexpr (std::is_same_v<Geometry, PrimitiveSphere3D>) {
                return finite(geometry.center) && std::isfinite(geometry.radius) && geometry.radius > 0.f &&
                       geometry.segments >= 3;
            } else if constexpr (std::is_same_v<Geometry, PrimitiveCapsule3D> ||
                                 std::is_same_v<Geometry, PrimitiveCylinder3D>) {
                return finite(geometry.a) && finite(geometry.b) && std::isfinite(geometry.radius) &&
                       geometry.radius > 0.f && geometry.segments >= 3;
            } else if constexpr (std::is_same_v<Geometry, PrimitiveCone3D>) {
                return finite(geometry.apex) && finite(geometry.axis) && std::isfinite(geometry.height) &&
                       geometry.height > 0.f && std::isfinite(geometry.radius) && geometry.radius > 0.f &&
                       geometry.segments >= 3;
            } else if constexpr (std::is_same_v<Geometry, PrimitiveGrid3D>) {
                return finite(geometry.origin) && finite(geometry.axisU) && finite(geometry.axisV) &&
                       geometry.cellsU > 0 && geometry.cellsV > 0;
            } else if constexpr (std::is_same_v<Geometry, PrimitiveArrow3D>) {
                return finite(geometry.from) && finite(geometry.to) && std::isfinite(geometry.headLength) &&
                       geometry.headLength > 0.f && std::isfinite(geometry.headRadius) && geometry.headRadius > 0.f;
            } else {
                return std::all_of(geometry.corners.begin(), geometry.corners.end(), finite);
            }
        },
        descriptor.geometry);
    if (!valid)
        return primitiveFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                      "primitive geometry is invalid or non-finite");
    return eve::Result<void>::success();
}

}  // namespace

PrimitiveScene::PrimitiveScene() {
    static std::atomic<PrimitiveHandle::owner_type> nextOwner{1};
    owner_ = nextOwner.fetch_add(1, std::memory_order_relaxed);
    if (owner_ == 0) owner_ = nextOwner.fetch_add(1, std::memory_order_relaxed);
}

eve::Result<PrimitiveHandle> PrimitiveScene::add(PrimitiveDescriptor3D descriptor) {
    auto validation = validateDescriptor(descriptor);
    if (!validation) return eve::Result<PrimitiveHandle>::failure(validation.status());
    PrimitiveHandle::index_type index;
    if (!freeSlots_.empty()) {
        index = freeSlots_.back();
        freeSlots_.pop_back();
        slots_[index].descriptor = std::move(descriptor);
    } else {
        if (slots_.size() >= PrimitiveHandle::invalidIndex) {
            return primitiveFailure<PrimitiveHandle>(eve::DiagnosticCode::Failed,
                                                     "primitive scene slot capacity exceeded");
        }
        index = static_cast<PrimitiveHandle::index_type>(slots_.size());
        slots_.push_back(Slot{std::move(descriptor), 1, false});
    }
    ++liveCount_;
    return eve::Result<PrimitiveHandle>::success(PrimitiveHandle(owner_, index, slots_[index].generation));
}

eve::Result<PrimitiveUpdateStatus> PrimitiveScene::update(PrimitiveHandle handle, PrimitiveDescriptor3D descriptor) {
    if (!matches(handle)) {
        return primitiveFailure<PrimitiveUpdateStatus>(eve::DiagnosticCode::StaleHandle, "primitive handle is stale");
    }
    auto validation = validateDescriptor(descriptor);
    if (!validation) return eve::Result<PrimitiveUpdateStatus>::failure(validation.status());
    slots_[handle.index()].descriptor = std::move(descriptor);
    return eve::Result<PrimitiveUpdateStatus>::success(PrimitiveUpdateStatus::Updated);
}

eve::Result<PrimitiveRemoveStatus> PrimitiveScene::remove(PrimitiveHandle handle) {
    if (!matches(handle)) {
        return primitiveFailure<PrimitiveRemoveStatus>(eve::DiagnosticCode::StaleHandle, "primitive handle is stale");
    }
    Slot& slot = slots_[handle.index()];
    slot.descriptor.reset();
    --liveCount_;
    const auto next = PrimitiveHandle::nextGeneration(slot.generation);
    if (next) {
        slot.generation = *next;
        freeSlots_.push_back(handle.index());
    } else {
        slot.retired = true;
    }
    return eve::Result<PrimitiveRemoveStatus>::success(PrimitiveRemoveStatus::Removed);
}

eve::Result<std::size_t> PrimitiveScene::updateMany(std::span<const PrimitiveBatchUpdate> updates) {
    for (const PrimitiveBatchUpdate& update : updates) {
        if (!matches(update.handle))
            return primitiveFailure<std::size_t>(eve::DiagnosticCode::StaleHandle,
                                                 "primitive batch contains a stale handle");
        auto validation = validateDescriptor(update.descriptor);
        if (!validation) return eve::Result<std::size_t>::failure(validation.status());
    }
    for (const PrimitiveBatchUpdate& update : updates) slots_[update.handle.index()].descriptor = update.descriptor;
    return eve::Result<std::size_t>::success(updates.size());
}

const PrimitiveDescriptor3D* PrimitiveScene::tryGet(PrimitiveHandle handle) const noexcept {
    if (!matches(handle)) return nullptr;
    return &*slots_[handle.index()].descriptor;
}

bool PrimitiveScene::isStale(PrimitiveHandle handle) const noexcept { return handle.isValid() && !matches(handle); }

void PrimitiveScene::clear() {
    freeSlots_.clear();
    liveCount_ = 0;
    for (PrimitiveHandle::index_type index = 0; index < slots_.size(); ++index) {
        Slot& slot = slots_[index];
        slot.descriptor.reset();
        if (slot.retired) continue;
        const auto next = PrimitiveHandle::nextGeneration(slot.generation);
        if (next) {
            slot.generation = *next;
            freeSlots_.push_back(index);
        } else {
            slot.retired = true;
        }
    }
}

void PrimitiveScene::render(PrimitiveSceneCanvas3D& canvas) const {
    for (const Slot& slot : slots_) {
        if (!slot.descriptor || !slot.descriptor->visible) continue;
        const PrimitiveDescriptor3D& descriptor = *slot.descriptor;
        canvas.save();
        canvas.concat(descriptor.transform);
        std::visit(
            [&](const auto& geometry) {
                using Geometry = std::decay_t<decltype(geometry)>;
                if constexpr (std::is_same_v<Geometry, PrimitivePolyline3D>) {
                    canvas.drawPolyline(geometry.points, geometry.closed, descriptor.paint);
                } else if constexpr (std::is_same_v<Geometry, PrimitiveAabb3D>) {
                    canvas.drawAabb(geometry.minimum, geometry.maximum, descriptor.paint);
                } else if constexpr (std::is_same_v<Geometry, PrimitiveObb3D>) {
                    canvas.drawObb(geometry.center, geometry.halfAxes, descriptor.paint);
                } else if constexpr (std::is_same_v<Geometry, PrimitiveDisk3D>) {
                    canvas.drawDisk(geometry.center, geometry.normal, geometry.radius, descriptor.paint,
                                    geometry.segments);
                } else if constexpr (std::is_same_v<Geometry, PrimitiveArc3D>) {
                    canvas.drawArc(geometry.center, geometry.normal, geometry.zeroDirection, geometry.radius,
                                   geometry.startRadians, geometry.sweepRadians, descriptor.paint, geometry.segments);
                } else if constexpr (std::is_same_v<Geometry, PrimitiveSphere3D>) {
                    canvas.drawSphere(geometry.center, geometry.radius, descriptor.paint, geometry.segments);
                } else if constexpr (std::is_same_v<Geometry, PrimitiveCapsule3D>) {
                    canvas.drawCapsule(geometry.a, geometry.b, geometry.radius, descriptor.paint, geometry.segments);
                } else if constexpr (std::is_same_v<Geometry, PrimitiveCylinder3D>) {
                    canvas.drawCylinder(geometry.a, geometry.b, geometry.radius, descriptor.paint, geometry.segments);
                } else if constexpr (std::is_same_v<Geometry, PrimitiveCone3D>) {
                    canvas.drawCone(geometry.apex, geometry.axis, geometry.height, geometry.radius, descriptor.paint,
                                    geometry.segments);
                } else if constexpr (std::is_same_v<Geometry, PrimitiveGrid3D>) {
                    canvas.drawGrid(geometry.origin, geometry.axisU, geometry.axisV, geometry.cellsU, geometry.cellsV,
                                    descriptor.paint);
                } else if constexpr (std::is_same_v<Geometry, PrimitiveArrow3D>) {
                    canvas.drawArrow(geometry.from, geometry.to, geometry.headLength, geometry.headRadius,
                                     descriptor.paint);
                } else if constexpr (std::is_same_v<Geometry, PrimitiveFrustum3D>) {
                    canvas.drawFrustum(geometry.corners, descriptor.paint);
                }
            },
            descriptor.geometry);
        canvas.restore();
    }
}

bool PrimitiveScene::matches(PrimitiveHandle handle) const noexcept {
    if (!handle.isValid() || handle.owner() != owner_ || handle.index() >= slots_.size()) return false;
    const Slot& slot = slots_[handle.index()];
    return !slot.retired && slot.descriptor.has_value() && slot.generation == handle.generation();
}

}  // namespace eve::graphics
