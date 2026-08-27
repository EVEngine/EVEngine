#include "sensing/Targeting.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <set>
#include <string>
#include <type_traits>
#include <utility>

namespace eve::sensing {
namespace {

template <class T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

template <class T>
Result<T> failure(Status status) {
    return Result<T>::failure(std::move(status));
}

template <class T>
Result<T> unsupported(std::string message, std::string path = {}) {
    return failure<T>(DiagnosticCode::Unsupported, std::move(message), std::move(path));
}

bool isWorldSpace(CoordinateSpace space) noexcept {
    return space == CoordinateSpace::World2D || space == CoordinateSpace::World3D;
}

bool isGridSpace(CoordinateSpace space) noexcept {
    return space == CoordinateSpace::Grid2D || space == CoordinateSpace::Grid3D;
}

bool sameSpace(const TargetLocation& a, const TargetLocation& b) noexcept {
    return std::visit([](const auto& left, const auto& right) {
        return left.isValid() && right.isValid() && left.space() == right.space();
    }, a, b);
}

bool sameSpace(const TargetLocation& location, CoordinateSpace space) noexcept {
    return std::visit([space](const auto& value) { return value.isValid() && value.space() == space; },
                      location);
}

double distanceSquared(const TargetLocation& a, const TargetLocation& b) noexcept {
    return std::visit([](const auto& left, const auto& right) -> double {
        if constexpr (std::is_same_v<std::decay_t<decltype(left)>, WorldPoint> &&
                      std::is_same_v<std::decay_t<decltype(right)>, WorldPoint>) {
            const double dx = static_cast<double>(left.x()) - right.x();
            const double dy = static_cast<double>(left.y()) - right.y();
            const double dz = static_cast<double>(left.z()) - right.z();
            return dx * dx + dy * dy + dz * dz;
        } else if constexpr (std::is_same_v<std::decay_t<decltype(left)>, GridPoint> &&
                             std::is_same_v<std::decay_t<decltype(right)>, GridPoint>) {
            const double dx = static_cast<double>(left.x()) - right.x();
            const double dy = static_cast<double>(left.y()) - right.y();
            const double dz = static_cast<double>(left.z()) - right.z();
            return dx * dx + dy * dy + dz * dz;
        } else {
            return std::numeric_limits<double>::infinity();
        }
    }, a, b);
}

bool contains(const TargetingSpec& spec, const TargetLocation& location) noexcept {
    if (spec.worldArea) {
        if (const auto* point = std::get_if<WorldPoint>(&location)) return spec.worldArea->contains(*point);
        return false;
    }
    if (spec.gridArea) {
        if (const auto* point = std::get_if<GridPoint>(&location)) return spec.gridArea->contains(*point);
        return false;
    }
    return true;
}

bool containsAllTags(const std::vector<std::string>& candidate, const std::vector<std::string>& required,
                     const std::vector<std::string>& excluded) {
    const std::set<std::string> tags(candidate.begin(), candidate.end());
    for (const auto& tag : required)
        if (!tags.contains(tag)) return false;
    for (const auto& tag : excluded)
        if (tags.contains(tag)) return false;
    return true;
}

bool inZone(const TargetCandidate& candidate, const std::optional<ZoneRef>& zone) {
    if (!zone) return true;
    return std::find(candidate.zones.begin(), candidate.zones.end(), *zone) != candidate.zones.end();
}

bool matches(const TargetingQuery& query, const TargetCandidate& candidate) {
    if (!candidate.subject.isValid() || !sameSpace(candidate.location, query.spec.space)) return false;
    if (query.spec.domain != TargetDomain::Any && candidate.domain != query.spec.domain) return false;
    if (!containsAllTags(candidate.tags, query.spec.requiredTags, query.spec.excludedTags)) return false;
    if (!inZone(candidate, query.spec.zone) || !contains(query.spec, candidate.location)) return false;

    const double distance = std::sqrt(distanceSquared(query.originLocation, candidate.location));
    return distance >= static_cast<double>(query.spec.minRange) &&
           distance <= static_cast<double>(query.spec.maxRange);
}

Result<void> validateWorldPair(WorldPoint minimum, WorldPoint maximum, CoordinateSpace expected,
                               const char* operation) {
    if (!minimum.isValid() || !maximum.isValid() || minimum.space() != expected ||
        maximum.space() != expected)
        return failure<void>(DiagnosticCode::InvalidArgument,
                             std::string(operation) + " requires two points in the same coordinate space");
    if (minimum.x() > maximum.x() || minimum.y() > maximum.y() ||
        (expected == CoordinateSpace::World3D && minimum.z() > maximum.z()))
        return failure<void>(DiagnosticCode::InvalidArgument,
                             std::string(operation) + " requires minimum coordinates not greater than maximum");
    return Result<void>::success();
}

Result<void> validateGridPair(GridPoint minimum, GridPoint maximum, CoordinateSpace expected,
                              const char* operation) {
    if (!minimum.isValid() || !maximum.isValid() || minimum.space() != expected ||
        maximum.space() != expected)
        return failure<void>(DiagnosticCode::InvalidArgument,
                             std::string(operation) + " requires two points in the same grid space");
    if (minimum.x() > maximum.x() || minimum.y() > maximum.y() ||
        (expected == CoordinateSpace::Grid3D && minimum.z() > maximum.z()))
        return failure<void>(DiagnosticCode::InvalidArgument,
                             std::string(operation) + " requires minimum cells not greater than maximum");
    return Result<void>::success();
}

}  // namespace

std::optional<ZoneRef> ZoneRef::fromLogicalId(LogicalId id) {
    if (!id.isValid()) return std::nullopt;
    return ZoneRef(std::move(id));
}

Result<WorldPoint> WorldPoint::world2D(float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y))
        return failure<WorldPoint>(DiagnosticCode::InvalidArgument, "World2D coordinates must be finite");
    return Result<WorldPoint>::success(WorldPoint(CoordinateSpace::World2D, x, y, 0.f));
}

