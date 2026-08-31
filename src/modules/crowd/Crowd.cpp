#include "crowd/Crowd.h"

#include "common/Exception.h"
#include "common/Profile.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace eve::crowd {
namespace {

enum Action : int32_t { kIdle = 0, kFlow = 1, kSeek = 2, kBoids = 3 };

int actionFromName(const std::string &name) {
    if (name == "idle") return kIdle;
    if (name == "flow") return kFlow;
    if (name == "seek") return kSeek;
    if (name == "boids") return kBoids;
    return -1;
}

const char *actionName(int action) {
    switch (action) {
    case kIdle: return "idle";
    case kFlow: return "flow";
    case kSeek: return "seek";
    case kBoids: return "boids";
    default: return "";
    }
}

float wrapPi(float a) {
    const float kTwoPi = 6.28318530717958647692f;
    while (a > M_PI) a -= kTwoPi;
    while (a < -M_PI) a += kTwoPi;
    return a;
}

}  // namespace

struct Crowd::Impl {
    CrowdField field;

    float defaultSpeed = 120.f;
    float defaultRadius = 6.f;
    float defaultTurnRate = 6.f;
    float arriveRadius = 64.f;
    float sepRadius = 28.f;
    float perceiveRadius = 64.f;
    float sepWeight = 1.f;
    float alignWeight = 0.f;
    float cohesionWeight = 0.f;
    float wanderWeight = 0.f;
    float goalWeight = 1.f;
    bool resolveOverlaps = true;
    bool clampToField = true;
    int maxAgents = 100000;
    float simTime = 0.f;

    // SOA 单位存储（id = 槽位索引）。
    std::vector<float> xs, ys, headings, vxs, vys, speeds;
    std::vector<float> radii, maxSpeeds, maxAccels, turnRates;
    std::vector<int32_t> actions, datas, avoidancePriorities;
    std::vector<uint8_t> hasTargets;
    std::vector<float> targetXs, targetYs;
    std::vector<float> wanderPhases;
    std::vector<std::string> stableIds;
    std::unordered_map<std::string, int> namedAgents;

    // 每帧重建的计数排序空间网格。
    std::vector<int32_t> cellCount, cellStart, cursor, sorted;
    int gridW = 0;
    int gridH = 0;
    float gridOriginX = 0.f;
    float gridOriginY = 0.f;
    float gridCell = 1.f;

    bool validId(int id) const { return id >= 0 && id < int(actions.size()); }

    void rebuildGrid() {
        const int n = int(actions.size());
        if (n == 0) {
            gridW = gridH = 0;
            return;
        }
        gridCell = std::max(std::max(sepRadius, perceiveRadius), 1.f);
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
        gridW = int((maxX - minX) / gridCell) + 1;
        gridH = int((maxY - minY) / gridCell) + 1;

        const size_t cells = size_t(gridW) * size_t(gridH);
        cellCount.assign(cells, 0);
        for (int i = 0; i < n; ++i) {
            int cx = int((xs[size_t(i)] - minX) / gridCell);
            int cy = int((ys[size_t(i)] - minY) / gridCell);
            cx = std::clamp(cx, 0, gridW - 1);
            cy = std::clamp(cy, 0, gridH - 1);
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
            int cx = int((xs[size_t(i)] - minX) / gridCell);
            int cy = int((ys[size_t(i)] - minY) / gridCell);
            cx = std::clamp(cx, 0, gridW - 1);
            cy = std::clamp(cy, 0, gridH - 1);
            const int c = cy * gridW + cx;
            sorted[size_t(cursor[size_t(c)]++)] = i;
        }
    }

