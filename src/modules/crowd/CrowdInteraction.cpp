#include <unordered_set>
#include "common/Profile.h"
#include "crowd/CrowdInternal.h"

// CrowdInteraction.cpp
namespace eve::crowd {

InteractionPolicy canonicalInteraction(AgentInteraction policy) {
    return {policy.pushability, policy.holdPosition, policy.layer, policy.mask, policy.layer, policy.mask};
}

bool validInteraction(InteractionPolicy policy) {
    return std::isfinite(policy.pushability) && policy.pushability >= 0.f && policy.pushability <= 1.f &&
           policy.avoidanceLayer >= 0 && policy.avoidanceMask >= 0 && policy.contactLayer >= 0 &&
           policy.contactMask >= 0;
}

Result<void> Crowd::setAgentInteraction(int id, AgentInteraction policy) {
    auto reference = agentRef(id);
    if (!reference) return Result<void>::failure(reference.status());
    return setInteractionPolicy(reference.value(), canonicalInteraction(policy));
}

Result<void> Crowd::setInteractionPolicy(AgentRef reference, InteractionPolicy policy) {
    const int slot = impl_->resolveAgent(reference);
    if (slot < 0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "Crowd agent not found", "reference"));
    if (!validInteraction(policy))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid crowd interaction policy", "policy"));
    const size_t index         = size_t(slot);
    impl_->interactions[index] = policy;
    if (policy.holdPosition) {
        impl_->vxs[index] = impl_->vys[index] = impl_->speeds[index] = 0.f;
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<AgentInteraction> Crowd::getAgentInteraction(int id) const {
    if (!impl_->validId(id))
        return Result<AgentInteraction>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "Crowd agent not found", "id"));
    const auto policy = impl_->interactions[size_t(id)];
    return Result<AgentInteraction>::success(
        {policy.pushability, policy.holdPosition, policy.contactLayer, policy.contactMask});
}

Result<InteractionPolicy> Crowd::readInteractionPolicy(AgentRef reference) const {
    const int slot = impl_->resolveAgent(reference);
    if (slot < 0)
        return Result<InteractionPolicy>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "Crowd agent not found", "reference"));
    return Result<InteractionPolicy>::success(impl_->interactions[std::size_t(slot)]);
}

}  // namespace eve::crowd

