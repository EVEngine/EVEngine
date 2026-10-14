#include "graphics/PrimitivePath.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <limits>
#include <memory>
#include <numeric>
#include <simplesquirrel/simplesquirrel.hpp>
#include <type_traits>
#include <utility>
#include "common/Assert.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "graphics/Graphics.h"
#include "graphics/PrimitiveScene.h"
#include "graphics/PrimitiveScriptBindings.h"
#include "graphics/PrimitiveTypes.h"

// PrimitivePath.cpp
namespace eve::graphics {
namespace {

void validatePoint(glm::vec2 point) {
    const bool finite = std::isfinite(point.x) && std::isfinite(point.y);
    EV_PARAM_CHECK(finite, "path points must be finite");
}

float pointLineDistance(glm::vec2 point, glm::vec2 a, glm::vec2 b) {
    const glm::vec2 line          = b - a;
    const float     lengthSquared = glm::dot(line, line);
    if (lengthSquared <= 1e-20f) return glm::length(point - a);
    const float t = glm::clamp(glm::dot(point - a, line) / lengthSquared, 0.f, 1.f);
    return glm::length(point - (a + line * t));
}

void flattenQuad(glm::vec2 a, glm::vec2 control, glm::vec2 b, float tolerance, std::uint32_t depth,
                 std::uint32_t maxDepth, std::vector<glm::vec2>& output) {
    if (depth >= maxDepth || pointLineDistance(control, a, b) <= tolerance) {
        output.push_back(b);
        return;
    }
    const glm::vec2 ac     = (a + control) * 0.5f;
    const glm::vec2 cb     = (control + b) * 0.5f;
    const glm::vec2 middle = (ac + cb) * 0.5f;
    flattenQuad(a, ac, middle, tolerance, depth + 1, maxDepth, output);
    flattenQuad(middle, cb, b, tolerance, depth + 1, maxDepth, output);
}

void flattenCubic(glm::vec2 a, glm::vec2 c1, glm::vec2 c2, glm::vec2 b, float tolerance, std::uint32_t depth,
                  std::uint32_t maxDepth, std::vector<glm::vec2>& output) {
    const float deviation = std::max(pointLineDistance(c1, a, b), pointLineDistance(c2, a, b));
    if (depth >= maxDepth || deviation <= tolerance) {
        output.push_back(b);
        return;
    }
    const glm::vec2 a1           = (a + c1) * 0.5f;
    const glm::vec2 c12          = (c1 + c2) * 0.5f;
    const glm::vec2 c2b          = (c2 + b) * 0.5f;
    const glm::vec2 leftControl  = (a1 + c12) * 0.5f;
    const glm::vec2 rightControl = (c12 + c2b) * 0.5f;
    const glm::vec2 middle       = (leftControl + rightControl) * 0.5f;
    flattenCubic(a, a1, leftControl, middle, tolerance, depth + 1, maxDepth, output);
    flattenCubic(middle, rightControl, c2b, b, tolerance, depth + 1, maxDepth, output);
}

}  // namespace

Path2D& Path2D::moveTo(glm::vec2 point) {
    validatePoint(point);
    verbs_.push_back(Verb::Move);
    points_.push_back(point);
    contourOpen_ = true;
    return *this;
}

Path2D& Path2D::lineTo(glm::vec2 point) {
    EV_PARAM_CHECK(contourOpen_, "lineTo requires a preceding moveTo");
    validatePoint(point);
    verbs_.push_back(Verb::Line);
    points_.push_back(point);
    return *this;
}

Path2D& Path2D::quadTo(glm::vec2 control, glm::vec2 point) {
    EV_PARAM_CHECK(contourOpen_, "quadTo requires a preceding moveTo");
    validatePoint(control);
    validatePoint(point);
    verbs_.push_back(Verb::Quad);
    points_.push_back(control);
    points_.push_back(point);
    return *this;
}

Path2D& Path2D::cubicTo(glm::vec2 control1, glm::vec2 control2, glm::vec2 point) {
    EV_PARAM_CHECK(contourOpen_, "cubicTo requires a preceding moveTo");
    validatePoint(control1);
    validatePoint(control2);
    validatePoint(point);
    verbs_.push_back(Verb::Cubic);
    points_.push_back(control1);
    points_.push_back(control2);
    points_.push_back(point);
    return *this;
}

Path2D& Path2D::close() {
    EV_PARAM_CHECK(contourOpen_, "close requires an open contour");
    verbs_.push_back(Verb::Close);
    contourOpen_ = false;
    return *this;
}

void Path2D::clear() noexcept {
    verbs_.clear();
    points_.clear();
    contourOpen_ = false;
}

std::vector<FlattenedContour2D> Path2D::flatten(float tolerance, std::uint32_t maxDepth) const {
    const bool validTolerance = std::isfinite(tolerance) && tolerance > 0.f;
    EV_PARAM_CHECK(validTolerance, "path flatten tolerance must be finite and positive");
    const bool validMaxDepth = maxDepth > 0 && maxDepth <= 24;
    EV_PARAM_CHECK(validMaxDepth, "path flatten maxDepth must be in [1, 24]");

    std::vector<FlattenedContour2D> result;
    std::size_t                     pointIndex = 0;
    glm::vec2                       current{0.f};
    for (Verb verb : verbs_) {
        switch (verb) {
            case Verb::Move:
                result.push_back({});
                current = points_[pointIndex++];
                result.back().points.push_back(current);
                break;
            case Verb::Line:
                current = points_[pointIndex++];
                result.back().points.push_back(current);
                break;
            case Verb::Quad: {
                const glm::vec2 control = points_[pointIndex++];
                const glm::vec2 end     = points_[pointIndex++];
                flattenQuad(current, control, end, tolerance, 0, maxDepth, result.back().points);
                current = end;
                break;
            }
            case Verb::Cubic: {
                const glm::vec2 control1 = points_[pointIndex++];
                const glm::vec2 control2 = points_[pointIndex++];
                const glm::vec2 end      = points_[pointIndex++];
                flattenCubic(current, control1, control2, end, tolerance, 0, maxDepth, result.back().points);
                current = end;
                break;
            }
            case Verb::Close: result.back().closed = true; break;
        }
    }
    return result;
}

}  // namespace eve::graphics

