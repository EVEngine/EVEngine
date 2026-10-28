// eve-hot-path
#include "common/Profile.h"
#include "crowd/CrowdInternal.h"

#include <exception>
#include <limits>

namespace eve::crowd {
namespace {
constexpr int32_t      seekAction            = 2;
constexpr float        predictionAngles[]    = {0.f,         -0.2617994f, 0.2617994f,  -0.5235988f, 0.5235988f,
                                                -0.7853982f, 0.7853982f,  -1.0471976f, 1.0471976f,  -1.5707963f,
                                                1.5707963f,  -2.3561945f, 2.3561945f,  3.1415927f};
constexpr float        predictionFractions[] = {1.f, 0.5f, 0.25f};
constexpr std::int64_t scoresPerNeighbor     = 3 + std::size(predictionAngles) * std::size(predictionFractions);
}  // namespace


Result<void> Crowd::configureAvoidance(AvoidanceSettings settings) {
    if (!std::isfinite(settings.horizon) || settings.horizon <= 0.f || settings.horizon > 10.f ||
        !std::isfinite(settings.margin) || settings.margin < 0.f || settings.maxNeighbors < 1 ||
        settings.maxNeighbors > 128)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid predictive avoidance settings", "avoidance"));
    impl_->avoidance = settings;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

AvoidanceSettings Crowd::getAvoidanceSettings() const { return impl_->avoidance; }

void Crowd::Impl::prepareAvoidanceNeighbors() {
    EV_PROFILE_MODULE("crowd", "Crowd::prepareAvoidanceNeighbors");
    // Arrival/settled tests depend only on a neighbour's own state, not the querying agent.
    std::vector<std::uint8_t> settled(actions.size(), 0);
    for (size_t j = 0; j < actions.size(); ++j) {
        if (interactions[j].holdPosition || interactions[j].pushability <= 0.f) continue;
        settled[j] = actions[j] == 0 || (actions[j] == seekAction && hasTargets[j] &&
                                         std::hypot(targetXs[j] - xs[j], targetYs[j] - ys[j]) <= arriveRadius &&
                                         std::hypot(vxs[j], vys[j]) <= maxSpeeds[j] * 0.05f);
    }
    predictionNeighbors.resize(actions.size());
    const auto prepareRange = [&](size_t first, size_t last, StepReport* partitionReport, std::int64_t& checks,
                                  int& truncations) {
        for (size_t i = first; i < last; ++i) {
            using Neighbor  = AvoidanceNeighbor;
            auto& neighbors = predictionNeighbors[i];
            neighbors.clear();
            if (maxSpeeds[i] <= 0.f || interactions[i].holdPosition) continue;
            neighbors.reserve(size_t(avoidance.maxNeighbors) + 1);
            const double query = double(radii[i]) + avoidanceMaxRadius + avoidance.margin +
                                 double(avoidance.horizon) * (double(maxSpeeds[i]) + avoidanceMaxSpeed);
            const bool arriving    = resolveOverlaps && actions[i] == seekAction && hasTargets[i];
            int        encountered = 0;
            const auto nearer      = [&](const Neighbor& a, const Neighbor& b) {
                if (a.distance2 != b.distance2) return a.distance2 < b.distance2;
                return tieKeys[a.slot] < tieKeys[b.slot];
            };
            const double queryRadius = float(std::min(query, double(std::numeric_limits<float>::max())));
            forEachNeighbor(
                xs[i], ys[i], float(queryRadius),
                [&](int slot) {
                    const size_t j = size_t(slot);
                    if (i == j || !canAvoid(i, j)) return;
                    // Settled, pushable neighbours yield through the contact solver during final arrival.
                    // Held bodies and pairs without physical contact still require predictive avoidance.
                    if (arriving && (settled[j] || maxSpeeds[j] <= maxSpeeds[i] * 0.05f) &&
                        !interactions[j].holdPosition && interactions[j].pushability > 0.f && canContact(i, j))
                        return;
                    ++encountered;
                    const double dx = double(xs[j]) - xs[i], dy = double(ys[j]) - ys[i];
                    Neighbor     neighbor{j, dx * dx + dy * dy};
                    if (neighbors.size() == size_t(avoidance.maxNeighbors) && !nearer(neighbor, neighbors.back()))
                        return;
                    const auto position = std::lower_bound(neighbors.begin(), neighbors.end(), neighbor, nearer);
                    if (position != neighbors.end() || neighbors.size() < size_t(avoidance.maxNeighbors)) {
                        neighbors.insert(position, neighbor);
                        if (neighbors.size() > size_t(avoidance.maxNeighbors)) neighbors.pop_back();
                    }
                },
                partitionReport);
            if (encountered > avoidance.maxNeighbors) ++truncations;
            checks += static_cast<std::int64_t>(neighbors.size()) * scoresPerNeighbor;
        }
    };
    std::int64_t checks = 0;
    if (execution == CrowdExecution::Serial) {
        prepareRange(0, actions.size(), nullptr, checks, avoidanceTruncations);
    } else {
        constexpr size_t grain = 256;
        struct Partition {
            StepReport         report;
            std::int64_t       checks      = 0;
            int                truncations = 0;
            std::exception_ptr error;
        };
        std::vector<Partition> partitions((actions.size() + grain - 1) / grain);
        dispatchWork(actions.size(), [&](size_t first, size_t last) {
            auto& partition = partitions[first / grain];
            try {
                prepareRange(first, last, &partition.report, partition.checks, partition.truncations);
            } catch (...) {
                partition.error = std::current_exception();
            }
        });
        for (const auto& partition : partitions) {
            if (partition.error) std::rethrow_exception(partition.error);
            if (budgetActive &&
                partition.report.spatialVisits > activeBudget.maxSpatialVisits - workReport.spatialVisits)
                throw WorkLimit{"Crowd spatial lookup budget exhausted"};
            workReport.spatialVisits += partition.report.spatialVisits;
            workReport.cellsVisited += partition.report.cellsVisited;
            if (budgetActive &&
                partition.report.candidatesVisited > activeBudget.maxNeighborVisits - workReport.candidatesVisited)
                throw WorkLimit{"Crowd neighbor visit budget exhausted"};
            workReport.candidatesVisited += partition.report.candidatesVisited;
            avoidanceTruncations += partition.truncations;
            checks += partition.checks;
        }
    }
    if (budgetActive && checks > activeBudget.maxPredictionChecks - avoidanceChecks)
        throw WorkLimit{"Crowd prediction check budget exhausted"};
    avoidanceChecks += checks;
}

void Crowd::Impl::selectAvoidanceVelocity(size_t i, float dt, float& vx, float& vy) {
    const auto& neighbors = predictionNeighbors[i];
    if (neighbors.empty()) return;


    const float speed      = maxSpeeds[i];
    const float preferredX = preferredVxs[i], preferredY = preferredVys[i];
    const float preferredLength = std::hypot(preferredX, preferredY);
    const float baseAngle       = preferredLength > 1e-5f ? std::atan2(preferredY, preferredX) : headings[i];
    const float acceleration    = maxAccels[i] * dt;
    const auto  reachable       = [&](float& x, float& y) {
        const float changeX = x - vxs[i], changeY = y - vys[i];
        const float change = std::hypot(changeX, changeY);
        if (change > acceleration && change > 0.f) {
            x = vxs[i] + changeX * acceleration / change;
            y = vys[i] + changeY * acceleration / change;
        }
        const float length = std::hypot(x, y);
        if (length > speed) {
            x *= speed / length;
            y *= speed / length;
        }
    };
    const auto score = [&](float x, float y, double cutoff) {
        double cost =
            std::hypot(x - preferredX, y - preferredY) / speed + 0.15 * std::hypot(x - vxs[i], y - vys[i]) / speed;
        // A small, persistent right-hand preference breaks mirror symmetry.
        if (preferredLength > 1e-5f)
            cost += 0.05 * std::max(0.f, preferredX * y - preferredY * x) / (preferredLength * speed);
        // Remaining terms can only add cost. A candidate already above the winner cannot improve it.
        if (cost >= cutoff) return cost;
        for (const auto& neighbor : neighbors) {
            const size_t j  = neighbor.slot;
            const double px = double(xs[j]) - xs[i], py = double(ys[j]) - ys[i];
            const double dx = double(x) - vxs[j], dy = double(y) - vys[j];
            const double radius = double(radii[i]) + radii[j] + avoidance.margin;
            const double c      = neighbor.distance2 - radius * radius;
            const double a = dx * dx + dy * dy, b = px * dx + py * dy;
            double       collision = double(avoidance.horizon) + 1.0;
            if (c < 0.0) {
                // Prefer opening an existing contact, rather than freezing all candidates.
                collision = 0.0;
                cost += 2.0 * std::max(0.0, b) / (std::max(radius, 1e-6) * speed);
            } else if (a > 1e-10 && b > 0.0) {
                const double discriminant = b * b - a * c;
                if (discriminant >= 0.0) collision = (b - std::sqrt(discriminant)) / a;
            }
            if (collision >= 0.0 && collision < avoidance.horizon) {
                const double urgency = 1.0 - collision / avoidance.horizon;
                // Priority adjusts yielding preference; it never removes collision cost.
                const double priority =
                    std::clamp(double(avoidancePriorities[j]) - avoidancePriorities[i], -100.0, 100.0);
                cost += (20.0 + 0.1 * priority) * urgency * urgency;
            }
            if (cost >= cutoff) return cost;
        }
        if (field.valid()) {
            float px = xs[i] + x * dt, py = ys[i] + y * dt;
            float projectedX = px, projectedY = py;
            field.resolvePenetration(projectedX, projectedY, radii[i]);
            if (std::hypot(projectedX - px, projectedY - py) > 1e-4f) cost += 100.0;
            if (clampToField && (px - radii[i] < field.getOriginX() || py - radii[i] < field.getOriginY() ||
                                 px + radii[i] > field.getOriginX() + float(field.getWidth()) * field.getCellSize() ||
                                 py + radii[i] > field.getOriginY() + float(field.getHeight()) * field.getCellSize()))
                cost += 100.0;
        }
        return cost;
    };
    double best = score(vx, vy, std::numeric_limits<double>::infinity());
    // Every score term is non-negative; a zero-cost current choice cannot be improved.
    if (best <= 1e-9) return;
    const auto consider = [&](float x, float y) {
        reachable(x, y);
        const double value = score(x, y, best - 1e-9);
        if (value < best - 1e-9) {
            best = value;
            vx   = x;
            vy   = y;
        }
    };
    consider(0.f, 0.f);
    consider(vxs[i], vys[i]);
    const float samplingSpeed = std::max(preferredLength, speed * 0.25f);
    for (float fraction : predictionFractions)
        for (float angle : predictionAngles)
            consider(std::cos(baseAngle + angle) * samplingSpeed * fraction,
                     std::sin(baseAngle + angle) * samplingSpeed * fraction);
}

}  // namespace eve::crowd
