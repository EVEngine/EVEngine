#include "common/Assert.h"
#include "crowd/CrowdInternal.h"
namespace eve::crowd {
namespace {
enum Action : int32_t { kIdle = 0, kFlow = 1, kSeek = 2, kBoids = 3 };
}
void Crowd::Impl::spendPreferredWork(std::int64_t amount) {
    const auto limit = budgetActive ? activeBudget.maxPreferredVisits : std::numeric_limits<std::int64_t>::max();
    if (amount > limit - workReport.preferredVisits) throw WorkLimit{"Crowd preferred work budget exhausted"};
    workReport.preferredVisits += amount;
}
void Crowd::Impl::preparePreferredNeighbors() {
    const size_t n = actions.size();
    spendPreferredWork(static_cast<std::int64_t>(n) * 2 + 1);
    preferredCounts.assign(n, 0);
    preferredStarts.resize(n + 1);
    const bool  forces     = sepWeight > 0.f || alignWeight > 0.f || cohesionWeight > 0.f;
    const float queryR     = std::max(sepRadius, perceiveRadius);
    const auto  maxEntries = budgetActive ? activeBudget.maxPreferredNeighbors : std::int64_t(1000000);
    const auto  visit      = [&](size_t i, const auto& accept) {
        if (!forces || interactions[i].holdPosition) return;
        forEachNeighbor(xs[i], ys[i], queryR, [&](int j) {
            if (!canAvoid(i, size_t(j))) return;
            const float dx = xs[size_t(j)] - xs[i], dy = ys[size_t(j)] - ys[i];
            const float d2 = dx * dx + dy * dy;
            if (d2 <= 0.f) return;
            if (!(sepWeight > 0.f && d2 <= sepRadius * sepRadius) &&
                !((alignWeight > 0.f || cohesionWeight > 0.f) && d2 <= perceiveRadius * perceiveRadius))
                return;
            accept(j);
        });
    };
    size_t total = 0;
    for (size_t i = 0; i < n; ++i) {
        spendPreferredWork(1);
        visit(i, [&](int) {
            spendPreferredWork(1);
            if (total >= static_cast<std::uint64_t>(maxEntries))
                throw WorkLimit{"Crowd preferred neighbor capacity exhausted"};
            ++preferredCounts[i];
            ++total;
        });
    }
    size_t prefix = 0;
    for (size_t i = 0; i < n; ++i) {
        spendPreferredWork(1);
        preferredStarts[i] = prefix;
        prefix += preferredCounts[i];
    }
    preferredStarts[n] = prefix;
    spendPreferredWork(static_cast<std::int64_t>(total));
    preferredNeighbors.resize(total);
    workReport.preferredNeighborsPeak = std::max(workReport.preferredNeighborsPeak, static_cast<std::int64_t>(total));
    for (size_t i = 0; i < n; ++i) {
        spendPreferredWork(1);
        size_t cursor = preferredStarts[i];
        visit(i, [&](int j) {
            spendPreferredWork(1);
            EV_ASSERT(cursor < preferredStarts[i + 1], "Preferred snapshot changed during fill");
            preferredNeighbors[cursor++] = j;
        });
        EV_ASSERT(cursor == preferredStarts[i + 1], "Preferred count/fill mismatch");
    }
}
void Crowd::Impl::preparePreferredVelocities(float dt) {
    const int n = int(actions.size());
    preparePreferredNeighbors();
    spendPreferredWork(n + static_cast<std::int64_t>(preferredNeighbors.size()));
    spendPreferredWork(std::int64_t(n) * 2);
    preferredFlows.assign(size_t(n), PreferredFlow{});
    for (size_t i = 0; i < actions.size(); ++i) {
        if (actions[i] != kFlow || interactions[i].holdPosition) continue;
        auto&       flow = preferredFlows[i];
        float       fx = 0.f, fy = 0.f;
        const auto* navigation = navigationField(i);
        const float cost       = navigation ? navigation->costAtWorld(xs[i], ys[i]) : CrowdField::kUnreachable;
        const bool  reachable  = navigation && isFlowPointReachable(*navigation, xs[i], ys[i]);
        if (reachable) navigation->flowAtWorld(xs[i], ys[i], fx, fy);
        if (reachable && navigation->isBuilt()) {
            const float          cell  = navigation->getCellSize();
            const int            cx    = int(std::floor((double(xs[i]) - navigation->getOriginX()) / cell));
            const int            cy    = int(std::floor((double(ys[i]) - navigation->getOriginY()) / cell));
            static constexpr int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
            spendPreferredWork(8);
            bool  edge = false;
            float best = navigation->costAtCell(cx, cy);
            int   next = -1;
            for (int side = 0; side < 4; ++side) {
                const int nx = cx + dx[side], ny = cy + dy[side];
                if (nx >= 0 && ny >= 0 && nx < navigation->getWidth() && ny < navigation->getHeight() &&
                    !navigation->isReachable(nx, ny))
                    edge = true;
                const float value = navigation->costAtCell(nx, ny);
                if (value < best) {
                    best = value;
                    next = side;
                }
            }
            if (edge && next >= 0 && radii[i] < cell * 0.5f) {
                const float centerX = navigation->getOriginX() + (float(cx) + 0.5f) * cell;
                const float centerY = navigation->getOriginY() + (float(cy) + 0.5f) * cell;
                const float inset   = std::min(radii[i] + (avoidance.enabled ? avoidance.margin : 0.f), cell * 0.49f);
                const bool  aligned = dx[next] != 0 ? std::fabs(ys[i] - centerY) <= cell * 0.5f - inset
                                                    : std::fabs(xs[i] - centerX) <= cell * 0.5f - inset;
                // Align the perpendicular axis before crossing a portal; do not recenter along travel.
                fx                 = centerX + (aligned ? float(dx[next]) * cell : 0.f) - xs[i];
                fy                 = centerY + (aligned ? float(dy[next]) * cell : 0.f) - ys[i];
                const float length = std::hypot(fx, fy);
                if (length > 1e-6f) {
                    fx /= length;
                    fy /= length;
                }
            }
        }
        // 接近目标时按积分代价线性减速（arrive），避免在目标格附近振荡。
        float k = 1.f;
        if (navigation) {
            const float c           = cost;
            const float cell        = std::max(navigation->getCellSize(), 1e-6f);
            const float arriveCells = arriveRadius / cell;
            if (c < CrowdField::kUnreachable && arriveCells > 0.f && c < arriveCells) {
                k           = c / arriveCells;
                flow.arrive = k;
            }
        }
        flow.x = fx * k;
        flow.y = fy * k;
    }
    dispatchWork(size_t(n), [&](std::size_t first, std::size_t last) {
        for (int i = int(first); i < int(last); ++i) {
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
                const auto flow = preferredFlows[size_t(i)];
                desX            = flow.x;
                desY            = flow.y;
                arriveK         = flow.arrive;
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
                float sx = 0.f, sy = 0.f;
                float axv = 0.f, ayv = 0.f;
                float cxv = 0.f, cyv = 0.f;
                int   alignN = 0, cohN = 0;
                for (size_t cursor = preferredStarts[size_t(i)]; cursor < preferredStarts[size_t(i) + 1]; ++cursor) {
                    const int   j  = preferredNeighbors[cursor];
                    const float dx = xs[size_t(j)] - x;
                    const float dy = ys[size_t(j)] - y;
                    const float d2 = dx * dx + dy * dy;
                    if (d2 <= 0.f) continue;
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
                }
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
    });
}
}  // namespace eve::crowd