// PrimitiveScene.cpp
namespace eve::graphics {
namespace {

bool finite(glm::vec3 value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

bool nonZero(glm::vec3 value) {
    constexpr float minimumLengthSquared = 1e-12f;
    return finite(value) && glm::dot(value, value) > minimumLengthSquared;
}

bool independent(glm::vec3 a, glm::vec3 b) {
    constexpr float minimumRelativeAreaSquared = 1e-12f;
    const float     aLengthSquared             = glm::dot(a, a);
    const float     bLengthSquared             = glm::dot(b, b);
    const glm::vec3 crossValue                 = glm::cross(a, b);
    const float     areaSquared                = glm::dot(crossValue, crossValue);
    return aLengthSquared > 0.f && bLengthSquared > 0.f &&
           areaSquared > minimumRelativeAreaSquared * aLengthSquared * bLengthSquared;
}

bool formsBasis(glm::vec3 a, glm::vec3 b, glm::vec3 c) {
    constexpr float minimumRelativeVolumeSquared = 1e-12f;
    const float     volume                       = glm::dot(a, glm::cross(b, c));
    return volume * volume > minimumRelativeVolumeSquared * glm::dot(a, a) * glm::dot(b, b) * glm::dot(c, c);
}

eve::Result<void> validateDescriptor(const PrimitiveDescriptor3D& descriptor) {
    const auto& paint  = descriptor.paint;
    const auto& stroke = paint.stroke;
    if (!std::isfinite(stroke.width) || stroke.width < 0.f || !std::isfinite(stroke.miterLimit) ||
        stroke.miterLimit <= 0.f || !std::isfinite(paint.color.r) || !std::isfinite(paint.color.g) ||
        !std::isfinite(paint.color.b) || !std::isfinite(paint.color.a))
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "primitive paint must be finite and valid",
                                                                 "primitive", {}, "graphics"));
    if (stroke.dash) {
        const auto& dash   = *stroke.dash;
        float       period = 0.f;
        if (dash.intervals.empty() || dash.intervals.size() % 2 != 0 || !std::isfinite(dash.phase))
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "invalid primitive dash pattern", "primitive", {}, "graphics"));
        for (float interval : dash.intervals) {
            if (!std::isfinite(interval) || interval <= 0.f)
                return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                         "invalid primitive dash interval", "primitive",
                                                                         {}, "graphics"));
            period += interval;
        }
        if (!std::isfinite(period))
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "primitive dash period overflow", "primitive", {}, "graphics"));
    }
    try {
        descriptor.paint.validate();
    } catch (const std::exception& error) {
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, error.what(), "primitive", {}, "graphics"));
    }
    for (glm::length_t column = 0; column < 4; ++column)
        for (glm::length_t row = 0; row < 4; ++row)
            if (!std::isfinite(descriptor.transform[column][row]))
                return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                         "primitive transform must be finite",
                                                                         "primitive", {}, "graphics"));
    const bool valid = std::visit(
        [](const auto& geometry) {
            using Geometry = std::decay_t<decltype(geometry)>;
            if constexpr (std::is_same_v<Geometry, PrimitivePolyline3D>) {
                return geometry.points.size() >= (geometry.closed ? 3u : 2u) &&
                       std::all_of(geometry.points.begin(), geometry.points.end(), finite);
            } else if constexpr (std::is_same_v<Geometry, PrimitiveAabb3D>) {
                return finite(geometry.minimum) && finite(geometry.maximum) &&
                       geometry.minimum.x <= geometry.maximum.x && geometry.minimum.y <= geometry.maximum.y &&
                       geometry.minimum.z <= geometry.maximum.z;
            } else if constexpr (std::is_same_v<Geometry, PrimitiveObb3D>) {
                return finite(geometry.center) && nonZero(geometry.halfAxes[0]) && nonZero(geometry.halfAxes[1]) &&
                       nonZero(geometry.halfAxes[2]) && independent(geometry.halfAxes[0], geometry.halfAxes[1]) &&
                       independent(geometry.halfAxes[1], geometry.halfAxes[2]) &&
                       independent(geometry.halfAxes[2], geometry.halfAxes[0]) &&
                       formsBasis(geometry.halfAxes[0], geometry.halfAxes[1], geometry.halfAxes[2]);
            } else if constexpr (std::is_same_v<Geometry, PrimitiveDisk3D>) {
                return finite(geometry.center) && nonZero(geometry.normal) && std::isfinite(geometry.radius) &&
                       geometry.radius > 0.f && geometry.segments >= 3;
            } else if constexpr (std::is_same_v<Geometry, PrimitiveArc3D>) {
                return finite(geometry.center) && nonZero(geometry.normal) && nonZero(geometry.zeroDirection) &&
                       independent(geometry.normal, geometry.zeroDirection) && std::isfinite(geometry.radius) &&
                       geometry.radius > 0.f && std::isfinite(geometry.startRadians) &&
                       std::isfinite(geometry.sweepRadians) && geometry.segments >= 1;
            } else if constexpr (std::is_same_v<Geometry, PrimitiveSphere3D>) {
                return finite(geometry.center) && std::isfinite(geometry.radius) && geometry.radius > 0.f &&
                       geometry.segments >= 3;
            } else if constexpr (std::is_same_v<Geometry, PrimitiveCapsule3D> ||
                                 std::is_same_v<Geometry, PrimitiveCylinder3D>) {
                return finite(geometry.a) && finite(geometry.b) && nonZero(geometry.b - geometry.a) &&
                       std::isfinite(geometry.radius) && geometry.radius > 0.f && geometry.segments >= 3;
            } else if constexpr (std::is_same_v<Geometry, PrimitiveCone3D>) {
                return finite(geometry.apex) && nonZero(geometry.axis) && std::isfinite(geometry.height) &&
                       geometry.height > 0.f && std::isfinite(geometry.radius) && geometry.radius > 0.f &&
                       geometry.segments >= 3;
            } else if constexpr (std::is_same_v<Geometry, PrimitiveGrid3D>) {
                return finite(geometry.origin) && nonZero(geometry.axisU) && nonZero(geometry.axisV) &&
                       independent(geometry.axisU, geometry.axisV) && geometry.cellsU > 0 && geometry.cellsV > 0;
            } else if constexpr (std::is_same_v<Geometry, PrimitiveArrow3D>) {
                return finite(geometry.from) && finite(geometry.to) && nonZero(geometry.to - geometry.from) &&
                       std::isfinite(geometry.headLength) && geometry.headLength > 0.f &&
                       std::isfinite(geometry.headRadius) && geometry.headRadius > 0.f;
            } else {
                return std::all_of(geometry.corners.begin(), geometry.corners.end(), finite);
            }
        },
        descriptor.geometry);
    if (!valid)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "primitive geometry is invalid or non-finite",
                                                                 "primitive", {}, "graphics"));
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
            return eve::Result<PrimitiveHandle>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, "primitive scene slot capacity exceeded", "primitive", {}, "graphics"));
        }
        index = static_cast<PrimitiveHandle::index_type>(slots_.size());
        slots_.push_back(Slot{std::move(descriptor), 1, false, {}});
    }
    ++liveCount_;
    return eve::Result<PrimitiveHandle>::success(PrimitiveHandle(owner_, index, slots_[index].generation));
}

