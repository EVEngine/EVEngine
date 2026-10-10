#include "crowd/CrowdInternal.h"

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