Result<WorldPoint> WorldPoint::world3D(float x, float y, float z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return failure<WorldPoint>(DiagnosticCode::InvalidArgument, "World3D coordinates must be finite");
    return Result<WorldPoint>::success(WorldPoint(CoordinateSpace::World3D, x, y, z));
}

GridPoint GridPoint::grid2D(std::int32_t x, std::int32_t y) noexcept {
    return GridPoint(CoordinateSpace::Grid2D, x, y, 0);
}

GridPoint GridPoint::grid3D(std::int32_t x, std::int32_t y, std::int32_t z) noexcept {
    return GridPoint(CoordinateSpace::Grid3D, x, y, z);
}

Result<WorldArea> WorldArea::circle2D(WorldPoint center, float radius) {
    if (!center.isValid() || center.space() != CoordinateSpace::World2D || !std::isfinite(radius) || radius < 0.f)
        return failure<WorldArea>(DiagnosticCode::InvalidArgument,
                                  "WorldArea.circle2D requires a finite World2D center and non-negative radius");
    return Result<WorldArea>::success(WorldArea(Shape::Circle2D, center, {}, radius));
}

Result<WorldArea> WorldArea::box2D(WorldPoint minimum, WorldPoint maximum) {
    auto valid = validateWorldPair(minimum, maximum, CoordinateSpace::World2D, "WorldArea.box2D");
    if (!valid) return failure<WorldArea>(valid.status());
    return Result<WorldArea>::success(WorldArea(Shape::Box2D, minimum, maximum, 0.f));
}

Result<WorldArea> WorldArea::sphere3D(WorldPoint center, float radius) {
    if (!center.isValid() || center.space() != CoordinateSpace::World3D || !std::isfinite(radius) || radius < 0.f)
        return failure<WorldArea>(DiagnosticCode::InvalidArgument,
                                  "WorldArea.sphere3D requires a finite World3D center and non-negative radius");
    return Result<WorldArea>::success(WorldArea(Shape::Sphere3D, center, {}, radius));
}