eve::Result<PrimitiveUpdateStatus> PrimitiveScene::update(PrimitiveHandle handle, PrimitiveDescriptor3D descriptor) {
    if (!matches(handle)) {
        return eve::Result<PrimitiveUpdateStatus>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "primitive handle is stale", "primitive", {}, "graphics"));
    }
    auto validation = validateDescriptor(descriptor);
    if (!validation) return eve::Result<PrimitiveUpdateStatus>::failure(validation.status());
    slots_[handle.index()].descriptor = std::move(descriptor);
    slots_[handle.index()].cache.reset();
    return eve::Result<PrimitiveUpdateStatus>::success(PrimitiveUpdateStatus::Updated);
}

eve::Result<PrimitiveRemoveStatus> PrimitiveScene::remove(PrimitiveHandle handle) {
    if (!matches(handle)) {
        return eve::Result<PrimitiveRemoveStatus>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "primitive handle is stale", "primitive", {}, "graphics"));
    }
    Slot& slot = slots_[handle.index()];
    slot.descriptor.reset();
    slot.cache.reset();
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
            return eve::Result<std::size_t>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                            "primitive batch contains a stale handle",
                                                                            "primitive", {}, "graphics"));
        auto validation = validateDescriptor(update.descriptor);
        if (!validation) return eve::Result<std::size_t>::failure(validation.status());
    }
    // Copy every owning payload before publishing any mutation. A later allocation
    // failure must not leave earlier descriptors in the batch already replaced.
    std::vector<PrimitiveBatchUpdate> prepared(updates.begin(), updates.end());
    static_assert(std::is_nothrow_move_assignable_v<PrimitiveDescriptor3D>);
    for (PrimitiveBatchUpdate& update : prepared) {
        *slots_[update.handle.index()].descriptor = std::move(update.descriptor);
        slots_[update.handle.index()].cache.reset();
    }
    return eve::Result<std::size_t>::success(updates.size());
}

const PrimitiveDescriptor3D* PrimitiveScene::tryGet(PrimitiveHandle handle) const noexcept {
    if (!matches(handle)) return nullptr;
    return &*slots_[handle.index()].descriptor;
}

bool PrimitiveScene::isStale(PrimitiveHandle handle) const noexcept { return !matches(handle); }

void PrimitiveScene::clear() {
    freeSlots_.clear();
    liveCount_ = 0;
    for (PrimitiveHandle::index_type index = 0; index < slots_.size(); ++index) {
        Slot& slot = slots_[index];
        slot.descriptor.reset();
        slot.cache.reset();
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

void PrimitiveScene::render(PrimitiveSceneCanvas3D& destination) const {
    auto result = tryRender(destination);
    std::move(result).expect("primitive scene exceeded its command budget");
}

eve::Result<void> PrimitiveScene::tryRender(PrimitiveSceneCanvas3D& destination) const {
    for (const Slot& slot : slots_) {
        if (!slot.descriptor || !slot.descriptor->visible) continue;
        const PrimitiveDescriptor3D& descriptor = *slot.descriptor;
        const bool                   hit        = slot.cache.has_value();
        if (!hit) {
            PrimitiveSceneCanvas3D canvas(destination.context_, std::numeric_limits<std::size_t>::max());
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
                                       geometry.startRadians, geometry.sweepRadians, descriptor.paint,
                                       geometry.segments);
                    } else if constexpr (std::is_same_v<Geometry, PrimitiveSphere3D>) {
                        canvas.drawSphere(geometry.center, geometry.radius, descriptor.paint, geometry.segments);
                    } else if constexpr (std::is_same_v<Geometry, PrimitiveCapsule3D>) {
                        canvas.drawCapsule(geometry.a, geometry.b, geometry.radius, descriptor.paint,
                                           geometry.segments);
                    } else if constexpr (std::is_same_v<Geometry, PrimitiveCylinder3D>) {
                        canvas.drawCylinder(geometry.a, geometry.b, geometry.radius, descriptor.paint,
                                            geometry.segments);
                    } else if constexpr (std::is_same_v<Geometry, PrimitiveCone3D>) {
                        canvas.drawCone(geometry.apex, geometry.axis, geometry.height, geometry.radius,
                                        descriptor.paint, geometry.segments);
                    } else if constexpr (std::is_same_v<Geometry, PrimitiveGrid3D>) {
                        canvas.drawGrid(geometry.origin, geometry.axisU, geometry.axisV, geometry.cellsU,
                                        geometry.cellsV, descriptor.paint);
                    } else if constexpr (std::is_same_v<Geometry, PrimitiveArrow3D>) {
                        canvas.drawArrow(geometry.from, geometry.to, geometry.headLength, geometry.headRadius,
                                         descriptor.paint);
                    } else if constexpr (std::is_same_v<Geometry, PrimitiveFrustum3D>) {
                        canvas.drawFrustum(geometry.corners, descriptor.paint);
                    }
                },
                descriptor.geometry);
            slot.cache = std::move(canvas);
        }
        const auto& cached = *slot.cache;
        const auto  count  = cached.commands_.size() + cached.triangles_.size();
        const auto  used   = destination.commands_.size() + destination.triangles_.size();
        if (used > destination.hardCommandLimit_ || count > destination.hardCommandLimit_ - used) {
            destination.statistics_.droppedCommands += count + cached.statistics_.droppedCommands;
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, "primitive scene command budget exceeded", "primitive", {}, "graphics"));
        }
        // Prepare all owning payloads and capacity before publishing this slot.
        auto lines     = cached.commands_;
        auto triangles = cached.triangles_;
        for (auto& command : lines) command.transform = destination.transform_ * command.transform;
        for (auto& command : triangles) command.transform = destination.transform_ * command.transform;
        destination.commands_.reserve(destination.commands_.size() + lines.size());
        destination.triangles_.reserve(destination.triangles_.size() + triangles.size());
        static_assert(std::is_nothrow_move_constructible_v<PolylineCommand3D>);
        static_assert(std::is_nothrow_move_constructible_v<TriangleCommand3D>);
        for (auto& command : lines) destination.commands_.push_back(std::move(command));
        for (auto& command : triangles) destination.triangles_.push_back(std::move(command));
        destination.statistics_.commandCount += cached.statistics_.commandCount;
        destination.statistics_.segmentCount += cached.statistics_.segmentCount;
        destination.statistics_.triangleCount += cached.statistics_.triangleCount;
        destination.statistics_.cacheHits += hit ? 1u : 0u;
    }
    return eve::Result<void>::success();
}

