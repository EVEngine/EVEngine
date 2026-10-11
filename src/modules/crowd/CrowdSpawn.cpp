#include "crowd/CrowdInternal.h"

#include <unordered_set>

namespace eve::crowd {
namespace {
constexpr float tolerance = 0.001f;

bool isTerrainClear(const CrowdField& field, bool clamp, float x, float y, float radius) {
    if (!field.valid()) return true;
    if (clamp && (x - radius < field.getOriginX() || y - radius < field.getOriginY() ||
                  x + radius > field.getOriginX() + float(field.getWidth()) * field.getCellSize() ||
                  y + radius > field.getOriginY() + float(field.getHeight()) * field.getCellSize()))
        return false;
    float projectedX = x, projectedY = y;
    field.resolvePenetration(projectedX, projectedY, radius);
    return std::hypot(projectedX - x, projectedY - y) <= tolerance;
}

void projectTerrain(const CrowdField& field, bool clamp, float& x, float& y, float radius) {
    if (!field.valid()) return;
    for (int pass = 0; pass < 4; ++pass) {
        field.resolvePenetration(x, y, radius);
        if (clamp) {
            const float width  = float(field.getWidth()) * field.getCellSize();
            const float height = float(field.getHeight()) * field.getCellSize();
            const float rx = std::min(radius, width * 0.5f), ry = std::min(radius, height * 0.5f);
            x = std::clamp(x, field.getOriginX() + rx, field.getOriginX() + width - rx);
            y = std::clamp(y, field.getOriginY() + ry, field.getOriginY() + height - ry);
        }
    }
}
}  // namespace

Result<SpawnReceipt> Crowd::applySpawnBatch(const SpawnBatch& batch) {
    const auto failure = [](DiagnosticCode code, const char* message) {
        return Result<SpawnReceipt>::failure(Diagnostic::error(code, message, "spawnBatch"));
    };
    if (!std::isfinite(batch.maxDistance) || batch.maxDistance < 0.f || !std::isfinite(batch.searchSpacing) ||
        batch.searchSpacing <= 0.f || batch.maxPasses < 1 || batch.maxPasses > 256 || batch.maxChecks < 1 ||
        batch.maxChecks > 10000000 || batch.agents.size() > 1024 ||
        (batch.policy != SpawnPolicy::RejectOverlap && batch.policy != SpawnPolicy::NearestFree &&
         batch.policy != SpawnPolicy::PushNeighbors))
        return failure(DiagnosticCode::InvalidArgument, "Invalid spawn policy or work budget");
    SpawnReceipt receipt;
    if (batch.agents.empty())
        return Result<SpawnReceipt>::success(std::move(receipt), Status::success(StatusCode::NoOp));
    if (impl_->xs.size() + batch.agents.size() > size_t(std::max(impl_->maxAgents, 0)))
        return failure(DiagnosticCode::PreconditionViolation, "Crowd capacity cannot fit the batch");
    std::unordered_set<std::string> names;
    for (size_t i = 0; i < impl_->xs.size(); ++i)
        if (!std::isfinite(impl_->xs[i]) || !std::isfinite(impl_->ys[i]) || !std::isfinite(impl_->radii[i]) ||
            impl_->radii[i] < 0.f)
            return failure(DiagnosticCode::PreconditionViolation, "Existing crowd geometry is invalid");
    for (const auto& request : batch.agents) {
        const auto& policy = request.interaction;
        if (request.stableId.empty() || !std::isfinite(request.x) || !std::isfinite(request.y) ||
            !std::isfinite(request.heading) || !std::isfinite(request.radius) || request.radius <= 0.f ||
            !std::isfinite(policy.pushability) || policy.pushability < 0.f || policy.pushability > 1.f ||
            policy.layer < 0 || policy.mask < 0)
            return failure(DiagnosticCode::InvalidArgument, "Invalid spawn agent");
        if (impl_->namedAgents.contains(request.stableId) || !names.insert(request.stableId).second)
            return failure(DiagnosticCode::Conflict, "Spawn stable identifiers must be unique");
    }

    // No callbacks or external mutations occur between the snapshot and swap.
    Crowd staged;
    staged.impl_               = std::make_unique<Impl>(*impl_);
    auto&        d             = *staged.impl_;
    const size_t originalCount = d.xs.size();
    bool         exhausted     = false;
    const auto   consume       = [&]() {
        if (receipt.checks >= batch.maxChecks) {
            exhausted = true;
            return false;
        }
        ++receipt.checks;
        return true;
    };
    const auto isClear = [&](const SpawnRequest& request, float x, float y, bool checkAgents) {
        if (!consume() || !std::isfinite(x) || !std::isfinite(y) ||
            !isTerrainClear(d.field, d.clampToField, x, y, request.radius))
            return false;
        if (!checkAgents) return true;
        for (size_t i = 0; i < d.xs.size(); ++i) {
            if (!consume()) return false;
            if ((request.interaction.layer & d.interactions[i].mask) == 0 ||
                (d.interactions[i].layer & request.interaction.mask) == 0)
                continue;
            if (std::hypot(double(x) - d.xs[i], double(y) - d.ys[i]) < double(request.radius) + d.radii[i] - tolerance)
                return false;
        }
        return true;
    };
    for (const auto& request : batch.agents) {
        float x = request.x, y = request.y;
        bool  clear = isClear(request, x, y, batch.policy != SpawnPolicy::PushNeighbors);
        if (!clear && batch.policy == SpawnPolicy::NearestFree) {
            // Double loop counters avoid integer overflow for tiny spacing/huge radii.
            const double rings = std::ceil(double(batch.maxDistance) / double(batch.searchSpacing));
            for (double ring = 1; ring <= rings && !clear && !exhausted; ++ring) {
                const double radius  = std::min(ring * double(batch.searchSpacing), double(batch.maxDistance));
                const double samples = std::max(8.0, std::ceil(6.283185307179586 * radius / batch.searchSpacing));
                for (double sample = 0; sample < samples && !exhausted; ++sample) {
                    const double angle = sample / samples * 6.283185307179586;
                    x                  = request.x + float(std::cos(angle) * radius);
                    y                  = request.y + float(std::sin(angle) * radius);
                    if (isClear(request, x, y, true)) {
                        clear = true;
                        break;
                    }
                }
            }
        }
        if (!clear)
            return failure(
                DiagnosticCode::PreconditionViolation,
                exhausted ? "Spawn work budget exhausted" : "No valid spawn placement within search distance");
        const int slot = staged.addNamedAgent(request.stableId, x, y, request.heading, request.radius);
        if (slot < 0) return failure(DiagnosticCode::InvariantViolation, "Validated spawn allocation rejected");
        auto policy = staged.setAgentInteraction(slot, request.interaction);
        if (!policy) return Result<SpawnReceipt>::failure(policy.status());
        receipt.created.push_back({request.stableId, x, y});
    }

    if (batch.policy == SpawnPolicy::PushNeighbors) {
        const size_t         count = d.xs.size();
        std::vector<uint8_t> active(count, 0);
        for (size_t i = originalCount; i < count; ++i) active[i] = 1;
        std::vector<double> correctionsX(count), correctionsY(count);
        bool                solved = false;
        for (int pass = 0; pass <= batch.maxPasses; ++pass) {
            std::fill(correctionsX.begin(), correctionsX.end(), 0.f);
            std::fill(correctionsY.begin(), correctionsY.end(), 0.f);
            bool overlap = false;
            for (size_t i = 0; i < count; ++i) {
                if (!active[i]) continue;
                for (size_t j = 0; j < count; ++j) {
                    if (j == i || (j < i && active[j])) continue;
                    if (!consume())
                        return failure(DiagnosticCode::PreconditionViolation, "Spawn work budget exhausted");
                    if (!d.canInteract(i, j)) continue;
                    double       dx = double(d.xs[j]) - d.xs[i], dy = double(d.ys[j]) - d.ys[i];
                    double       distance = std::hypot(dx, dy);
                    const double depth    = double(d.radii[i]) + d.radii[j] - distance;
                    if (depth <= tolerance) continue;
                    overlap             = true;
                    active[j]           = 1;
                    const auto mobility = [&](size_t slot) {
                        return slot >= originalCount || d.interactions[slot].holdPosition
                                   ? 0.f
                                   : d.interactions[slot].pushability;
                    };
                    const float left = mobility(i), right = mobility(j), total = left + right;
                    if (total == 0.f)
                        return failure(DiagnosticCode::Conflict, "Spawn contact is blocked by fixed agents");
                    if (distance < 1e-6f) {
                        // Slot order only selects the tie direction. The operation is repeatable
                        // for identical world and request ordering; no random stream is consumed.
                        dx       = i < j ? 1.f : -1.f;
                        dy       = 0.f;
                        distance = 1.f;
                    }
                    correctionsX[i] -= dx / distance * depth * left / total;
                    correctionsY[i] -= dy / distance * depth * left / total;
                    correctionsX[j] += dx / distance * depth * right / total;
                    correctionsY[j] += dy / distance * depth * right / total;
                }
            }
            if (!overlap) {
                solved = true;
                break;
            }
            if (pass == batch.maxPasses) break;
            for (size_t i = 0; i < originalCount; ++i) {
                if (!active[i] || d.interactions[i].holdPosition || d.interactions[i].pushability == 0.f) continue;
                const double length = std::hypot(correctionsX[i], correctionsY[i]);
                const float  limit  = d.field.valid() ? std::min(d.radii[i], d.field.getCellSize() * 0.5f) : d.radii[i];
                const double scale  = length > limit && length > 0.f ? limit / length : 1.0;
                d.xs[i] += float(correctionsX[i] * scale);
                d.ys[i] += float(correctionsY[i] * scale);
                if (!std::isfinite(d.xs[i]) || !std::isfinite(d.ys[i]))
                    return failure(DiagnosticCode::PreconditionViolation, "Spawn displacement exceeds numeric range");
                projectTerrain(d.field, d.clampToField, d.xs[i], d.ys[i], d.radii[i]);
                if (!isTerrainClear(d.field, d.clampToField, d.xs[i], d.ys[i], d.radii[i]) ||
                    std::hypot(d.xs[i] - impl_->xs[i], d.ys[i] - impl_->ys[i]) > batch.maxDistance + tolerance)
                    return failure(DiagnosticCode::PreconditionViolation,
                                   "Spawn displacement violates terrain or distance budget");
            }
        }
        if (!solved)
            return failure(DiagnosticCode::PreconditionViolation, "Spawn relaxation did not converge within budget");
        for (size_t i = 0; i < originalCount; ++i)
            if (d.xs[i] != impl_->xs[i] || d.ys[i] != impl_->ys[i]) ++receipt.displacedAgents;
    }
    // Cached broadphase refers to pre-transaction positions; rebuild on the next advance.
    d.gridW = d.gridH = 0;
    impl_.swap(staged.impl_);
    return Result<SpawnReceipt>::success(std::move(receipt), Status::success(StatusCode::Applied));
}

}  // namespace eve::crowd
