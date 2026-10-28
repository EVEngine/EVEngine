// eve-hot-path
#include "crowd/CrowdInternal.h"

namespace eve::crowd {
namespace {
float wrapPi(float a) {
    const float kTwoPi = 6.28318530717958647692f;
    return std::remainder(a, kTwoPi);
}

}  // namespace

void Crowd::Impl::rebuildGrid() {
    ++workReport.gridBuilds;
    const double maxRadius = radii.empty() ? 0.0 : double(*std::max_element(radii.begin(), radii.end()));
    spatialIndex.rebuild(xs, ys, tieKeys, std::max(2.0 * maxRadius, 1.0),
                         budgetActive ? activeBudget.maxGridCells : std::numeric_limits<std::int64_t>::max(),
                         workReport.gridSortComparisons);
    workReport.gridCellsPeak = std::max(workReport.gridCellsPeak, static_cast<std::int64_t>(spatialIndex.cellCount()));
    workReport.gridEntriesPeak = std::max(workReport.gridEntriesPeak, static_cast<std::int64_t>(actions.size()));
}

void Crowd::Impl::stepAgents(float dt) {
    const int n = int(actions.size());
    if (avoidance.enabled) {
        avoidanceMaxSpeed  = n == 0 ? 0.f : *std::max_element(maxSpeeds.begin(), maxSpeeds.end());
        avoidanceMaxRadius = n == 0 ? 0.f : *std::max_element(radii.begin(), radii.end());
    }
    spendPreferredWork(std::int64_t(n) * 5);
    nextVxs.resize(size_t(n));
    nextVys.resize(size_t(n));
    preferredVxs.resize(size_t(n));
    preferredVys.resize(size_t(n));
    arriveFactors.resize(size_t(n), 1.f);
    preparePreferredVelocities(dt);
    if (avoidance.enabled) prepareAvoidanceNeighbors();
    dispatchWork(size_t(n), [&](std::size_t first, std::size_t last) {
        for (int i = int(first); i < int(last); ++i) {
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
    });
    vxs.swap(nextVxs);
    vys.swap(nextVys);
    for (int i = 0; i < n; ++i) {
        xs[size_t(i)] += vxs[size_t(i)] * dt;
        ys[size_t(i)] += vys[size_t(i)] * dt;
    }
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
