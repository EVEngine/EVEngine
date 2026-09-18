/**
 * @file LineOfSightRouter.cpp
 * @brief Space-routing line-of-sight provider.
 */

#include "sensing/LineOfSightRouter.h"

#include "common/Diagnostic.h"

#include <optional>
#include <utility>

namespace eve::sensing {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

/** @brief Stable path name for a rejected location, so diagnostics point at the right argument. */
std::string locationPath(const char* name) { return std::string("lineOfSight.") + name; }

}  // namespace

std::optional<CoordinateSpace> LineOfSightRouter::spaceOf(const TargetLocation& location) noexcept {
    return std::visit(
        [](const auto& point) -> std::optional<CoordinateSpace> { return point.space(); }, location);
}

Result<void> LineOfSightRouter::addProvider(CoordinateSpace space, const ILineOfSightQuery* provider) {
    if (provider == nullptr)
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "line-of-sight backend must not be null", "lineOfSight.provider");
    const auto index = static_cast<std::size_t>(space);
    if (index >= providers_.size())
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "unknown line-of-sight coordinate space", "lineOfSight.space");
    if (providers_[index] != nullptr)
        // Replace would make the effective backend depend on registration order, which is the
        // problem this router exists to remove.
        return failure<void>(DiagnosticCode::Conflict,
                             "line-of-sight coordinate space already has a backend", "lineOfSight.space");
    providers_[index] = provider;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> LineOfSightRouter::removeProvider(CoordinateSpace space, const ILineOfSightQuery* provider) {
    const auto index = static_cast<std::size_t>(space);
    if (index >= providers_.size() || providers_[index] != provider)
        return Result<void>::success(Status::success(StatusCode::NoOp));
    providers_[index] = nullptr;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

bool LineOfSightRouter::hasProvider(CoordinateSpace space) const noexcept {
    const auto index = static_cast<std::size_t>(space);
    return index < providers_.size() && providers_[index] != nullptr;
}

std::vector<CoordinateSpace> LineOfSightRouter::spaces() const {
    std::vector<CoordinateSpace> claimed;
    for (std::size_t index = 0; index < providers_.size(); ++index) {
        if (providers_[index] != nullptr) claimed.push_back(static_cast<CoordinateSpace>(index));
    }
    return claimed;
}

Result<LineOfSightResult> LineOfSightRouter::query(const TargetLocation& from,
                                                  const TargetLocation& to) const {
    const auto fromSpace = spaceOf(from);
    const auto toSpace   = spaceOf(to);
    if (!fromSpace || !toSpace)
        return failure<LineOfSightResult>(DiagnosticCode::InvalidArgument,
                                          "line-of-sight endpoints must be valid points",
                                          locationPath("from"));
    if (*fromSpace != *toSpace)
        // The interface forbids converting between spaces, so a mixed query is a caller error
        // rather than something to approximate.
        return failure<LineOfSightResult>(DiagnosticCode::InvalidArgument,
                                          "line-of-sight endpoints must share a coordinate space",
                                          locationPath("to"));
    const auto index = static_cast<std::size_t>(*fromSpace);
    if (index >= providers_.size() || providers_[index] == nullptr)
        return failure<LineOfSightResult>(DiagnosticCode::Unsupported,
                                          "no line-of-sight backend for coordinate space " +
                                              std::string(coordinateSpaceName(*fromSpace)),
                                          locationPath("from"));
    return providers_[index]->query(from, to);
}

}  // namespace eve::sensing