Result<WorldArea> WorldArea::box3D(WorldPoint minimum, WorldPoint maximum) {
    auto valid = validateWorldPair(minimum, maximum, CoordinateSpace::World3D, "WorldArea.box3D");
    if (!valid) return failure<WorldArea>(valid.status());
    return Result<WorldArea>::success(WorldArea(Shape::Box3D, minimum, maximum, 0.f));
}

bool WorldArea::contains(WorldPoint point) const noexcept {
    if (!valid_ || !point.isValid() || point.space() != space_) return false;
    if (shape_ == Shape::Circle2D || shape_ == Shape::Sphere3D) {
        const double dx = static_cast<double>(point.x()) - first_.x();
        const double dy = static_cast<double>(point.y()) - first_.y();
        const double dz = static_cast<double>(point.z()) - first_.z();
        const double distance = dx * dx + dy * dy + dz * dz;
        return distance <= static_cast<double>(radius_) * radius_;
    }
    return point.x() >= first_.x() && point.x() <= second_.x() && point.y() >= first_.y() &&
           point.y() <= second_.y() &&
           (shape_ == Shape::Box2D || (point.z() >= first_.z() && point.z() <= second_.z()));
}

Result<GridArea> GridArea::box2D(GridPoint minimum, GridPoint maximum) {
    auto valid = validateGridPair(minimum, maximum, CoordinateSpace::Grid2D, "GridArea.box2D");
    if (!valid) return failure<GridArea>(valid.status());
    return Result<GridArea>::success(GridArea(Shape::Box2D, minimum, maximum));
}

Result<GridArea> GridArea::box3D(GridPoint minimum, GridPoint maximum) {
    auto valid = validateGridPair(minimum, maximum, CoordinateSpace::Grid3D, "GridArea.box3D");
    if (!valid) return failure<GridArea>(valid.status());
    return Result<GridArea>::success(GridArea(Shape::Box3D, minimum, maximum));
}

bool GridArea::contains(GridPoint point) const noexcept {
    if (!valid_ || !point.isValid() || point.space() != space_) return false;
    return point.x() >= minimum_.x() && point.x() <= maximum_.x() && point.y() >= minimum_.y() &&
           point.y() <= maximum_.y() &&
           (shape_ == Shape::Box2D || (point.z() >= minimum_.z() && point.z() <= maximum_.z()));
}

Result<void> TargetingSpec::validate() const {
    if ((isWorldSpace(space) && gridArea) || (isGridSpace(space) && worldArea))
        return failure<void>(DiagnosticCode::InvalidArgument, "TargetingSpec area uses a different coordinate space");
    if (worldArea && !worldArea->isValid())
        return failure<void>(DiagnosticCode::InvalidArgument, "TargetingSpec world area must be valid");
    if (gridArea && !gridArea->isValid())
        return failure<void>(DiagnosticCode::InvalidArgument, "TargetingSpec grid area must be valid");
    if (worldArea && worldArea->space() != space)
        return failure<void>(DiagnosticCode::InvalidArgument, "TargetingSpec world area does not match its space");
    if (gridArea && gridArea->space() != space)
        return failure<void>(DiagnosticCode::InvalidArgument, "TargetingSpec grid area does not match its space");
    if (minCount > maxCount)
        return failure<void>(DiagnosticCode::InvalidArgument, "TargetingSpec minCount exceeds maxCount");
    if (!std::isfinite(minRange) ||
        (!(std::isfinite(maxRange) || maxRange == std::numeric_limits<float>::infinity())) ||
        minRange < 0.f || maxRange < minRange)
        return failure<void>(DiagnosticCode::InvalidArgument, "TargetingSpec range must be finite and ordered");
    if (zone && !zone->isValid())
        return failure<void>(DiagnosticCode::InvalidArgument, "TargetingSpec zone must be valid");
    for (const auto& tag : requiredTags)
        if (tag.empty())
            return failure<void>(DiagnosticCode::InvalidArgument,
                                "requiredTags cannot contain empty keys");
    for (const auto& tag : excludedTags)
        if (tag.empty())
            return failure<void>(DiagnosticCode::InvalidArgument,
                                "excludedTags cannot contain empty keys");
    return Result<void>::success();
}

