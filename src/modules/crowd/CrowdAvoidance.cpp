#include "crowd/CrowdInternal.h"

#include <limits>

namespace eve::crowd {

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

void Crowd::Impl::selectAvoidanceVelocity(size_t i, float dt, float& vx, float& vy) {
    if (maxSpeeds[i] <= 0.f) return;
    using Neighbor  = AvoidanceNeighbor;
    auto& neighbors = avoidanceNeighbors;
    neighbors.clear();
    neighbors.reserve(size_t(avoidance.maxNeighbors) + 1);
    const double query = double(radii[i]) + avoidanceMaxRadius + avoidance.margin +
                         double(avoidance.horizon) * (double(maxSpeeds[i]) + avoidanceMaxSpeed);
    int        encountered = 0;
    const auto nearer      = [&](const Neighbor& a, const Neighbor& b) {
        if (a.distance2 != b.distance2) return a.distance2 < b.distance2;
        if (stableIds[a.slot] != stableIds[b.slot]) return stableIds[a.slot] < stableIds[b.slot];
        return a.slot < b.slot;
    };
    forEachNeighbor(xs[i], ys[i], float(std::min(query, double(std::numeric_limits<float>::max()))), [&](int slot) {
        const size_t j = size_t(slot);
        if (i == j || !canInteract(i, j)) return;
        ++encountered;
        const double dx = double(xs[j]) - xs[i], dy = double(ys[j]) - ys[i];
        Neighbor     neighbor{j, dx * dx + dy * dy};
        const auto   position = std::lower_bound(neighbors.begin(), neighbors.end(), neighbor, nearer);
        if (position != neighbors.end() || neighbors.size() < size_t(avoidance.maxNeighbors)) {
            neighbors.insert(position, neighbor);
            if (neighbors.size() > size_t(avoidance.maxNeighbors)) neighbors.pop_back();
        }
    });
    if (encountered > avoidance.maxNeighbors) ++avoidanceTruncations;
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
    const auto score = [&](float x, float y) {
        double cost =
            std::hypot(x - preferredX, y - preferredY) / speed + 0.15 * std::hypot(x - vxs[i], y - vys[i]) / speed;
        // A small, persistent right-hand preference breaks mirror symmetry.
        if (preferredLength > 1e-5f)
            cost += 0.05 * std::max(0.f, preferredX * y - preferredY * x) / (preferredLength * speed);
        for (const auto& neighbor : neighbors) {
            ++avoidanceChecks;
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
    double     best     = score(vx, vy);
    const auto consider = [&](float x, float y) {
        reachable(x, y);
        const double value = score(x, y);
        if (value < best - 1e-9) {
            best = value;
            vx   = x;
            vy   = y;
        }
    };
    consider(0.f, 0.f);
    consider(vxs[i], vys[i]);
    const float     samplingSpeed = std::max(preferredLength, speed * 0.25f);
    constexpr float angles[]      = {0.f,         -0.2617994f, 0.2617994f,  -0.5235988f, 0.5235988f,
                                     -0.7853982f, 0.7853982f,  -1.0471976f, 1.0471976f,  -1.5707963f,
                                     1.5707963f,  -2.3561945f, 2.3561945f,  3.1415927f};
    for (float fraction : {1.f, 0.5f, 0.25f})
        for (float angle : angles)
            consider(std::cos(baseAngle + angle) * samplingSpeed * fraction,
                     std::sin(baseAngle + angle) * samplingSpeed * fraction);
}

}  // namespace eve::crowd