bool PrimitiveScene::matches(PrimitiveHandle handle) const noexcept {
    if (!handle.isValid() || handle.owner() != owner_ || handle.index() >= slots_.size()) return false;
    const Slot& slot = slots_[handle.index()];
    return !slot.retired && slot.descriptor.has_value() && slot.generation == handle.generation();
}

}  // namespace eve::graphics

// PrimitiveScriptBindings.cpp
namespace eve::graphics {
namespace {
struct ScriptPrimitive3D {
    ScriptPrimitive3D(std::weak_ptr<PrimitiveScene> sceneValue, PrimitiveHandle handleValue)
        : scene(std::move(sceneValue)), handle(handleValue) {}
    ~ScriptPrimitive3D() noexcept {
        if (auto owner = scene.lock(); owner && !owner->isStale(handle))
            owner->remove(handle).ignore("script primitive proxy destruction");
    }

    std::weak_ptr<PrimitiveScene> scene;
    PrimitiveHandle               handle;
};

eve::Result<PrimitiveDescriptor3D> primitiveDescriptor(ScriptPrimitive3D* value) {
    if (!value)
        return eve::Result<PrimitiveDescriptor3D>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "primitive proxy must not be null",
                                   "primitive", {}, "graphics.primitive.binding"));
    auto scene = value->scene.lock();
    if (!scene)
        return eve::Result<PrimitiveDescriptor3D>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "primitive scene owner no longer exists",
                                   "primitive", {}, "graphics.primitive.binding"));
    const PrimitiveDescriptor3D* descriptor = scene->tryGet(value->handle);
    if (!descriptor)
        return eve::Result<PrimitiveDescriptor3D>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "primitive handle is stale", "primitive", {},
                                   "graphics.primitive.binding"));
    return eve::Result<PrimitiveDescriptor3D>::success(*descriptor);
}

template <class Mutator>
eve::Result<PrimitiveUpdateStatus> mutatePrimitive(ScriptPrimitive3D* value, Mutator&& mutator) {
    auto descriptor = primitiveDescriptor(value);
    if (!descriptor) return eve::Result<PrimitiveUpdateStatus>::failure(descriptor.status());
    PrimitiveDescriptor3D copy = std::move(descriptor).takeValue();
    std::invoke(std::forward<Mutator>(mutator), copy);
    auto scene = value->scene.lock();
    if (!scene)
        return eve::Result<PrimitiveUpdateStatus>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "primitive scene owner no longer exists",
                                   "primitive", {}, "graphics.primitive.binding"));
    return scene->update(value->handle, std::move(copy));
}

ssq::Table makePrimitiveProxy(HSQUIRRELVM vm, const std::shared_ptr<PrimitiveScene>& scene,
                              eve::Result<PrimitiveHandle>&& result) {
    if (!result) return eve::script::projectStatusResult(vm, result.status());
    const PrimitiveHandle handle = std::move(result).takeValue();
    auto                  object = eve::script::makeOwnedSquirrelInstance<ScriptPrimitive3D>(
        vm, std::make_unique<ScriptPrimitive3D>(scene, handle));
    if (!object) {
        const eve::Status status = object.status();
        object.ignore("failed to create owned primitive proxy");
        scene->remove(handle).ignore("rollback failed primitive proxy allocation");
        return eve::script::projectStatusResult(vm, status);
    }
    auto projected = eve::script::projectStatusResult(vm, eve::Status::success(eve::StatusCode::Applied));
    projected.set("value", std::move(object).takeValue());
    projected.set("ownership", std::string("owned"));
    projected.set("owner", static_cast<std::int64_t>(handle.owner()));
    projected.set("index", static_cast<std::int64_t>(handle.index()));
    projected.set("generation", static_cast<std::int64_t>(handle.generation()));
    return projected;
}

ScenePrimitivePaint scriptPrimitivePaint(float r, float g, float b, float a, float width) {
    ScenePrimitivePaint paint;
    paint.color             = Color(r, g, b, a);
    paint.mode              = PaintMode::Stroke;
    paint.stroke.width      = width;
    paint.stroke.widthSpace = WidthSpace::ScreenPixels;
    return paint;
}

