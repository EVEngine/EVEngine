#pragma once

#include "crowd/Crowd.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace eve::crowd {

struct Crowd::Impl {
    CrowdField field;

    float             defaultSpeed    = 120.f;
    float             defaultRadius   = 6.f;
    float             defaultTurnRate = 6.f;
    float             arriveRadius    = 64.f;
    float             sepRadius       = 28.f;
    float             perceiveRadius  = 64.f;
    float             sepWeight       = 1.f;
    float             alignWeight     = 0.f;
    float             cohesionWeight  = 0.f;
    float             wanderWeight    = 0.f;
    float             goalWeight      = 1.f;
    bool              resolveOverlaps = true;
    bool              clampToField    = true;
    int               maxAgents       = 100000;
    float             simTime         = 0.f;
    AvoidanceSettings avoidance;
    std::int64_t      avoidanceChecks      = 0;
    int               avoidanceTruncations = 0;
    float             avoidanceMaxSpeed = 0.f, avoidanceMaxRadius = 0.f;
    struct AvoidanceNeighbor {
        size_t slot;
        double distance2;
    };
    std::vector<AvoidanceNeighbor> avoidanceNeighbors;

    // SOA 单位存储（id = 槽位索引）。
    std::vector<float>                   xs, ys, headings, vxs, vys, speeds;
    std::vector<float>                   nextVxs, nextVys, correctionXs, correctionYs;
    std::vector<float>                   preferredVxs, preferredVys, arriveFactors;
    std::vector<float>                   radii, maxSpeeds, maxAccels, turnRates;
    std::vector<int32_t>                 actions, datas, avoidancePriorities;
    std::vector<uint8_t>                 hasTargets;
    std::vector<AgentInteraction>        interactions;
    std::vector<float>                   targetXs, targetYs;
    std::vector<float>                   wanderPhases;
    std::vector<std::string>             stableIds;
    std::unordered_map<std::string, int> namedAgents;

    // 每帧重建的计数排序空间网格。
    std::vector<int32_t> cellCount, cellStart, cursor, sorted;
    int                  gridW       = 0;
    int                  gridH       = 0;
    float                gridOriginX = 0.f;
    float                gridOriginY = 0.f;
    float                gridCell    = 1.f;

    bool validId(int id) const { return id >= 0 && id < int(actions.size()); }

    bool canInteract(size_t a, size_t b) const {
        return (interactions[a].layer & interactions[b].mask) != 0 &&
               (interactions[b].layer & interactions[a].mask) != 0;
    }

    void rebuildGrid();

    template <typename Fn>
    void forEachNeighbor(float qx, float qy, float radius, Fn&& fn) const {
        if (gridW <= 0 || gridH <= 0 || radius <= 0.f) return;
        const float cell        = gridCell;
        const auto  boundedCell = [&](double coordinate, float origin, int extent) {
            return int(std::clamp(std::floor((coordinate - origin) / cell), 0.0, double(extent - 1)));
        };
        const int   x0 = boundedCell(double(qx) - radius, gridOriginX, gridW);
        const int   x1 = boundedCell(double(qx) + radius, gridOriginX, gridW);
        const int   y0 = boundedCell(double(qy) - radius, gridOriginY, gridH);
        const int   y1 = boundedCell(double(qy) + radius, gridOriginY, gridH);
        const float r2 = radius * radius;
        for (int cy = y0; cy <= y1; ++cy) {
            const int rowBase = cy * gridW;
            for (int cx = x0; cx <= x1; ++cx) {
                const int c     = rowBase + cx;
                const int begin = cellStart[size_t(c)];
                const int end   = begin + cellCount[size_t(c)];
                for (int k = begin; k < end; ++k) {
                    const int   j  = sorted[size_t(k)];
                    const float dx = xs[size_t(j)] - qx;
                    const float dy = ys[size_t(j)] - qy;
                    if (dx * dx + dy * dy <= r2) fn(j);
                }
            }
        }
    }

    void stepAgents(float dt);
    void selectAvoidanceVelocity(size_t index, float dt, float& vx, float& vy);

    float resolveOverlapsPass();

    void resolveWalls();
};

}  // namespace eve::crowd