// CrowdSimulation.cpp
namespace eve::crowd {
namespace {
enum Action : int32_t { kIdle = 0, kFlow = 1, kSeek = 2, kBoids = 3 };
float wrapPi(float a) {
    const float kTwoPi = 6.28318530717958647692f;
    while (a > 3.14159265358979323846f) a -= kTwoPi;
    while (a < -3.14159265358979323846f) a += kTwoPi;
    return a;
}

}  // namespace

void Crowd::Impl::rebuildGrid() {
    const int n = int(actions.size());
    if (n == 0) {
        gridW = gridH = 0;
        return;
    }
    gridCell   = std::max(std::max(sepRadius, perceiveRadius), 1.f);
    float minX = xs[0], maxX = xs[0], minY = ys[0], maxY = ys[0];
    for (int i = 1; i < n; ++i) {
        minX = std::min(minX, xs[size_t(i)]);
        maxX = std::max(maxX, xs[size_t(i)]);
        minY = std::min(minY, ys[size_t(i)]);
        maxY = std::max(maxY, ys[size_t(i)]);
    }
    minX -= gridCell;
    minY -= gridCell;
    maxX += gridCell;
    maxY += gridCell;
    gridOriginX = minX;
    gridOriginY = minY;
    gridW       = int((maxX - minX) / gridCell) + 1;
    gridH       = int((maxY - minY) / gridCell) + 1;

    const size_t cells = size_t(gridW) * size_t(gridH);
    cellCount.assign(cells, 0);
    for (int i = 0; i < n; ++i) {
        int cx = int((xs[size_t(i)] - minX) / gridCell);
        int cy = int((ys[size_t(i)] - minY) / gridCell);
        cx     = std::clamp(cx, 0, gridW - 1);
        cy     = std::clamp(cy, 0, gridH - 1);
        ++cellCount[size_t(cy * gridW + cx)];
    }
    cellStart.assign(cells, 0);
    int running = 0;
    for (size_t c = 0; c < cells; ++c) {
        cellStart[c] = running;
        running += cellCount[c];
    }
    cursor = cellStart;
    sorted.resize(size_t(n));
    for (int i = 0; i < n; ++i) {
        int cx                              = int((xs[size_t(i)] - minX) / gridCell);
        int cy                              = int((ys[size_t(i)] - minY) / gridCell);
        cx                                  = std::clamp(cx, 0, gridW - 1);
        cy                                  = std::clamp(cy, 0, gridH - 1);
        const int c                         = cy * gridW + cx;
        sorted[size_t(cursor[size_t(c)]++)] = i;
    }
}

void Crowd::Impl::stepAgents(float dt) {
    const int  n        = int(actions.size());
    const bool hasField = field.isBuilt();
    if (avoidance.enabled) {
        avoidanceMaxSpeed  = n == 0 ? 0.f : *std::max_element(maxSpeeds.begin(), maxSpeeds.end());
        avoidanceMaxRadius = n == 0 ? 0.f : *std::max_element(radii.begin(), radii.end());
    }
    nextVxs.resize(size_t(n));
    nextVys.resize(size_t(n));
    preferredVxs.resize(size_t(n));
    preferredVys.resize(size_t(n));
    arriveFactors.resize(size_t(n), 1.f);
    for (int i = 0; i < n; ++i) {
        if (interactions[size_t(i)].holdPosition) {
            preferredVxs[size_t(i)] = preferredVys[size_t(i)] = 0.f;
            arriveFactors[size_t(i)]                          = 1.f;
            continue;
        }
        const float x        = xs[size_t(i)];
        const float y        = ys[size_t(i)];
        const float maxSpeed = maxSpeeds[size_t(i)];
        const int   action   = actions[size_t(i)];
        float       arriveK  = 1.f;  // flow 模式到达减速系数（1=全速，0=停）

        // 基础期望方向。
        float desX = 0.f, desY = 0.f;
        if (action == kFlow) {
            float fx = 0.f, fy = 0.f;
            if (hasField) field.flowAtWorld(x, y, fx, fy);
            // 接近目标时按积分代价线性减速（arrive），避免在目标格附近振荡。
            float k = 1.f;
            if (hasField) {
                const float c           = field.costAtWorld(x, y);
                const float cell        = std::max(field.getCellSize(), 1e-6f);
                const float arriveCells = arriveRadius / cell;
                if (c < CrowdField::kUnreachable && arriveCells > 0.f && c < arriveCells) {
                    k       = c / arriveCells;
                    arriveK = k;
                }
            }
            desX = fx * k;
            desY = fy * k;
        } else if (action == kSeek && hasTargets[size_t(i)]) {
            const float tx = targetXs[size_t(i)] - x;
            const float ty = targetYs[size_t(i)] - y;
            const float d  = std::sqrt(tx * tx + ty * ty);
            if (d > 1e-3f) {
                desX = tx / d;
                desY = ty / d;
                if (d < arriveRadius) {
                    const float k = d / arriveRadius;
                    desX *= k;
                    desY *= k;
                }
            }
        } else if (action == kBoids && hasTargets[size_t(i)] && goalWeight > 0.f) {
            const float tx = targetXs[size_t(i)] - x;
            const float ty = targetYs[size_t(i)] - y;
            const float d  = std::sqrt(tx * tx + ty * ty);
            if (d > 1e-3f) {
                desX = tx / d * goalWeight;
                desY = ty / d * goalWeight;
            }
        }
        desX *= maxSpeed;
        desY *= maxSpeed;

        // Boids 三力。
        if (sepWeight > 0.f || alignWeight > 0.f || cohesionWeight > 0.f) {
            const float queryR = std::max(sepRadius, perceiveRadius);
            float       sx = 0.f, sy = 0.f;
            float       axv = 0.f, ayv = 0.f;
            float       cxv = 0.f, cyv = 0.f;
            int         alignN = 0, cohN = 0;
            forEachNeighbor(x, y, queryR, [&](int j) {
                if (!canInteract(size_t(i), size_t(j))) return;
                const float dx = xs[size_t(j)] - x;
                const float dy = ys[size_t(j)] - y;
                const float d2 = dx * dx + dy * dy;
                if (d2 <= 0.f) return;
                if (sepWeight > 0.f && d2 <= sepRadius * sepRadius) {
                    const float d    = std::sqrt(d2);
                    const float fall = 1.f - d / sepRadius;
                    sx += (x - xs[size_t(j)]) / d * fall;
                    sy += (y - ys[size_t(j)]) / d * fall;
                }
                if (alignWeight > 0.f && d2 <= perceiveRadius * perceiveRadius) {
                    axv += vxs[size_t(j)];
                    ayv += vys[size_t(j)];
                    ++alignN;
                }
                if (cohesionWeight > 0.f && d2 <= perceiveRadius * perceiveRadius) {
                    cxv += xs[size_t(j)];
                    cyv += ys[size_t(j)];
                    ++cohN;
                }
            });
            desX += sx * sepWeight * maxSpeed;
            desY += sy * sepWeight * maxSpeed;
            if (alignN > 0) {
                const float al = std::sqrt(axv * axv + ayv * ayv);
                if (al > 1e-4f) {
                    desX += axv / al * alignWeight * maxSpeed;
                    desY += ayv / al * alignWeight * maxSpeed;
                }
            }
            if (cohN > 0) {
                const float cmx = cxv / float(cohN) - x;
                const float cmy = cyv / float(cohN) - y;
                const float cl  = std::sqrt(cmx * cmx + cmy * cmy);
                if (cl > 1e-4f) {
                    desX += cmx / cl * cohesionWeight * maxSpeed;
                    desY += cmy / cl * cohesionWeight * maxSpeed;
                }
            }
        }

        // Boids wander。
        if (action == kBoids && wanderWeight > 0.f) {
            wanderPhases[size_t(i)] += dt * 2.5f;
            desX += std::cos(wanderPhases[size_t(i)]) * wanderWeight * maxSpeed;
            desY += std::sin(wanderPhases[size_t(i)]) * wanderWeight * maxSpeed;
        }

        // 期望速度限幅。
        float dl = std::sqrt(desX * desX + desY * desY);
        if (dl > maxSpeed && dl > 1e-6f) {
            desX *= maxSpeed / dl;
            desY *= maxSpeed / dl;
        }

        preferredVxs[size_t(i)]  = desX;
        preferredVys[size_t(i)]  = desY;
        arriveFactors[size_t(i)] = arriveK;
    }
    for (int i = 0; i < n; ++i) {
        if (interactions[size_t(i)].holdPosition) {
            nextVxs[size_t(i)] = nextVys[size_t(i)] = speeds[size_t(i)] = 0.f;
            continue;
        }
        const float desX = preferredVxs[size_t(i)], desY = preferredVys[size_t(i)];
        const float maxSpeed = maxSpeeds[size_t(i)];
        const float arriveK  = arriveFactors[size_t(i)];
        // 加速度限幅。
        float       avx      = vxs[size_t(i)];
        float       avy      = vys[size_t(i)];
        float       ddx      = desX - avx;
        float       ddy      = desY - avy;
        const float ddl      = std::sqrt(ddx * ddx + ddy * ddy);
        const float maxDelta = std::max(maxAccels[size_t(i)] * dt, 0.f);
        if (ddl > maxDelta && ddl > 1e-6f) {
            ddx *= maxDelta / ddl;
            ddy *= maxDelta / ddl;
        }
        avx += ddx;
        avy += ddy;

        if (avoidance.enabled) selectAvoidanceVelocity(size_t(i), dt, avx, avy);

        // 速度限幅。
        float sp = std::sqrt(avx * avx + avy * avy);
        if (sp > maxSpeed && sp > 1e-6f) {
            avx *= maxSpeed / sp;
            avy *= maxSpeed / sp;
            sp = maxSpeed;
        }
        nextVxs[size_t(i)] = avx;
        nextVys[size_t(i)] = avy;
        speeds[size_t(i)]  = sp;

        // 接近目标时对速度做阻尼，快速消除目标点附近的来回振荡。
        if (arriveK < 1.f) {
            const float damp = 1.f - (1.f - arriveK) * std::min(1.f, dt * 4.f);
            nextVxs[size_t(i)] *= damp;
            nextVys[size_t(i)] *= damp;
            speeds[size_t(i)] *= damp;
        }

        // 转向：heading 以 turnRate·dt 为上限向速度方向收敛。
        const float spAfter = speeds[size_t(i)];
        if (spAfter > 1e-4f) {
            const float targetAng = std::atan2(nextVys[size_t(i)], nextVxs[size_t(i)]);
            const float delta     = wrapPi(targetAng - headings[size_t(i)]);
            const float maxTurn   = std::max(turnRates[size_t(i)] * dt, 0.f);
            headings[size_t(i)] += std::clamp(delta, -maxTurn, maxTurn);
        }
    }
    vxs.swap(nextVxs);
    vys.swap(nextVys);
    for (int i = 0; i < n; ++i) {
        xs[size_t(i)] += vxs[size_t(i)] * dt;
        ys[size_t(i)] += vys[size_t(i)] * dt;
    }
}

float Crowd::Impl::resolveOverlapsPass() {
    const int n = int(actions.size());
    correctionXs.assign(size_t(n), 0.f);
    correctionYs.assign(size_t(n), 0.f);
    const float maxRadius      = radii.empty() ? 0.f : *std::max_element(radii.begin(), radii.end());
    float       maxPenetration = 0.f;
    for (int i = 0; i < n; ++i) {
        const float xi = xs[size_t(i)];
        const float yi = ys[size_t(i)];
        forEachNeighbor(xi, yi, radii[size_t(i)] + maxRadius, [&](int j) {
            if (j <= i || !canInteract(size_t(i), size_t(j))) return;
            float       dx   = xs[size_t(j)] - xi;
            float       dy   = ys[size_t(j)] - yi;
            float       d2   = dx * dx + dy * dy;
            const float minD = radii[size_t(i)] + radii[size_t(j)];
            if (d2 >= minD * minD) return;
            const bool exactOverlap = d2 <= 1e-9f;
            if (exactOverlap) {
                const std::string  left   = stableIds[size_t(i)].empty() ? std::to_string(i) : stableIds[size_t(i)];
                const std::string  right  = stableIds[size_t(j)].empty() ? std::to_string(j) : stableIds[size_t(j)];
                const std::string& first  = left < right ? left : right;
                const std::string& second = left < right ? right : left;
                std::uint64_t      hash   = 1469598103934665603ULL;
                for (const unsigned char value : first + "\n" + second) {
                    hash ^= value;
                    hash *= 1099511628211ULL;
                }
                const float angle = static_cast<float>(hash % 104729ULL) / 104729.0f * 6.28318530717958647692f;
                dx                = std::cos(angle);
                dy                = std::sin(angle);
                if (left > right) {
                    dx = -dx;
                    dy = -dy;
                }
                d2 = 1.0f;
            }
            const float d             = std::sqrt(d2);
            const float push          = minD - (exactOverlap ? 0.0f : d);
            maxPenetration            = std::max(maxPenetration, push);
            const float nx            = dx / d;
            const float ny            = dy / d;
            float       leftShare     = 0.5f;
            float       rightShare    = 0.5f;
            const auto& leftPolicy    = interactions[size_t(i)];
            const auto& rightPolicy   = interactions[size_t(j)];
            const float leftMobility  = leftPolicy.holdPosition ? 0.f : leftPolicy.pushability;
            const float rightMobility = rightPolicy.holdPosition ? 0.f : rightPolicy.pushability;
            if (leftMobility + rightMobility == 0.f) return;
            leftShare  = leftMobility / (leftMobility + rightMobility);
            rightShare = 1.f - leftShare;
            // Preserve the existing priority preference only when both can yield.
            // Priority never overrides an explicit immovable/hold policy.
            if (leftMobility > 0.f && rightMobility > 0.f &&
                avoidancePriorities[size_t(i)] > avoidancePriorities[size_t(j)]) {
                leftShare  = 0.0f;
                rightShare = 1.0f;
            } else if (leftMobility > 0.f && rightMobility > 0.f &&
                       avoidancePriorities[size_t(j)] > avoidancePriorities[size_t(i)]) {
                leftShare  = 1.0f;
                rightShare = 0.0f;
            }
            correctionXs[size_t(i)] -= nx * push * leftShare;
            correctionYs[size_t(i)] -= ny * push * leftShare;
            correctionXs[size_t(j)] += nx * push * rightShare;
            correctionYs[size_t(j)] += ny * push * rightShare;
        });
    }
    for (int i = 0; i < n; ++i) {
        // Bound dense-cluster corrections so a single pass cannot eject an agent.
        const float length = std::hypot(correctionXs[size_t(i)], correctionYs[size_t(i)]);
        const float limit  = std::max(radii[size_t(i)], 1e-4f);
        const float scale  = length > limit ? limit / length : 1.f;
        xs[size_t(i)] += correctionXs[size_t(i)] * scale;
        ys[size_t(i)] += correctionYs[size_t(i)] * scale;
    }
    return maxPenetration;
}

void Crowd::Impl::resolveWalls() {
    if (!field.valid()) return;
    const float width  = float(field.getWidth()) * field.getCellSize();
    const float height = float(field.getHeight()) * field.getCellSize();
    for (size_t i = 0; i < xs.size(); ++i) {
        for (int pass = 0; pass < 4; ++pass) {
            const float oldX = xs[i], oldY = ys[i];
            field.resolvePenetration(xs[i], ys[i], radii[i]);
            if (clampToField) {
                // Field origin is the lower cell corner, consistently with sampling.
                const float rx = std::min(radii[i], width * 0.5f);
                const float ry = std::min(radii[i], height * 0.5f);
                xs[i]          = std::clamp(xs[i], field.getOriginX() + rx, field.getOriginX() + width - rx);
                ys[i]          = std::clamp(ys[i], field.getOriginY() + ry, field.getOriginY() + height - ry);
            }
            const float dx = xs[i] - oldX, dy = ys[i] - oldY;
            const float length2 = dx * dx + dy * dy;
            if (length2 == 0.f) break;
            // Remove velocity into the constraint; retaining it causes permanent
            // walking-in-place when a requested destination lies outside the field.
            const float inward = vxs[i] * dx + vys[i] * dy;
            if (inward < 0.f) {
                vxs[i] -= dx * inward / length2;
                vys[i] -= dy * inward / length2;
                speeds[i] = std::hypot(vxs[i], vys[i]);
            }
        }
    }
}

}  // namespace eve::crowd