    template <typename Fn>
    void forEachNeighbor(float qx, float qy, float radius, Fn &&fn) const {
        if (gridW <= 0 || gridH <= 0 || radius <= 0.f) return;
        const float cell = gridCell;
        const int x0 = std::max(0, int(std::floor((qx - radius - gridOriginX) / cell)));
        const int x1 = std::min(gridW - 1, int(std::floor((qx + radius - gridOriginX) / cell)));
        const int y0 = std::max(0, int(std::floor((qy - radius - gridOriginY) / cell)));
        const int y1 = std::min(gridH - 1, int(std::floor((qy + radius - gridOriginY) / cell)));
        const float r2 = radius * radius;
        for (int cy = y0; cy <= y1; ++cy) {
            const int rowBase = cy * gridW;
            for (int cx = x0; cx <= x1; ++cx) {
                const int c = rowBase + cx;
                const int begin = cellStart[size_t(c)];
                const int end = begin + cellCount[size_t(c)];
                for (int k = begin; k < end; ++k) {
                    const int j = sorted[size_t(k)];
                    const float dx = xs[size_t(j)] - qx;
                    const float dy = ys[size_t(j)] - qy;
                    if (dx * dx + dy * dy <= r2) fn(j);
                }
            }
        }
    }

    void stepAgents(float dt) {
        const int n = int(actions.size());
        const bool hasField = field.isBuilt();
        for (int i = 0; i < n; ++i) {
            const float x = xs[size_t(i)];
            const float y = ys[size_t(i)];
            const float maxSpeed = maxSpeeds[size_t(i)];
            const int action = actions[size_t(i)];
            float arriveK = 1.f;  // flow 模式到达减速系数（1=全速，0=停）

            // 基础期望方向。
            float desX = 0.f, desY = 0.f;
            if (action == kFlow) {
                float fx = 0.f, fy = 0.f;
                if (hasField) field.flowAtWorld(x, y, fx, fy);
                // 接近目标时按积分代价线性减速（arrive），避免在目标格附近振荡。
                float k = 1.f;
                if (hasField) {
                    const float c = field.costAtWorld(x, y);
                    const float cell = std::max(field.getCellSize(), 1e-6f);
                    const float arriveCells = arriveRadius / cell;
                    if (c < CrowdField::kUnreachable && arriveCells > 0.f && c < arriveCells) {
                        k = c / arriveCells;
                        arriveK = k;
                    }
                }
                desX = fx * k;
                desY = fy * k;
            } else if (action == kSeek && hasTargets[size_t(i)]) {
                const float tx = targetXs[size_t(i)] - x;
                const float ty = targetYs[size_t(i)] - y;
                const float d = std::sqrt(tx * tx + ty * ty);
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
                const float d = std::sqrt(tx * tx + ty * ty);
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
                float sx = 0.f, sy = 0.f;
                float axv = 0.f, ayv = 0.f;
                float cxv = 0.f, cyv = 0.f;
                int alignN = 0, cohN = 0;
                forEachNeighbor(x, y, queryR, [&](int j) {
                    const float dx = xs[size_t(j)] - x;
                    const float dy = ys[size_t(j)] - y;
                    const float d2 = dx * dx + dy * dy;
                    if (d2 <= 0.f) return;
                    if (sepWeight > 0.f && d2 <= sepRadius * sepRadius) {
                        const float d = std::sqrt(d2);
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
                    const float cl = std::sqrt(cmx * cmx + cmy * cmy);
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

            // 加速度限幅。
            float avx = vxs[size_t(i)];
            float avy = vys[size_t(i)];
            float ddx = desX - avx;
            float ddy = desY - avy;
            const float ddl = std::sqrt(ddx * ddx + ddy * ddy);
            const float maxDelta = std::max(maxAccels[size_t(i)] * dt, 0.f);
            if (ddl > maxDelta && ddl > 1e-6f) {
                ddx *= maxDelta / ddl;
                ddy *= maxDelta / ddl;
            }
            avx += ddx;
            avy += ddy;

            // 速度限幅。
            float sp = std::sqrt(avx * avx + avy * avy);
            if (sp > maxSpeed && sp > 1e-6f) {
                avx *= maxSpeed / sp;
                avy *= maxSpeed / sp;
                sp = maxSpeed;
            }
            vxs[size_t(i)] = avx;
            vys[size_t(i)] = avy;
            speeds[size_t(i)] = sp;

            // 接近目标时对速度做阻尼，快速消除目标点附近的来回振荡。
            if (arriveK < 1.f) {
                const float damp = 1.f - (1.f - arriveK) * std::min(1.f, dt * 4.f);
                vxs[size_t(i)] *= damp;
                vys[size_t(i)] *= damp;
                speeds[size_t(i)] *= damp;
            }

            // 转向：heading 以 turnRate·dt 为上限向速度方向收敛。
            const float spAfter = speeds[size_t(i)];
            if (spAfter > 1e-4f) {
                const float targetAng = std::atan2(vys[size_t(i)], vxs[size_t(i)]);
                const float delta = wrapPi(targetAng - headings[size_t(i)]);
                const float maxTurn = std::max(turnRates[size_t(i)] * dt, 0.f);
                headings[size_t(i)] += std::clamp(delta, -maxTurn, maxTurn);
            }

            xs[size_t(i)] += vxs[size_t(i)] * dt;
            ys[size_t(i)] += vys[size_t(i)] * dt;

            // 钳制在场边界内。
            if (clampToField && field.valid()) {
                const float cs = field.getCellSize();
                const float loX = field.getOriginX() - cs * 0.5f;
                const float hiX = field.getOriginX() + (float(field.getWidth()) - 0.5f) * cs;
                const float loY = field.getOriginY() - cs * 0.5f;
                const float hiY = field.getOriginY() + (float(field.getHeight()) - 0.5f) * cs;
                xs[size_t(i)] = std::clamp(xs[size_t(i)], loX, hiX);
                ys[size_t(i)] = std::clamp(ys[size_t(i)], loY, hiY);
            }
        }
    }

    void resolveOverlapsPass() {
        const int n = int(actions.size());
        for (int i = 0; i < n; ++i) {
            const float xi = xs[size_t(i)];
            const float yi = ys[size_t(i)];
            forEachNeighbor(xi, yi, sepRadius, [&](int j) {
                if (j <= i) return;
                float dx = xs[size_t(j)] - xi;
                float dy = ys[size_t(j)] - yi;
                float d2 = dx * dx + dy * dy;
                const float minD = radii[size_t(i)] + radii[size_t(j)];
                if (d2 >= minD * minD) return;
                const bool exactOverlap = d2 <= 1e-9f;
                if (exactOverlap) {
                    const std::string left = stableIds[size_t(i)].empty()
                                                 ? std::to_string(i) : stableIds[size_t(i)];
                    const std::string right = stableIds[size_t(j)].empty()
                                                  ? std::to_string(j) : stableIds[size_t(j)];
                    const std::string& first = left < right ? left : right;
                    const std::string& second = left < right ? right : left;
                    std::uint64_t hash = 1469598103934665603ULL;
                    for (const unsigned char value : first + "\n" + second) {
                        hash ^= value;
                        hash *= 1099511628211ULL;
                    }
                    const float angle = static_cast<float>(hash % 104729ULL) /
                                        104729.0f * 6.28318530717958647692f;
                    dx = std::cos(angle);
                    dy = std::sin(angle);
                    if (left > right) { dx = -dx; dy = -dy; }
                    d2 = 1.0f;
                }
                const float d = std::sqrt(d2);
                const float push = minD - (exactOverlap ? 0.0f : d);
                const float nx = dx / d;
                const float ny = dy / d;
                float leftShare = 0.5f;
                float rightShare = 0.5f;
                if (avoidancePriorities[size_t(i)] > avoidancePriorities[size_t(j)]) {
                    leftShare = 0.0f; rightShare = 1.0f;
                } else if (avoidancePriorities[size_t(j)] > avoidancePriorities[size_t(i)]) {
                    leftShare = 1.0f; rightShare = 0.0f;
                }
                xs[size_t(i)] -= nx * push * leftShare;
                ys[size_t(i)] -= ny * push * leftShare;
                xs[size_t(j)] += nx * push * rightShare;
                ys[size_t(j)] += ny * push * rightShare;
            });
        }
    }

    void resolveWalls() {
        if (!field.valid()) return;
        const int n = int(actions.size());
        for (int i = 0; i < n; ++i) {
            // 墙角处一次推出可能仍与相邻阻挡格重叠，迭代 2 次。
            for (int pass = 0; pass < 2; ++pass) {
                if (!field.resolvePenetration(xs[size_t(i)], ys[size_t(i)], radii[size_t(i)]))
                    break;
            }
        }
    }
};

Module_IMPL(Crowd, new Crowd());

Crowd::Crowd() : impl_(std::make_unique<Impl>()) {}
Crowd::~Crowd() = default;

// --- 流场 ---

void Crowd::resizeField(int width, int height, float cellSize, float originX, float originY) {
    impl_->field.resize(width, height, cellSize, originX, originY);
}

void Crowd::setBlocked(int cx, int cy, bool blocked) {
    impl_->field.setBlocked(cx, cy, blocked);
}

void Crowd::setCellCost(int cx, int cy, float cost) {
    impl_->field.setCellCost(cx, cy, cost);
}

float Crowd::getCellCost(int cx, int cy) const { return impl_->field.getCellCost(cx, cy); }

void Crowd::buildFlowField(int gx, int gy) {
    impl_->field.setGoal(gx, gy);
    impl_->field.build();
}

void Crowd::addFlowGoal(int gx, int gy) { impl_->field.addGoal(gx, gy); }

void Crowd::clearFlowGoals() { impl_->field.clearGoals(); }

void Crowd::build() { impl_->field.build(); }

bool Crowd::isFieldBuilt() const { return impl_->field.isBuilt(); }

bool Crowd::isReachable(int cx, int cy) const { return impl_->field.isReachable(cx, cy); }

int Crowd::getFieldWidth() const { return impl_->field.getWidth(); }
int Crowd::getFieldHeight() const { return impl_->field.getHeight(); }
float Crowd::getCellSize() const { return impl_->field.getCellSize(); }
float Crowd::getFieldOriginX() const { return impl_->field.getOriginX(); }
float Crowd::getFieldOriginY() const { return impl_->field.getOriginY(); }

FlowVec Crowd::flowAtWorld(float wx, float wy) const {
    FlowVec v;
    impl_->field.flowAtWorld(wx, wy, v.x, v.y);
    return v;
}

float Crowd::costAtWorld(float wx, float wy) const { return impl_->field.costAtWorld(wx, wy); }

FlowVec Crowd::flowAtCell(int cx, int cy) const {
    FlowVec v;
    impl_->field.flowAtCell(cx, cy, v.x, v.y);
    return v;
}

// --- 单位 ---

int Crowd::addAgent(float x, float y, float heading, float radius) {
    auto &d = *impl_;
    if (int(d.actions.size()) >= d.maxAgents) return -1;
    const int id = int(d.xs.size());
    d.xs.push_back(x);
    d.ys.push_back(y);
    d.headings.push_back(heading);
    d.vxs.push_back(0.f);
    d.vys.push_back(0.f);
    d.speeds.push_back(0.f);
    d.radii.push_back(radius > 0.f ? radius : d.defaultRadius);
    d.maxSpeeds.push_back(d.defaultSpeed);
    d.maxAccels.push_back(d.defaultSpeed * 2.f);
    d.turnRates.push_back(d.defaultTurnRate);
    d.actions.push_back(kFlow);
    d.datas.push_back(0);
    d.avoidancePriorities.push_back(0);
    d.hasTargets.push_back(0);
    d.targetXs.push_back(0.f);
    d.targetYs.push_back(0.f);
    d.wanderPhases.push_back(float(id) * 2.399963f);
    d.stableIds.emplace_back();
    return id;
}

int Crowd::addNamedAgent(const std::string &stableId, float x, float y, float heading, float radius) {
    auto &d = *impl_;
    if (stableId.empty() || d.namedAgents.count(stableId) != 0) return -1;
    const int index = addAgent(x, y, heading, radius);
    if (index < 0) return -1;
    d.stableIds[static_cast<size_t>(index)] = stableId;
    d.namedAgents[stableId] = index;
    return index;
}

bool Crowd::hasNamedAgent(const std::string &stableId) const {
    return impl_->namedAgents.count(stableId) != 0;
}

int Crowd::getNamedAgentIndex(const std::string &stableId) const {
    const auto found = impl_->namedAgents.find(stableId);
    return found == impl_->namedAgents.end() ? -1 : found->second;
}

std::string Crowd::getAgentStableId(int index) const {
    return impl_->validId(index) ? impl_->stableIds[static_cast<size_t>(index)] : std::string{};
}

bool Crowd::removeNamedAgent(const std::string &stableId) {
    const int index = getNamedAgentIndex(stableId);
    return index >= 0 && removeAgent(index);
}

bool Crowd::removeAgent(int id) {
    auto &d = *impl_;
    if (!d.validId(id)) return false;
    const int last = int(d.xs.size()) - 1;
    const std::string removedStableId = d.stableIds[static_cast<size_t>(id)];
    if (id != last) {
        d.xs[size_t(id)] = d.xs[size_t(last)];
        d.ys[size_t(id)] = d.ys[size_t(last)];
        d.headings[size_t(id)] = d.headings[size_t(last)];
        d.vxs[size_t(id)] = d.vxs[size_t(last)];
        d.vys[size_t(id)] = d.vys[size_t(last)];
        d.speeds[size_t(id)] = d.speeds[size_t(last)];
        d.radii[size_t(id)] = d.radii[size_t(last)];
        d.maxSpeeds[size_t(id)] = d.maxSpeeds[size_t(last)];
        d.maxAccels[size_t(id)] = d.maxAccels[size_t(last)];
        d.turnRates[size_t(id)] = d.turnRates[size_t(last)];
        d.actions[size_t(id)] = d.actions[size_t(last)];
        d.datas[size_t(id)] = d.datas[size_t(last)];
        d.avoidancePriorities[size_t(id)] = d.avoidancePriorities[size_t(last)];
        d.hasTargets[size_t(id)] = d.hasTargets[size_t(last)];
        d.targetXs[size_t(id)] = d.targetXs[size_t(last)];
        d.targetYs[size_t(id)] = d.targetYs[size_t(last)];
        d.wanderPhases[size_t(id)] = d.wanderPhases[size_t(last)];
        d.stableIds[size_t(id)] = d.stableIds[size_t(last)];
        if (!d.stableIds[size_t(id)].empty()) d.namedAgents[d.stableIds[size_t(id)]] = id;
    }
    if (!removedStableId.empty()) d.namedAgents.erase(removedStableId);
    d.xs.pop_back();
    d.ys.pop_back();
    d.headings.pop_back();
    d.vxs.pop_back();
    d.vys.pop_back();
    d.speeds.pop_back();
    d.radii.pop_back();
    d.maxSpeeds.pop_back();
    d.maxAccels.pop_back();
    d.turnRates.pop_back();
    d.actions.pop_back();
    d.datas.pop_back();
    d.avoidancePriorities.pop_back();
    d.hasTargets.pop_back();
    d.targetXs.pop_back();
    d.targetYs.pop_back();
    d.wanderPhases.pop_back();
    d.stableIds.pop_back();
    return true;
}

void Crowd::clearAgents() {
    auto &d = *impl_;
    d.xs.clear();
    d.ys.clear();
    d.headings.clear();
    d.vxs.clear();
    d.vys.clear();
    d.speeds.clear();
    d.radii.clear();
    d.maxSpeeds.clear();
    d.maxAccels.clear();
    d.turnRates.clear();
    d.actions.clear();
    d.datas.clear();
    d.avoidancePriorities.clear();
    d.hasTargets.clear();
    d.targetXs.clear();
    d.targetYs.clear();
    d.wanderPhases.clear();
    d.stableIds.clear();
    d.namedAgents.clear();
    d.gridW = d.gridH = 0;
}

int Crowd::getAgentCount() const { return int(impl_->xs.size()); }

void Crowd::setMaxAgents(int maxAgents) {
    impl_->maxAgents = std::max(maxAgents, 0);
}

int Crowd::getMaxAgents() const { return impl_->maxAgents; }

bool Crowd::setAgentAction(int id, const std::string &action) {
    if (!impl_->validId(id)) return false;
    const int a = actionFromName(action);
    if (a < 0) return false;
    impl_->actions[size_t(id)] = a;
    return true;
}

std::string Crowd::getAgentAction(int id) const {
    if (!impl_->validId(id)) return "";
    return actionName(impl_->actions[size_t(id)]);
}

bool Crowd::setAgentTarget(int id, float tx, float ty) {
    if (!impl_->validId(id)) return false;
    impl_->hasTargets[size_t(id)] = 1;
    impl_->targetXs[size_t(id)] = tx;
    impl_->targetYs[size_t(id)] = ty;
    return true;
}

bool Crowd::clearAgentTarget(int id) {
    if (!impl_->validId(id)) return false;
    impl_->hasTargets[size_t(id)] = 0;
    return true;
}

bool Crowd::setAgentSpeed(int id, float speed) {
    if (!impl_->validId(id) || speed < 0.f) return false;
    impl_->maxSpeeds[size_t(id)] = speed;
    return true;
}

bool Crowd::setAgentAccel(int id, float accel) {
    if (!impl_->validId(id) || accel < 0.f) return false;
    impl_->maxAccels[size_t(id)] = accel;
    return true;
}

bool Crowd::setAgentTurnRate(int id, float radPerSec) {
    if (!impl_->validId(id) || radPerSec < 0.f) return false;
    impl_->turnRates[size_t(id)] = radPerSec;
    return true;
}

bool Crowd::setAgentRadius(int id, float radius) {
    if (!impl_->validId(id) || radius < 0.f) return false;
    impl_->radii[size_t(id)] = radius;
    return true;
}

bool Crowd::setAgentData(int id, int data) {
    if (!impl_->validId(id)) return false;
    impl_->datas[size_t(id)] = data;
    return true;
}

int Crowd::getAgentData(int id) const {
    if (!impl_->validId(id)) return 0;
    return impl_->datas[size_t(id)];
}

Result<void> Crowd::setAgentAvoidancePriority(int id, int priority) {
    if (!impl_->validId(id))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "Crowd avoidance-priority agent slot was not found", "id"));
    impl_->avoidancePriorities[size_t(id)] = priority;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

int Crowd::getAgentAvoidancePriority(int id) const {
    return impl_->validId(id) ? impl_->avoidancePriorities[size_t(id)] : 0;
}

bool Crowd::setAgentPosition(int id, float x, float y) {
    if (!impl_->validId(id)) return false;
    impl_->xs[size_t(id)] = x;
    impl_->ys[size_t(id)] = y;
    return true;
}

AgentState Crowd::getAgentState(int id) const {
    AgentState s;
    if (!impl_->validId(id)) {
        s.action = -1;
        return s;
    }
    s.x = impl_->xs[size_t(id)];
    s.y = impl_->ys[size_t(id)];
    s.heading = impl_->headings[size_t(id)];
    s.speed = impl_->speeds[size_t(id)];
    s.vx = impl_->vxs[size_t(id)];
    s.vy = impl_->vys[size_t(id)];
    s.action = impl_->actions[size_t(id)];
    s.data = impl_->datas[size_t(id)];
    s.avoidancePriority = impl_->avoidancePriorities[size_t(id)];
    return s;
}

// --- 群体参数 ---

void Crowd::setDefaultSpeed(float speed) { impl_->defaultSpeed = std::max(speed, 0.f); }
void Crowd::setDefaultRadius(float radius) { impl_->defaultRadius = std::max(radius, 0.f); }
void Crowd::setDefaultTurnRate(float radPerSec) {
    impl_->defaultTurnRate = std::max(radPerSec, 0.f);
}
void Crowd::setArriveRadius(float radius) { impl_->arriveRadius = std::max(radius, 0.f); }
void Crowd::setSeparationRadius(float radius) { impl_->sepRadius = std::max(radius, 0.f); }
void Crowd::setPerceptionRadius(float radius) { impl_->perceiveRadius = std::max(radius, 0.f); }
void Crowd::setSeparationWeight(float weight) { impl_->sepWeight = weight; }
void Crowd::setAlignmentWeight(float weight) { impl_->alignWeight = weight; }
void Crowd::setCohesionWeight(float weight) { impl_->cohesionWeight = weight; }
void Crowd::setWanderWeight(float weight) { impl_->wanderWeight = weight; }
void Crowd::setGoalWeight(float weight) { impl_->goalWeight = weight; }
void Crowd::setResolveOverlaps(bool enable) { impl_->resolveOverlaps = enable; }
void Crowd::setClampToField(bool enable) { impl_->clampToField = enable; }

void Crowd::step(float dt) {
    EV_PROFILE_MODULE("crowd", "Crowd::step");
    if (dt <= 0.f) return;
    impl_->simTime += dt;
    impl_->rebuildGrid();
    impl_->stepAgents(dt);
    if (impl_->resolveOverlaps) impl_->resolveOverlapsPass();
    impl_->resolveWalls();  // 最后兜底：任何情况都不允许单位留在阻挡格内
}

// --- 脚本绑定 ---

void Crowd::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Crowd::create, false);
    expose(cls);

    auto state = table.addClass<AgentState>("CrowdAgentState", ssq::Class::Ctor<AgentState()>());
    state.addVar("x", &AgentState::x);
    state.addVar("y", &AgentState::y);
    state.addVar("heading", &AgentState::heading);
    state.addVar("speed", &AgentState::speed);
    state.addVar("vx", &AgentState::vx);
    state.addVar("vy", &AgentState::vy);
    state.addVar("action", &AgentState::action);
    state.addVar("data", &AgentState::data);
    state.addVar("avoidancePriority", &AgentState::avoidancePriority);

    auto flow = table.addClass<FlowVec>("CrowdFlowVec", ssq::Class::Ctor<FlowVec()>());
    flow.addVar("x", &FlowVec::x);
    flow.addVar("y", &FlowVec::y);
}

void Crowd::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Crowd::getName);

    // 流场
    cls.addFunc("resizeField", &Crowd::resizeField);
    cls.addFunc("setBlocked", &Crowd::setBlocked);
    cls.addFunc("setCellCost", &Crowd::setCellCost);
    cls.addFunc("getCellCost", &Crowd::getCellCost);
    cls.addFunc("buildFlowField", &Crowd::buildFlowField);
    cls.addFunc("addFlowGoal", &Crowd::addFlowGoal);
    cls.addFunc("clearFlowGoals", &Crowd::clearFlowGoals);
    cls.addFunc("build", &Crowd::build);
    cls.addFunc("isFieldBuilt", &Crowd::isFieldBuilt);
    cls.addFunc("isReachable", &Crowd::isReachable);
    cls.addFunc("getFieldWidth", &Crowd::getFieldWidth);
    cls.addFunc("getFieldHeight", &Crowd::getFieldHeight);
    cls.addFunc("getCellSize", &Crowd::getCellSize);
    cls.addFunc("getFieldOriginX", &Crowd::getFieldOriginX);
    cls.addFunc("getFieldOriginY", &Crowd::getFieldOriginY);
    cls.addFunc("flowAtWorld", &Crowd::flowAtWorld);
    cls.addFunc("costAtWorld", &Crowd::costAtWorld);
    cls.addFunc("flowAtCell", &Crowd::flowAtCell);

    // 单位
    cls.addFunc("addAgent", &Crowd::addAgent);
    cls.addFunc("addNamedAgent", &Crowd::addNamedAgent);
    cls.addFunc("hasNamedAgent", &Crowd::hasNamedAgent);
    cls.addFunc("getNamedAgentIndex", &Crowd::getNamedAgentIndex);
    cls.addFunc("getAgentStableId", &Crowd::getAgentStableId);
    cls.addFunc("removeNamedAgent", &Crowd::removeNamedAgent);
    cls.addFunc("removeAgent", &Crowd::removeAgent);
    cls.addFunc("clearAgents", &Crowd::clearAgents);
    cls.addFunc("getAgentCount", &Crowd::getAgentCount);
    cls.addFunc("setMaxAgents", &Crowd::setMaxAgents);
    cls.addFunc("getMaxAgents", &Crowd::getMaxAgents);
    cls.addFunc("setAgentAction", &Crowd::setAgentAction);
    cls.addFunc("getAgentAction", &Crowd::getAgentAction);
    cls.addFunc("setAgentTarget", &Crowd::setAgentTarget);
    cls.addFunc("clearAgentTarget", &Crowd::clearAgentTarget);
    cls.addFunc("setAgentSpeed", &Crowd::setAgentSpeed);
    cls.addFunc("setAgentAccel", &Crowd::setAgentAccel);
    cls.addFunc("setAgentTurnRate", &Crowd::setAgentTurnRate);
    cls.addFunc("setAgentRadius", &Crowd::setAgentRadius);
    cls.addFunc("setAgentData", &Crowd::setAgentData);
    cls.addFunc("getAgentData", &Crowd::getAgentData);
    cls.addFunc("setAgentAvoidancePriority",
                [](Crowd* crowd, int id, int priority) { return crowd->setAgentAvoidancePriority(id, priority).ok(); });
    cls.addFunc("getAgentAvoidancePriority", &Crowd::getAgentAvoidancePriority);
    cls.addFunc("setAgentPosition", &Crowd::setAgentPosition);
    cls.addFunc("getAgentState", &Crowd::getAgentState);

    // 群体参数
    cls.addFunc("setDefaultSpeed", &Crowd::setDefaultSpeed);
    cls.addFunc("setDefaultRadius", &Crowd::setDefaultRadius);
    cls.addFunc("setDefaultTurnRate", &Crowd::setDefaultTurnRate);
    cls.addFunc("setArriveRadius", &Crowd::setArriveRadius);
    cls.addFunc("setSeparationRadius", &Crowd::setSeparationRadius);
    cls.addFunc("setPerceptionRadius", &Crowd::setPerceptionRadius);
    cls.addFunc("setSeparationWeight", &Crowd::setSeparationWeight);
    cls.addFunc("setAlignmentWeight", &Crowd::setAlignmentWeight);
    cls.addFunc("setCohesionWeight", &Crowd::setCohesionWeight);
    cls.addFunc("setWanderWeight", &Crowd::setWanderWeight);
    cls.addFunc("setGoalWeight", &Crowd::setGoalWeight);
    cls.addFunc("setResolveOverlaps", &Crowd::setResolveOverlaps);
    cls.addFunc("setClampToField", &Crowd::setClampToField);

    // 批量读取（脚本预分配数组）
    cls.addFunc("getPositions", [](Crowd *c, ssq::Array xs, ssq::Array ys) {
        if (!c) return;
        const size_t n = std::min<size_t>({xs.size(), ys.size(), c->impl_->xs.size()});
        for (size_t i = 0; i < n; ++i) {
            xs.set(i, c->impl_->xs[i]);
            ys.set(i, c->impl_->ys[i]);
        }
    });
    cls.addFunc("getHeadings", [](Crowd *c, ssq::Array hs) {
        if (!c) return;
        const size_t n = std::min<size_t>(hs.size(), c->impl_->headings.size());
        for (size_t i = 0; i < n; ++i) hs.set(i, c->impl_->headings[i]);
    });
    cls.addFunc("step", &Crowd::step);
}

}  // namespace eve::crowd