eve::Result<std::vector<glm::vec3>> scriptPoints(ssq::Array values, std::size_t minimum, std::size_t maximum) {
    if (values.size() % 3 != 0 || values.size() / 3 < minimum || values.size() / 3 > maximum)
        return eve::Result<std::vector<glm::vec3>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "expected a flat xyz array with the required point count", "points",
            {}, "graphics.primitive.binding"));
    std::vector<glm::vec3> points;
    points.reserve(values.size() / 3);
    try {
        for (std::size_t i = 0; i < values.size(); i += 3)
            points.push_back({values.get<float>(i), values.get<float>(i + 1), values.get<float>(i + 2)});
    } catch (const std::exception& error) {
        return eve::Result<std::vector<glm::vec3>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, error.what(), "points", {}, "graphics.primitive.binding"));
    }
    return eve::Result<std::vector<glm::vec3>>::success(std::move(points));
}
}  // namespace

void exposePrimitiveScriptBindings(ssq::Table& table, ssq::Class& cls) {
    const auto vm = table.getHandle();
    cls.addFunc("newPrimitiveGrid3D", [vm](Graphics* self, ssq::Array xyz, int cellsU, int cellsV, float r, float g,
                                           float b, float a, float width) {
        auto parsed = scriptPoints(xyz, 3, 3);
        if (!parsed) return eve::script::projectStatusResult(vm, parsed.status());
        auto points = std::move(parsed).takeValue();
        if (cellsU <= 0 || cellsV <= 0 || cellsU > 4096 || cellsV > 4096)
            return eve::script::projectStatusResult(
                vm, eve::Result<PrimitiveHandle>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "grid cells must be in [1,4096]",
                                               "cells", {}, "graphics.primitive.binding"))
                        .status());
        auto                  scene = self->getPrimitiveScene();
        PrimitiveDescriptor3D descriptor;
        descriptor.geometry = PrimitiveGrid3D{points[0], points[1], points[2], static_cast<std::uint32_t>(cellsU),
                                              static_cast<std::uint32_t>(cellsV)};
        descriptor.paint    = scriptPrimitivePaint(r, g, b, a, width);
        return makePrimitiveProxy(vm, scene, scene->add(std::move(descriptor)));
    });
    cls.addFunc("newPrimitiveArc3D", [vm](Graphics* self, ssq::Array xyz, float radius, float start, float sweep,
                                          float r, float g, float b, float a, float width) {
        auto parsed = scriptPoints(xyz, 3, 3);
        if (!parsed) return eve::script::projectStatusResult(vm, parsed.status());
        auto                  points = std::move(parsed).takeValue();
        auto                  scene  = self->getPrimitiveScene();
        PrimitiveDescriptor3D descriptor;
        descriptor.geometry = PrimitiveArc3D{points[0], points[1], points[2], radius, start, sweep, 32};
        descriptor.paint    = scriptPrimitivePaint(r, g, b, a, width);
        return makePrimitiveProxy(vm, scene, scene->add(std::move(descriptor)));
    });
    cls.addFunc("newPrimitivePolyline3D",
                [vm](Graphics* self, ssq::Array xyz, bool closed, float r, float g, float b, float a, float width) {
                    auto parsed = scriptPoints(xyz, closed ? 3 : 2, 65536);
                    if (!parsed) return eve::script::projectStatusResult(vm, parsed.status());
                    auto                  points = std::move(parsed).takeValue();
                    auto                  scene  = self->getPrimitiveScene();
                    PrimitiveDescriptor3D descriptor;
                    descriptor.geometry = PrimitivePolyline3D{points, closed};
                    descriptor.paint    = scriptPrimitivePaint(r, g, b, a, width);
                    return makePrimitiveProxy(vm, scene, scene->add(std::move(descriptor)));
                });
    cls.addFunc("newPrimitiveObb3D",
                [vm](Graphics* self, ssq::Array xyz, float r, float g, float b, float a, float width) {
                    auto parsed = scriptPoints(xyz, 4, 4);
                    if (!parsed) return eve::script::projectStatusResult(vm, parsed.status());
                    auto                  points = std::move(parsed).takeValue();
                    auto                  scene  = self->getPrimitiveScene();
                    PrimitiveDescriptor3D descriptor;
                    descriptor.geometry = PrimitiveObb3D{points[0], {points[1], points[2], points[3]}};
                    descriptor.paint    = scriptPrimitivePaint(r, g, b, a, width);
                    return makePrimitiveProxy(vm, scene, scene->add(std::move(descriptor)));
                });
    cls.addFunc("newPrimitiveFrustum3D",
                [vm](Graphics* self, ssq::Array xyz, float r, float g, float b, float a, float width) {
                    auto parsed = scriptPoints(xyz, 8, 8);
                    if (!parsed) return eve::script::projectStatusResult(vm, parsed.status());
                    auto                  points = std::move(parsed).takeValue();
                    auto                  scene  = self->getPrimitiveScene();
                    PrimitiveDescriptor3D descriptor;
                    descriptor.geometry = PrimitiveFrustum3D{
                        {points[0], points[1], points[2], points[3], points[4], points[5], points[6], points[7]}};
                    descriptor.paint = scriptPrimitivePaint(r, g, b, a, width);
                    return makePrimitiveProxy(vm, scene, scene->add(std::move(descriptor)));
                });
    cls.addFunc("newPrimitiveDisk3D", [vm](Graphics* self, float x, float y, float z, float nx, float ny, float nz,
                                           float radius, float r, float g, float b, float a, float width) {
        auto                  scene = self->getPrimitiveScene();
        PrimitiveDescriptor3D descriptor;
        descriptor.geometry = PrimitiveDisk3D{{x, y, z}, {nx, ny, nz}, radius, 32};
        descriptor.paint    = scriptPrimitivePaint(r, g, b, a, width);
        return makePrimitiveProxy(vm, scene, scene->add(std::move(descriptor)));
    });
    cls.addFunc("newPrimitiveCylinder3D",
                [vm](Graphics* self, float ax, float ay, float az, float bx, float by, float bz, float radius, float r,
                     float g, float b, float a, float width) {
                    auto                  scene = self->getPrimitiveScene();
                    PrimitiveDescriptor3D descriptor;
                    descriptor.geometry = PrimitiveCylinder3D{{ax, ay, az}, {bx, by, bz}, radius, 32};
                    descriptor.paint    = scriptPrimitivePaint(r, g, b, a, width);
                    return makePrimitiveProxy(vm, scene, scene->add(std::move(descriptor)));
                });
    cls.addFunc("newPrimitiveCapsule3D", [vm](Graphics* self, float ax, float ay, float az, float bx, float by,
                                              float bz, float radius, float r, float g, float b, float a, float width) {
        auto                  scene = self->getPrimitiveScene();
        PrimitiveDescriptor3D descriptor;
        descriptor.geometry = PrimitiveCapsule3D{{ax, ay, az}, {bx, by, bz}, radius, 32};
        descriptor.paint    = scriptPrimitivePaint(r, g, b, a, width);
        return makePrimitiveProxy(vm, scene, scene->add(std::move(descriptor)));
    });
    cls.addFunc("newPrimitiveCone3D",
                [vm](Graphics* self, float x, float y, float z, float dx, float dy, float dz, float height,
                     float radius, float r, float g, float b, float a, float width) {
                    auto                  scene = self->getPrimitiveScene();
                    PrimitiveDescriptor3D descriptor;
                    descriptor.geometry = PrimitiveCone3D{{x, y, z}, {dx, dy, dz}, height, radius, 32};
                    descriptor.paint    = scriptPrimitivePaint(r, g, b, a, width);
                    return makePrimitiveProxy(vm, scene, scene->add(std::move(descriptor)));
                });
    cls.addFunc("newPrimitiveArrow3D",
                [vm](Graphics* self, float ax, float ay, float az, float bx, float by, float bz, float headLength,
                     float headRadius, float r, float g, float b, float a, float width) {
                    auto                  scene = self->getPrimitiveScene();
                    PrimitiveDescriptor3D descriptor;
                    descriptor.geometry = PrimitiveArrow3D{{ax, ay, az}, {bx, by, bz}, headLength, headRadius};
                    descriptor.paint    = scriptPrimitivePaint(r, g, b, a, width);
                    return makePrimitiveProxy(vm, scene, scene->add(std::move(descriptor)));
                });
    auto primitive = table.addClass<ScriptPrimitive3D>(
        "Primitive3D", std::function<ScriptPrimitive3D*()>([]() { return nullptr; }), false);
    const auto primitiveTag = primitive.getTypeTag();
    cls.addFunc(
        "setPrimitiveTransforms3D", [vm, primitiveTag](Graphics* self, ssq::Array proxies, ssq::Array matrices) {
            auto failure = [vm](const std::string& message) {
                return eve::script::projectStatusResult(
                    vm, eve::Result<std::size_t>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                                 message, "transforms", {},
                                                                                 "graphics.primitive.binding"))
                            .status());
            };
            if (!self || proxies.size() > 65536 || matrices.size() != proxies.size() * 16)
                return failure("expected one column-major matrix per primitive, at most 65536 primitives");
            auto                              scene = self->getPrimitiveScene();
            std::vector<PrimitiveBatchUpdate> updates;
            updates.reserve(proxies.size());
            try {
                for (std::size_t i = 0; i < proxies.size(); ++i) {
                    auto object = proxies.get<ssq::Object>(i);
                    if (object.getType() != ssq::Type::INSTANCE || object.getTypeTag() != primitiveTag)
                        return failure("batch entries must be Primitive3D proxies");
                    auto* proxy      = object.toPtrUnsafe<ScriptPrimitive3D*>();
                    auto  descriptor = primitiveDescriptor(proxy);
                    if (!descriptor) return eve::script::projectStatusResult(vm, descriptor.status());
                    auto copy = std::move(descriptor).takeValue();
                    for (int column = 0; column < 4; ++column)
                        for (int row = 0; row < 4; ++row)
                            copy.transform[column][row] = matrices.get<float>(i * 16 + column * 4 + row);
                    updates.push_back({proxy->handle, std::move(copy)});
                }
            } catch (const std::exception& error) {
                return failure(error.what());
            }
            auto       result = scene->updateMany(updates);
            const auto status = result.status();
            if (!result) return eve::script::projectStatusResult(vm, status);
            const auto count     = std::move(result).takeValue();
            auto       projected = eve::script::projectStatusResult(vm, status);
            projected.set("count", static_cast<std::int64_t>(count));
            return projected;
        });
    primitive.addFunc("ownership", [](ScriptPrimitive3D*) { return std::string("owned"); });
    primitive.addFunc("setPolyline", [vm](ScriptPrimitive3D* value, ssq::Array xyz, bool closed) {
        auto parsed = scriptPoints(xyz, closed ? 3 : 2, 65536);
        if (!parsed) return eve::script::projectStatusResult(vm, parsed.status());
        auto       points = std::move(parsed).takeValue();
        auto       result = mutatePrimitive(value, [&](PrimitiveDescriptor3D& descriptor) {
            descriptor.geometry = PrimitivePolyline3D{std::move(points), closed};
        });
        const auto status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status);
    });
    primitive.addFunc("setLayer", [vm](ScriptPrimitive3D* value, std::int64_t layer) {
        if (layer < 0 || layer > 0xffffffffLL)
            return eve::script::projectStatusResult(
                vm, eve::Result<PrimitiveUpdateStatus>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "layer must fit uint32", "layer",
                                               {}, "graphics.primitive.binding"))
                        .status());
        auto       result = mutatePrimitive(value, [layer](PrimitiveDescriptor3D& descriptor) {
            descriptor.paint.layer = static_cast<std::uint32_t>(layer);
        });
        const auto status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status);
    });
    primitive.addFunc("setTransform", [vm](ScriptPrimitive3D* value, ssq::Array elements) {
        if (elements.size() != 16)
            return eve::script::projectStatusResult(
                vm, eve::Result<PrimitiveUpdateStatus>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                               "transform requires 16 column-major floats", "transform", {},
                                               "graphics.primitive.binding"))
                        .status());
        glm::mat4 transform(1.f);
        try {
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row) transform[column][row] = elements.get<float>(column * 4 + row);
        } catch (const std::exception& error) {
            return eve::script::projectStatusResult(
                vm, eve::Result<PrimitiveUpdateStatus>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, error.what(), "transform", {},
                                               "graphics.primitive.binding"))
                        .status());
        }
        auto result =
            mutatePrimitive(value, [&](PrimitiveDescriptor3D& descriptor) { descriptor.transform = transform; });
        const auto status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status);
    });
    primitive.addFunc("setWidthSpace", [vm](ScriptPrimitive3D* value, const std::string& mode) {
        std::optional<WidthSpace> parsed;
        if (mode == "screen") parsed = WidthSpace::ScreenPixels;
        if (mode == "world") parsed = WidthSpace::WorldUnits;
        if (!parsed)
            return eve::script::projectStatusResult(
                vm, eve::Result<PrimitiveUpdateStatus>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "unsupported setWidthSpace value",
                                               "setWidthSpace", {}, "graphics.primitive.binding"))
                        .status());
        auto result = mutatePrimitive(
            value, [mode = *parsed](PrimitiveDescriptor3D& descriptor) { descriptor.paint.stroke.widthSpace = mode; });
        const auto status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status);
    });
    primitive.addFunc("setPaintMode", [vm](ScriptPrimitive3D* value, const std::string& mode) {
        std::optional<PaintMode> parsed;
        if (mode == "fill") parsed = PaintMode::Fill;
        if (mode == "stroke") parsed = PaintMode::Stroke;
        if (mode == "fill-stroke") parsed = PaintMode::FillAndStroke;
        if (!parsed)
            return eve::script::projectStatusResult(
                vm, eve::Result<PrimitiveUpdateStatus>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "unsupported setPaintMode value",
                                               "setPaintMode", {}, "graphics.primitive.binding"))
                        .status());
        auto result = mutatePrimitive(
            value, [mode = *parsed](PrimitiveDescriptor3D& descriptor) { descriptor.paint.mode = mode; });
        const auto status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status);
    });
    primitive.addFunc("setLineCap", [vm](ScriptPrimitive3D* value, const std::string& mode) {
        std::optional<LineCap> parsed;
        if (mode == "butt") parsed = LineCap::Butt;
        if (mode == "square") parsed = LineCap::Square;
        if (mode == "round") parsed = LineCap::Round;
        if (!parsed)
            return eve::script::projectStatusResult(
                vm, eve::Result<PrimitiveUpdateStatus>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "unsupported setLineCap value",
                                               "setLineCap", {}, "graphics.primitive.binding"))
                        .status());
        auto result = mutatePrimitive(
            value, [mode = *parsed](PrimitiveDescriptor3D& descriptor) { descriptor.paint.stroke.cap = mode; });
        const auto status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status);
    });
    primitive.addFunc("setLineJoin", [vm](ScriptPrimitive3D* value, const std::string& mode) {
        std::optional<LineJoin> parsed;
        if (mode == "miter") parsed = LineJoin::Miter;
        if (mode == "bevel") parsed = LineJoin::Bevel;
        if (mode == "round") parsed = LineJoin::Round;
        if (!parsed)
            return eve::script::projectStatusResult(
                vm, eve::Result<PrimitiveUpdateStatus>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "unsupported setLineJoin value",
                                               "setLineJoin", {}, "graphics.primitive.binding"))
                        .status());
        auto result = mutatePrimitive(
            value, [mode = *parsed](PrimitiveDescriptor3D& descriptor) { descriptor.paint.stroke.join = mode; });
        const auto status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status);
    });
    primitive.addFunc("setCullMode", [vm](ScriptPrimitive3D* value, const std::string& mode) {
        std::optional<PrimitiveCullMode> parsed;
        if (mode == "none") parsed = PrimitiveCullMode::None;
        if (mode == "back") parsed = PrimitiveCullMode::Back;
        if (mode == "front") parsed = PrimitiveCullMode::Front;
        if (!parsed)
            return eve::script::projectStatusResult(
                vm, eve::Result<PrimitiveUpdateStatus>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "unsupported setCullMode value",
                                               "setCullMode", {}, "graphics.primitive.binding"))
                        .status());
        auto result = mutatePrimitive(
            value, [mode = *parsed](PrimitiveDescriptor3D& descriptor) { descriptor.paint.cull = mode; });
        const auto status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status);
    });
    primitive.addFunc("setBlendMode", [vm](ScriptPrimitive3D* value, const std::string& mode) {
        std::optional<BlendMode> parsed;
        if (mode == "alpha") parsed = BlendMode::Alpha;
        if (mode == "opaque") parsed = BlendMode::Opaque;
        if (mode == "additive") parsed = BlendMode::Additive;
        if (mode == "premultiplied") parsed = BlendMode::Premultiplied;
        if (mode == "multiply") parsed = BlendMode::Multiply;
        if (!parsed)
            return eve::script::projectStatusResult(
                vm, eve::Result<PrimitiveUpdateStatus>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "unsupported setBlendMode value",
                                               "setBlendMode", {}, "graphics.primitive.binding"))
                        .status());
        auto result = mutatePrimitive(
            value, [mode = *parsed](PrimitiveDescriptor3D& descriptor) { descriptor.paint.blend = mode; });
        const auto status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status);
    });
    primitive.addFunc("isStale", [](ScriptPrimitive3D* value) {
        if (!value) return true;
        auto scene = value->scene.lock();
        return !scene || scene->isStale(value->handle);
    });
    primitive.addFunc("remove", [vm](ScriptPrimitive3D* value) {
        if (!value)
            return eve::script::projectStatusResult(
                vm, eve::Result<PrimitiveRemoveStatus>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "primitive proxy must not be null",
                                               "primitive", {}, "graphics.primitive.binding"))
                        .status());
        auto scene = value->scene.lock();
        if (!scene)
            return eve::script::projectStatusResult(
                vm,
                eve::Result<PrimitiveRemoveStatus>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "primitive scene owner no longer exists",
                                           "primitive", {}, "graphics.primitive.binding"))
                    .status());
        auto              result = scene->remove(value->handle);
        const eve::Status status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status);
        std::move(result).takeValue();
        value->handle = {};
        return eve::script::projectStatusResult(vm, status);
    });
    primitive.addFunc("setVisible", [vm](ScriptPrimitive3D* value, bool visible) {
        auto result =
            mutatePrimitive(value, [visible](PrimitiveDescriptor3D& descriptor) { descriptor.visible = visible; });
        const eve::Status status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status);
    });
    primitive.addFunc("setColor", [vm](ScriptPrimitive3D* value, float r, float g, float b, float a) {
        auto result = mutatePrimitive(
            value, [=](PrimitiveDescriptor3D& descriptor) { descriptor.paint.color = Color(r, g, b, a); });
        const eve::Status status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status);
    });
    primitive.addFunc("setLineWidth", [vm](ScriptPrimitive3D* value, float width) {
        auto result = mutatePrimitive(
            value, [width](PrimitiveDescriptor3D& descriptor) { descriptor.paint.stroke.width = width; });
        const eve::Status status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status);
    });
    primitive.addFunc("setDash", [vm](ScriptPrimitive3D* value, float drawLength, float gapLength, float phase,
                                      const std::string& space) {
        if (space != "screen" && space != "world")
            return eve::script::projectStatusResult(
                vm,
                eve::Result<PrimitiveUpdateStatus>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "dash space must be screen or world",
                                           "dashSpace", {}, "graphics.primitive.binding"))
                    .status());
        auto              result = mutatePrimitive(value, [=](PrimitiveDescriptor3D& descriptor) {
            DashPattern dash;
            dash.intervals = {drawLength, gapLength};
            dash.phase     = phase;
            dash.space     = space == "screen" ? DashSpace::ScreenPixels : DashSpace::WorldUnits;
            descriptor.paint.stroke.dash = std::move(dash);
        });
        const eve::Status status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status);
    });
    primitive.addFunc("clearDash", [vm](ScriptPrimitive3D* value) {
        auto result =
            mutatePrimitive(value, [](PrimitiveDescriptor3D& descriptor) { descriptor.paint.stroke.dash.reset(); });
        const eve::Status status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status);
    });
    primitive.addFunc("setDepthMode", [vm](ScriptPrimitive3D* value, const std::string& mode) {
        std::optional<PrimitiveDepthMode> parsed;
        if (mode == "test-write")
            parsed = PrimitiveDepthMode::TestAndWrite;
        else if (mode == "test")
            parsed = PrimitiveDepthMode::TestOnly;
        else if (mode == "ignore")
            parsed = PrimitiveDepthMode::Ignore;
        if (!parsed)
            return eve::script::projectStatusResult(
                vm, eve::Result<PrimitiveUpdateStatus>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                               "depth mode must be test-write, test, or ignore", "depthMode", {},
                                               "graphics.primitive.binding"))
                        .status());
        auto result = mutatePrimitive(
            value, [mode = *parsed](PrimitiveDescriptor3D& descriptor) { descriptor.paint.depth = mode; });
        const eve::Status status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status);
    });
    primitive.addFunc("setObjectId", [vm](ScriptPrimitive3D* value, std::int64_t objectId) {
        if (objectId < 0)
            return eve::script::projectStatusResult(
                vm, eve::Result<PrimitiveUpdateStatus>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "object id must be non-negative",
                                               "objectId", {}, "graphics.primitive.binding"))
                        .status());
        auto              result = mutatePrimitive(value, [objectId](PrimitiveDescriptor3D& descriptor) {
            descriptor.paint.objectId = static_cast<std::uint64_t>(objectId);
        });
        const eve::Status status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status);
    });

    cls.addFunc("newPrimitiveLine3D", [vm](Graphics* self, float ax, float ay, float az, float bx, float by, float bz,
                                           float r, float g, float b, float a, float width) {
        auto                  scene = self->getPrimitiveScene();
        PrimitiveDescriptor3D descriptor;
        descriptor.geometry = PrimitivePolyline3D{{{ax, ay, az}, {bx, by, bz}}, false};
        descriptor.paint    = scriptPrimitivePaint(r, g, b, a, width);
        return makePrimitiveProxy(vm, scene, scene->add(std::move(descriptor)));
    });
    cls.addFunc("newPrimitiveSphere3D", [vm](Graphics* self, float x, float y, float z, float radius, float r, float g,
                                             float b, float a, float width) {
        auto                  scene = self->getPrimitiveScene();
        PrimitiveDescriptor3D descriptor;
        descriptor.geometry = PrimitiveSphere3D{{x, y, z}, radius, 32};
        descriptor.paint    = scriptPrimitivePaint(r, g, b, a, width);
        return makePrimitiveProxy(vm, scene, scene->add(std::move(descriptor)));
    });
    cls.addFunc("newPrimitiveAabb3D", [vm](Graphics* self, float minX, float minY, float minZ, float maxX, float maxY,
                                           float maxZ, float r, float g, float b, float a, float width) {
        auto                  scene = self->getPrimitiveScene();
        PrimitiveDescriptor3D descriptor;
        descriptor.geometry = PrimitiveAabb3D{{minX, minY, minZ}, {maxX, maxY, maxZ}};
        descriptor.paint    = scriptPrimitivePaint(r, g, b, a, width);
        return makePrimitiveProxy(vm, scene, scene->add(std::move(descriptor)));
    });
}
}  // namespace eve::graphics

// PrimitiveTypes.cpp
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