Result<void> TargetingQuery::validate() const {
    auto specResult = spec.validate();
    if (!specResult) return failure<void>(specResult.status());
    if (!origin.isValid())
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "TargetingQuery origin must be non-nil");
    if (!sameSpace(originLocation, spec.space))
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "TargetingQuery origin and spec use different coordinate spaces");
    return Result<void>::success();
}

Result<void> TargetSet::addSubject(SubjectRef subject) {
    if (!subject.isValid()) return failure<void>(DiagnosticCode::InvalidArgument, "TargetSet subject must be non-nil");
    if (std::find(subjects_.begin(), subjects_.end(), subject) != subjects_.end())
        return Result<void>::success(Status::success(StatusCode::NoOp));
    subjects_.push_back(subject);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> TargetSet::setPrimary(SubjectRef subject) {
    if (!subject.isValid()) return failure<void>(DiagnosticCode::InvalidArgument, "TargetSet primary must be non-nil");
    if (std::find(subjects_.begin(), subjects_.end(), subject) == subjects_.end())
        return failure<void>(DiagnosticCode::NotFound, "TargetSet primary must already be a member");
    primary_ = subject;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> TargetSet::setPoint(TargetLocation point) {
    if (!std::visit([](const auto& value) { return value.isValid(); }, point))
        return failure<void>(DiagnosticCode::InvalidArgument, "TargetSet point must be valid");
    if (point_ && !sameSpace(*point_, point))
        return failure<void>(DiagnosticCode::InvalidArgument, "TargetSet points cannot mix coordinate spaces");
    if (worldArea_ && (!std::get_if<WorldPoint>(&point) ||
                       std::get<WorldPoint>(point).space() != worldArea_->space()))
        return failure<void>(DiagnosticCode::InvalidArgument, "TargetSet point and world area use different spaces");
    if (gridArea_ && (!std::get_if<GridPoint>(&point) ||
                      std::get<GridPoint>(point).space() != gridArea_->space()))
        return failure<void>(DiagnosticCode::InvalidArgument, "TargetSet point and grid area use different spaces");
    point_ = std::move(point);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> TargetSet::setArea(WorldArea area) {
    if (!area.isValid()) return failure<void>(DiagnosticCode::InvalidArgument, "TargetSet world area must be valid");
    if (gridArea_) return failure<void>(DiagnosticCode::InvalidArgument, "TargetSet cannot mix world and grid areas");
    if (point_ && (!std::get_if<WorldPoint>(&*point_) ||
                   std::get<WorldPoint>(*point_).space() != area.space()))
        return failure<void>(DiagnosticCode::InvalidArgument, "TargetSet point and world area use different spaces");
    worldArea_ = std::move(area);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> TargetSet::setArea(GridArea area) {
    if (!area.isValid()) return failure<void>(DiagnosticCode::InvalidArgument, "TargetSet grid area must be valid");
    if (worldArea_) return failure<void>(DiagnosticCode::InvalidArgument, "TargetSet cannot mix world and grid areas");
    if (point_ && (!std::get_if<GridPoint>(&*point_) ||
                   std::get<GridPoint>(*point_).space() != area.space()))
        return failure<void>(DiagnosticCode::InvalidArgument, "TargetSet point and grid area use different spaces");
    gridArea_ = std::move(area);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> SensingCandidateProvider::upsert(TargetCandidate candidate) {
    if (!candidate.subject.isValid())
        return failure<void>(DiagnosticCode::InvalidArgument, "candidate subject must be non-nil");
    if (!std::visit([](const auto& value) { return value.isValid(); }, candidate.location))
        return failure<void>(DiagnosticCode::InvalidArgument, "candidate location must be valid");
    for (const auto& tag : candidate.tags)
        if (tag.empty())
            return failure<void>(DiagnosticCode::InvalidArgument,
                                "candidate tags cannot contain empty keys");
    for (const auto& zone : candidate.zones)
        if (!zone.isValid()) return failure<void>(DiagnosticCode::InvalidArgument, "candidate zones must be valid");
    candidates_[candidate.subject.format()] = std::move(candidate);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> SensingCandidateProvider::remove(SubjectRef subject) {
    if (!subject.isValid()) return failure<void>(DiagnosticCode::InvalidArgument, "candidate subject must be non-nil");
    if (candidates_.erase(subject.format()) == 0)
        return failure<void>(DiagnosticCode::NotFound, "candidate subject was not registered");
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<std::vector<TargetCandidate>> SensingCandidateProvider::query(const TargetingQuery& query) const {
    auto valid = query.validate();
    if (!valid) return failure<std::vector<TargetCandidate>>(valid.status());
    std::vector<TargetCandidate> result;
    result.reserve(candidates_.size());
    for (const auto& [key, candidate] : candidates_) {
        (void)key;
        if (matches(query, candidate)) result.push_back(candidate);
    }
    return Result<std::vector<TargetCandidate>>::success(std::move(result));
}

Result<TargetSet> TargetingResolver::resolve(const TargetingQuery& query) const {
    auto valid = query.validate();
    if (!valid) return failure<TargetSet>(valid.status());

    auto* provider = cap::query<ISensingCandidateProvider>();
    if (!provider) return unsupported<TargetSet>("Targeting requires an ISensingCandidateProvider");

    auto candidateResult = provider->query(query);
    if (!candidateResult) return failure<TargetSet>(candidateResult.status());
    auto candidates = std::move(candidateResult).takeValue();

    ILineOfSightQuery* los = nullptr;
    if (query.spec.lineOfSight == LineOfSightMode::Required) {
        los = cap::query<ILineOfSightQuery>();
        if (!los) return unsupported<TargetSet>("Targeting line-of-sight was requested but no provider is registered");
    }

    TargetSet result;
    for (const auto& candidate : candidates) {
        if (!candidate.subject.isValid() ||
            !std::visit([](const auto& value) { return value.isValid(); }, candidate.location))
            return failure<TargetSet>(DiagnosticCode::InvariantViolation,
                                      "candidate provider returned an invalid candidate");
        if (!sameSpace(candidate.location, query.spec.space))
            return failure<TargetSet>(DiagnosticCode::InvariantViolation, "candidate provider mixed coordinate spaces");
        if (!matches(query, candidate)) continue;
        if (los) {
            auto visible = los->query(query.originLocation, candidate.location);
            if (!visible) return failure<TargetSet>(visible.status());
            if (!std::move(visible).takeValue().visible) continue;
        }
        auto added = result.addSubject(candidate.subject);
        std::move(added).expect("TargetingResolver could not add a validated candidate");
    }

    if (result.subjects().size() < query.spec.minCount || result.subjects().size() > query.spec.maxCount)
        return failure<TargetSet>(DiagnosticCode::PreconditionViolation,
                                  "target candidate count violates TargetingSpec");
    return Result<TargetSet>::success(std::move(result));
}

std::string_view coordinateSpaceName(CoordinateSpace space) noexcept {
    switch (space) {
    case CoordinateSpace::World2D: return "world2d";
    case CoordinateSpace::World3D: return "world3d";
    case CoordinateSpace::Grid2D: return "grid2d";
    case CoordinateSpace::Grid3D: return "grid3d";
    }
    return "unknown";
}

}  // namespace eve::sensing
