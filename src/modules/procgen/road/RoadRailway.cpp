#include "procgen/road/RoadRailway.h"

#include "common/Diagnostic.h"

#include <array>
#include <cmath>

namespace eve::procgen::road {
namespace {

constexpr float kStandardGauge = 1.435f;
constexpr float kTrackCenters  = 4.0f;

RoadFacilityPrefab sleeperPrefab(float halfWidth) {
    RoadFacilityPrefab prefab;
    prefab.assetId          = "procgen.railway.sleeper";
    prefab.length           = 0.65f;
    prefab.automaticSpacing = true;
    prefab.averageSpacing   = true;
    prefab.middleCopies     = 1;
    const float x0 = -halfWidth, x1 = halfWidth, y0 = 0.f, y1 = 0.10f, z0 = 0.08f, z1 = 0.30f;
    const std::array<std::array<float, 3>, 8> points{{{x0, y0, z0},
                                                      {x1, y0, z0},
                                                      {x1, y1, z0},
                                                      {x0, y1, z0},
                                                      {x0, y0, z1},
                                                      {x1, y0, z1},
                                                      {x1, y1, z1},
                                                      {x0, y1, z1}}};
    const int   faces[6][4]{{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 4, 7, 3}, {1, 2, 6, 5}, {0, 1, 5, 4}, {3, 7, 6, 2}};
    const float normals[6][3]{{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}};
    for (int f = 0; f < 6; ++f) {
        const auto base = static_cast<std::uint32_t>(prefab.vertices.size());
        for (int i = 0; i < 4; ++i) {
            const auto& p = points[static_cast<std::size_t>(faces[f][i])];
            prefab.vertices.push_back({p[0], p[1], p[2], normals[f][0], normals[f][1], normals[f][2],
                                       i == 1 || i == 2 ? 1.f : 0.f, i >= 2 ? 1.f : 0.f});
        }
        prefab.triangles.push_back({base, base + 1u, base + 2u, RoadMaterial::Deck});
        prefab.triangles.push_back({base, base + 2u, base + 3u, RoadMaterial::Deck});
    }
    return prefab;
}

RoadContinuousFacility railFacility(std::uint32_t id, float lateral) {
    RoadContinuousFacility facility;
    facility.id             = id;
    facility.lateralOffset  = lateral;
    facility.verticalOffset = 0.10f;
    facility.uvMeters       = 1.5f;
    facility.profile        = {{-.035f, 0.f, RoadMaterial::Curb},
                               {.035f, 0.f, RoadMaterial::Curb},
                               {.035f, .14f, RoadMaterial::Curb},
                               {-.035f, .14f, RoadMaterial::Curb}};
    return facility;
}

RoadContinuousFacility sleeperFacility(std::uint32_t id, float lateral, float halfWidth) {
    RoadContinuousFacility facility;
    facility.id             = id;
    facility.lateralOffset  = lateral;
    facility.verticalOffset = 0.f;
    facility.uvMeters       = 2.f;
    facility.capStart       = false;
    facility.capEnd         = false;
    facility.profile        = {{-halfWidth, 0.f, RoadMaterial::Deck},
                               {halfWidth, 0.f, RoadMaterial::Deck},
                               {halfWidth, .10f, RoadMaterial::Deck},
                               {-halfWidth, .10f, RoadMaterial::Deck}};
    facility.prefab         = sleeperPrefab(halfWidth);
    return facility;
}

}  // namespace

Result<RoadStyle> makeRailwayStyle(int tracks) {
    if (tracks < 1 || tracks > 2)
        return Result<RoadStyle>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "railway tracks must be 1 or 2", "tracks"));

    RoadStyle style;
    style.laneWidth             = kStandardGauge;
    style.curbWidth             = 0.f;
    style.curbHeight            = 0.f;
    style.sidewalkWidth         = 0.f;
    style.sidewalkHeight        = 0.f;
    style.markingWidth          = 0.f;
    style.dashLength            = 0.f;
    style.dashGap               = 0.f;
    style.deckThickness         = 0.35f;
    style.pierClearance         = 100.f;
    style.uvMeters              = 4.f;
    style.sideObjectsLeft       = false;
    style.sideObjectsRight      = false;
    style.automaticStructures   = true;
    style.speedLimitMps         = 33.3333f;  // ~120 km/h rail cruise default for nav overlay.
    style.samplingSpacingMeters = 2.f;

    const float halfExtent = tracks == 1 ? 1.6f : (0.5f * kTrackCenters + 1.6f);
    const float toe        = halfExtent + 0.45f;
    // Open loft ribbon: left toe → ballast crown (asphalt driving span) → right toe.
    style.crossSection = {{-toe, -0.18f, RoadMaterial::Deck},
                          {-halfExtent, 0.f, RoadMaterial::Asphalt},
                          {halfExtent, 0.f, RoadMaterial::Deck},
                          {toe, -0.18f, RoadMaterial::Deck}};

    std::uint32_t nextId = 101;
    for (int track = 0; track < tracks; ++track) {
        const float center =
            tracks == 1 ? 0.f : (static_cast<float>(track) - 0.5f * static_cast<float>(tracks - 1)) * kTrackCenters;
        style.facilities.push_back(railFacility(nextId++, center - 0.5f * kStandardGauge));
        style.facilities.push_back(railFacility(nextId++, center + 0.5f * kStandardGauge));
        style.facilities.push_back(sleeperFacility(nextId++, center, 0.5f * kStandardGauge + 0.25f));
    }

    auto valid = makeRoadProfile(style, tracks, 0);
    if (!valid.ok()) return Result<RoadStyle>::failure(valid.status());
    return Result<RoadStyle>::success(std::move(style));
}

Result<void> applyRailwayRoadType(RoadNetwork& network, int tracks) {
    auto style = makeRailwayStyle(tracks);
    if (!style.ok()) return Result<void>::failure(style.status());

    auto candidate = network;
    for (const auto& edge : network.edges()) {
        RoadStyle next           = style.value();
        next.structures          = edge.style.structures;
        next.automaticStructures = edge.style.automaticStructures;
        next.deckThickness       = edge.style.deckThickness;
        next.pierWidth           = edge.style.pierWidth;
        next.pierDepth           = edge.style.pierDepth;
        next.pierSpacing         = edge.style.pierSpacing;
        next.pierClearance       = edge.style.pierClearance;
        next.terrainPolicy       = edge.style.terrainPolicy;
        auto styled              = candidate.setEdgeStyle(edge.id, next);
        if (!styled.ok()) return styled;
        auto lanes = candidate.setEdgeLaneCounts(edge.id, tracks, 0);
        if (!lanes.ok()) return Result<void>::failure(lanes.status());
    }
    for (const auto& node : candidate.nodes()) {
        int incident = 0;
        for (const auto& edge : candidate.edges())
            if (edge.from == node.id || edge.to == node.id) ++incident;
        if (incident < 2) continue;
        auto linked = candidate.connectAllTurns(node.id);
        if (!linked.ok()) return Result<void>::failure(linked.status());
    }
    network = std::move(candidate);
    return Result<void>::success();
}

}  // namespace eve::procgen::road