// CrowdSpawn.cpp
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

// CrowdStep.cpp
namespace eve::crowd {

Result<StepReport> Crowd::advance(float dt) {
    EV_PROFILE_MODULE("crowd", "Crowd::advance");
    if (!std::isfinite(dt) || dt < 0.f)
        return Result<StepReport>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Crowd dt must be finite and non-negative", "dt"));
    StepReport report;
    if (dt == 0.f || impl_->xs.empty()) return Result<StepReport>::success(report, Status::success(StatusCode::NoOp));

    auto& d                = *impl_;
    d.avoidanceChecks      = 0;
    d.avoidanceTruncations = 0;
    double maxStep         = 1.0 / 60.0;
    float  travelScale     = d.field.valid() ? d.field.getCellSize() : 0.f;
    float  maxSpeed        = 0.f;
    for (size_t i = 0; i < d.xs.size(); ++i) {
        if (d.radii[i] > 0.f) travelScale = travelScale > 0.f ? std::min(travelScale, d.radii[i]) : d.radii[i];
        maxSpeed = std::max(maxSpeed, d.maxSpeeds[i]);
    }
    if (travelScale > 0.f && maxSpeed > 0.f) maxStep = std::min(maxStep, 0.5 * double(travelScale) / double(maxSpeed));
    // Float dt=1/60 rounds slightly upward. Do not create an extra substep for
    // representation error at an otherwise exact boundary.
    const double count = std::ceil(double(dt) / maxStep - 1e-6);
    if (!std::isfinite(count) || count > 1024.0)
        return Result<StepReport>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "Crowd step exceeds 1024 substeps; reduce dt or speed", "dt"));
    report.substeps     = std::max(1, static_cast<int>(count));
    const float substep = dt / float(report.substeps);
    for (int sub = 0; sub < report.substeps; ++sub) {
        d.simTime += substep;
        d.rebuildGrid();
        d.stepAgents(substep);
        d.resolveWalls();
        if (d.resolveOverlaps) {
            for (int pass = 0; pass < 12; ++pass) {
                d.rebuildGrid();
                const float penetration = d.resolveOverlapsPass();
                d.resolveWalls();
                if (penetration < 1e-4f) break;
            }
        }
    }

    d.rebuildGrid();
    const float maxRadius = *std::max_element(d.radii.begin(), d.radii.end());
    for (size_t i = 0; i < d.xs.size(); ++i) {
        d.forEachNeighbor(d.xs[i], d.ys[i], d.radii[i] + maxRadius, [&](int neighbor) {
            const size_t j = size_t(neighbor);
            if (j <= i || !d.canInteract(i, j)) return;
            const float depth     = d.radii[i] + d.radii[j] - std::hypot(d.xs[j] - d.xs[i], d.ys[j] - d.ys[i]);
            report.maxPenetration = std::max(report.maxPenetration, depth);
            if (depth > 0.001f) ++report.unresolvedContacts;
        });
        if (d.field.valid()) {
            float x = d.xs[i], y = d.ys[i];
            d.field.resolvePenetration(x, y, d.radii[i]);
            bool outside = false;
            if (d.clampToField) {
                outside = d.xs[i] - d.radii[i] < d.field.getOriginX() - 0.001f ||
                          d.ys[i] - d.radii[i] < d.field.getOriginY() - 0.001f ||
                          d.xs[i] + d.radii[i] >
                              d.field.getOriginX() + float(d.field.getWidth()) * d.field.getCellSize() + 0.001f ||
                          d.ys[i] + d.radii[i] >
                              d.field.getOriginY() + float(d.field.getHeight()) * d.field.getCellSize() + 0.001f;
            }
            if (outside || std::hypot(x - d.xs[i], y - d.ys[i]) > 0.001f) ++report.unresolvedWalls;
        }
    }
    report.avoidanceChecks      = d.avoidanceChecks;
    report.avoidanceTruncations = d.avoidanceTruncations;
    return Result<StepReport>::success(report, Status::success(StatusCode::Applied));
}

void Crowd::step(float dt) { advance(dt).expect("Crowd step rejected"); }

}  // namespace eve::crowd
