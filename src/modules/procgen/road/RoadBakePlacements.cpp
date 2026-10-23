#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadBakeInternal.h"

#include <map>
#include <tuple>
#include "common/Capability.h"
#include "common/Diagnostic.h"
#include "common/ProcgenAssetBounds.h"
#include "common/ProcgenWorldQuery.h"
#include "procgen/spline/SplinePath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace eve::procgen::road::detail {

Result<void> bakePlacementPoints(PointSet& placements, const RoadNetwork& network, const RoadBakeOptions& options,
                                 const std::unordered_set<std::uint32_t>& activeJunctionNodes,
                                 RoadFrameCache&                          frameCache) {
    const bool needsTerrain = std::any_of(network.edges().begin(), network.edges().end(), [](const RoadEdge& edge) {
        return (edge.style.sideTerrain.mode != RoadSideTerrainMode::None ||
                edge.style.sideTerrain.alignment == RoadSideAlignment::TerrainNormal) &&
               (edge.style.sideObjectsLeft || edge.style.sideObjectsRight);
    });
    auto*      terrainQuery = options.placementWorldQuery;
    if (needsTerrain && !terrainQuery) terrainQuery = eve::cap::query<eve::IProcgenWorldQuery>();
    if (needsTerrain && !terrainQuery)
        return bakeFail<void>(DiagnosticCode::Unsupported, "side-object terrain snapping requires IProcgenWorldQuery",
                              "sideTerrain");
    std::size_t terrainWork = 0;
    auto*       assetBounds = options.placementAssetBounds;
    const bool  needsAssets = std::any_of(network.edges().begin(), network.edges().end(), [](const RoadEdge& edge) {
        return edge.style.sideTerrain.asset.has_value() && (edge.style.sideObjectsLeft || edge.style.sideObjectsRight);
    });
    if (needsAssets && !assetBounds) assetBounds = eve::cap::query<eve::IProcgenAssetBounds>();
    if (needsAssets && !assetBounds)
        return bakeFail<void>(DiagnosticCode::Unsupported, "side-object prefab bounds require IProcgenAssetBounds",
                              "sideTerrain.assetRef");
    std::map<std::tuple<eve::PersistentId, eve::PersistentId, eve::PrefabRootPose>, std::array<float, 6>>
                                                            resolvedAssets;
    std::unordered_map<std::uint32_t, RoadSideTerrainRule>  terrainRules;
    std::unordered_map<std::uint32_t, std::array<float, 6>> edgeAssetBounds;
    for (const auto& edge : network.edges()) {
        auto rule = edge.style.sideTerrain;
        if (rule.asset.has_value() && (edge.style.sideObjectsLeft || edge.style.sideObjectsRight)) {
            const auto key   = std::tuple{rule.asset->asset.id(), rule.asset->buildId, rule.asset->rootPose};
            auto       found = resolvedAssets.find(key);
            if (found == resolvedAssets.end()) {
                auto resolved = assetBounds->localBounds(rule.asset->asset, rule.asset->buildId, rule.asset->rootPose);
                if (!resolved.ok()) return Result<void>::failure(resolved.status());
                const auto& bounds = resolved.value();
                for (unsigned axis = 0; axis < 3; ++axis)
                    if (!std::isfinite(bounds[axis]) || !std::isfinite(bounds[axis + 3]) ||
                        bounds[axis] > bounds[axis + 3] || std::fabs(bounds[axis]) > 10000.f ||
                        std::fabs(bounds[axis + 3]) > 10000.f)
                        return bakeFail<void>(DiagnosticCode::PreconditionViolation,
                                              "asset provider returned invalid prefab bounds", "sideTerrain.assetRef");
                found = resolvedAssets.emplace(key, bounds).first;
            }
            const auto& bounds = found->second;
            rule.minSide       = bounds[0];
            rule.maxSide       = bounds[3];
            rule.minForward    = bounds[2];
            rule.maxForward    = bounds[5];
            edgeAssetBounds.emplace(edge.id, bounds);
        }
        terrainRules.emplace(edge.id, std::move(rule));
    }
    if (!std::isfinite(options.sideObjectSpacing) || options.sideObjectSpacing < 0.25f ||
        !std::isfinite(options.sideObjectOffset) || options.sideObjectOffset < 0.f || options.maximumPlacements < 1)
        return bakeFail<void>(DiagnosticCode::InvalidArgument, "road placement options are invalid", "placements");
    if (!std::isfinite(options.sideObjectClearance) || options.sideObjectClearance < 0.f)
        return bakeFail<void>(DiagnosticCode::InvalidArgument, "road side-object clearance is invalid",
                              "placements.clearance");

    struct EdgeSource {
        const RoadEdge*                    edge = nullptr;
        SplinePath                         spline;
        float                              length    = 0.f;
        float                              halfWidth = 0.f;
        float                              sideStart = 0.f;
        float                              sideEnd   = 0.f;
        std::size_t                        sideCount = 0;
        float                              sideFirst = 0.f, sideStep = 0.f;
        int                                arcSamples = 32;
        std::optional<RoadSurfacePolyline> surface;
    };
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> adjacency;
    for (const auto& node : network.nodes()) adjacency.emplace(node.id, std::vector<std::uint32_t>{});
    for (const auto& edge : network.edges()) {
        adjacency[edge.from].push_back(edge.to);
        adjacency[edge.to].push_back(edge.from);
    }

    std::vector<std::vector<std::uint32_t>> roundabouts;
    auto                                    canonicalCycle = [](std::vector<std::uint32_t> cycle) {
        const auto minimum = std::min_element(cycle.begin(), cycle.end());
        std::rotate(cycle.begin(), minimum, cycle.end());
        std::vector<std::uint32_t> reversed{cycle.front()};
        for (auto it = cycle.rbegin(); it != cycle.rend() - 1; ++it) reversed.push_back(*it);
        return reversed < cycle ? reversed : cycle;
    };
    for (const auto& node : network.nodes()) {
        if (adjacency[node.id].size() != 3u) continue;
        std::vector<std::uint32_t>         path{node.id};
        std::function<void(std::uint32_t)> visit = [&](std::uint32_t current) {
            for (const auto next : adjacency[current]) {
                if (next == path.front()) {
                    if (path.size() < 3u || path.size() > 12u) continue;
                    auto cycle = canonicalCycle(path);
                    if (std::find(roundabouts.begin(), roundabouts.end(), cycle) != roundabouts.end()) continue;
                    float centerX = 0.f, centerZ = 0.f, minY = std::numeric_limits<float>::max();
                    float maxY       = -std::numeric_limits<float>::max();
                    bool  valid      = true;
                    int   entryCount = 0;
                    for (const auto id : cycle) {
                        if (adjacency[id].size() < 2u || adjacency[id].size() > 3u) {
                            valid = false;
                            break;
                        }
                        if (adjacency[id].size() == 3u) ++entryCount;
                        auto value = network.nodeResult(id);
                        if (!value.ok()) {
                            valid = false;
                            break;
                        }
                        centerX += value.value().x;
                        centerZ += value.value().z;
                        minY = std::min(minY, value.value().y);
                        maxY = std::max(maxY, value.value().y);
                    }
                    if (!valid || entryCount < 3 || maxY - minY > 1.f) continue;
                    for (std::size_t i = 0; i < cycle.size(); ++i) {
                        const auto a = cycle[i];
                        const auto b = cycle[(i + 1u) % cycle.size()];
                        const auto ringEdge =
                            std::find_if(network.edges().begin(), network.edges().end(), [&](const RoadEdge& edge) {
                                return (edge.from == a && edge.to == b) || (edge.from == b && edge.to == a);
                            });
                        if (ringEdge == network.edges().end() || ringEdge->controlPoints.size() < 3u) {
                            valid = false;
                            break;
                        }
                    }
                    if (!valid) continue;
                    centerX /= static_cast<float>(cycle.size());
                    centerZ /= static_cast<float>(cycle.size());
                    float minRadius = std::numeric_limits<float>::max(), maxRadius = 0.f;
                    for (const auto id : cycle) {
                        const auto  value = network.nodeResult(id).value();
                        const float dx = value.x - centerX, dz = value.z - centerZ;
                        const float radius = std::sqrt(dx * dx + dz * dz);
                        minRadius          = std::min(minRadius, radius);
                        maxRadius          = std::max(maxRadius, radius);
                    }
                    if (minRadius >= 1.f && maxRadius <= minRadius * 1.8f) roundabouts.push_back(std::move(cycle));
                    continue;
                }
                if (path.size() >= 12u || adjacency[next].size() < 2u || adjacency[next].size() > 3u ||
                    std::find(path.begin(), path.end(), next) != path.end())
                    continue;
                path.push_back(next);
                visit(next);
                path.pop_back();
            }
        };
        visit(node.id);
    }
    std::sort(roundabouts.begin(), roundabouts.end());
    std::unordered_set<std::uint32_t> roundaboutNodes;
    for (const auto& cycle : roundabouts)
        for (const auto id : cycle) roundaboutNodes.insert(id);

    std::vector<EdgeSource> sources;
    sources.reserve(network.edges().size());
    std::size_t required = 0;
    for (const auto& cycle : roundabouts) {
        required += 1u;
        for (const auto id : cycle)
            if (adjacency[id].size() == 3u) ++required;
    }
    for (const auto& node : network.nodes()) {
        if (roundaboutNodes.contains(node.id)) continue;
        const auto arms = static_cast<std::size_t>(
            std::count_if(network.edges().begin(), network.edges().end(),
                          [&](const RoadEdge& edge) { return edge.from == node.id || edge.to == node.id; }));
        if (arms >= 3u) required += arms == 3u ? 2u : 1u;
    }
    for (const auto& edge : network.edges()) {
        auto spline = edgeToSpline(edge);
        if (!spline.ok()) return Result<void>::failure(spline.status());
        const int arcSamples   = edge.style.sideObjectDistribution == RoadSideDistribution::LegacyCentered ? 32 : 512;
        auto      lengthResult = spline.value().lengthResult(arcSamples);
        if (!lengthResult.ok()) return Result<void>::failure(lengthResult.status());
        auto profile = makeRoadProfile(edge.style, edge.lanesForward, edge.lanesBackward);
        if (!profile.ok()) return Result<void>::failure(profile.status());
        std::optional<RoadSurfacePolyline> surface;
        const bool followsSurface = edge.style.sidePlacement.pathSampling == RoadSidePathSampling::SurfaceSegments;
        const auto retained       = frameCache.surfaces.find(edge.id);
        if (followsSurface && retained != frameCache.surfaces.end()) {
            auto prepared = prepareSurfacePolyline(retained->second.frames);
            if (!prepared.ok()) return Result<void>::failure(prepared.status());
            surface               = std::move(prepared).takeValue();
            surface->firstVisible = retained->second.firstVisible;
            surface->lastVisible  = retained->second.lastVisible;
        }
        const float length    = surface ? surface->length : lengthResult.value();
        float       sideStart = edge.style.sideObjectStartOffset;
        float       sideEnd   = length - edge.style.sideObjectEndOffset;
        if (!surface && activeJunctionNodes.contains(edge.from)) {
            const auto node = network.nodeResult(edge.from);
            if (!node.ok()) return Result<void>::failure(node.status());
            sideStart = std::max(sideStart, junctionSocketDistance(network, node.value(), edge, length));
        }
        if (!surface && activeJunctionNodes.contains(edge.to)) {
            const auto node = network.nodeResult(edge.to);
            if (!node.ok()) return Result<void>::failure(node.status());
            sideEnd = std::min(sideEnd, length - junctionSocketDistance(network, node.value(), edge, length));
        }
        if (surface) {
            sideStart = std::max(sideStart, static_cast<float>(surface->distances[surface->firstVisible]));
            sideEnd   = std::min(sideEnd, static_cast<float>(surface->distances[surface->lastVisible]));
        } else if (followsSurface) {
            // Geometry intentionally omitted the whole edge inside its sockets; no visible facilities exist.
            sideStart = sideEnd = 0.f;
        }
        sideEnd                     = std::max(sideStart, sideEnd);
        const float placementLength = sideEnd - sideStart;
        const auto  distribution    = edge.style.sideObjectDistribution;
        const float firstSideDistance =
            distribution == RoadSideDistribution::LegacyCentered ? options.sideObjectSpacing * .5f : 0.f;
        float       sideStep    = options.sideObjectSpacing;
        std::size_t sideSamples = 0;
        if ((edge.style.sideObjectsLeft || edge.style.sideObjectsRight) && placementLength > firstSideDistance) {
            const float ratio = (placementLength - firstSideDistance) / options.sideObjectSpacing;
            if (!std::isfinite(ratio) || ratio > static_cast<float>(options.maximumPlacements))
                return bakeFail<void>(DiagnosticCode::PreconditionViolation,
                                      "road placement distribution exceeds budget", "placements");
            if (distribution == RoadSideDistribution::Average) {
                const float integral = std::floor(ratio), fraction = ratio - integral;
                // Explicit nearest-even rounding is independent of process floating-point rounding mode.
                const float rounded =
                    integral + ((fraction > .5f || (fraction == .5f && std::fmod(integral, 2.f) != 0.f)) ? 1.f : 0.f);
                const auto intervals = static_cast<std::size_t>(std::max(1.f, rounded));
                sideSamples          = intervals + 1;
                sideStep             = placementLength / static_cast<float>(intervals) + .005f;
            } else if (distribution == RoadSideDistribution::LegacyCentered)
                sideSamples = static_cast<std::size_t>(std::floor(ratio)) + 1;
            else
                sideSamples = static_cast<std::size_t>(std::ceil(ratio));
        }
        const auto enabledSides =
            edge.style.sidePlacement.reference == RoadSideReference::Center
                ? static_cast<std::size_t>(edge.style.sideObjectsLeft || edge.style.sideObjectsRight)
                : static_cast<std::size_t>(edge.style.sideObjectsLeft) +
                      static_cast<std::size_t>(edge.style.sideObjectsRight);
        required += 2u + sideSamples * enabledSides;
        if (required > static_cast<std::size_t>(options.maximumPlacements))
            return bakeFail<void>(DiagnosticCode::PreconditionViolation, "road placement budget exceeded",
                                  "placements");
        sources.push_back({&edge, std::move(spline).takeValue(), length, profile.value().halfWidth, sideStart, sideEnd,
                           sideSamples, sideStart + firstSideDistance, sideStep, arcSamples, std::move(surface)});
    }
    placements.reserve(required);

    for (const auto& cycle : roundabouts) {
        float centerX = 0.f, centerY = 0.f, centerZ = 0.f;
        for (const auto id : cycle) {
            const auto node = network.nodeResult(id).value();
            centerX += node.x;
            centerY += node.y;
            centerZ += node.z;
        }
        const float divisor = static_cast<float>(cycle.size());
        centerX /= divisor;
        centerY /= divisor;
        centerZ /= divisor;
        const std::uint32_t roundaboutId = cycle.front();
        const auto          entryCount   = static_cast<std::int64_t>(
            std::count_if(cycle.begin(), cycle.end(), [&](std::uint32_t id) { return adjacency[id].size() == 3u; }));
        const auto pointNamespace        = 0x524e444200000000ull | roundaboutId;
        auto       appendRoundaboutPoint = [&](const RoadNode& node, V3 heading, const char* role,
                                         std::uint64_t ordinal) -> Result<void> {
            const int row = placements.add(node.x, node.y, node.z);
            placements.setNormal(row, 0.f, 1.f, 0.f);
            placements.setYaw(row, std::atan2(heading.x, heading.z) * 57.2957795f);
            placements.setPointSeed(row, deriveSeed(roundaboutId, std::string(role) + std::to_string(node.id)));
            auto status = placements.trySetPointId(row, derivePointId(pointNamespace, ordinal));
            if (!status.ok()) return status;
            status = placements.trySetStringAttribute(row, "road_role", role);
            if (!status.ok()) return status;
            status = placements.trySetIntAttribute(row, "road_node_id", node.id);
            if (!status.ok()) return status;
            status = placements.trySetIntAttribute(row, "road_roundabout_id", roundaboutId);
            if (!status.ok()) return status;
            status = placements.trySetIntAttribute(row, "road_arm_count", entryCount);
            if (!status.ok()) return status;
            return placements.trySetVectorAttribute(row, "road_direction", heading.x, heading.y, heading.z);
        };

        RoadNode center;
        center.x    = centerX;
        center.y    = centerY;
        center.z    = centerZ;
        auto status = appendRoundaboutPoint(center, V3{0.f, 0.f, 1.f}, "junction.roundabout.center", 1u);
        if (!status.ok()) return status;
        std::uint64_t entryOrdinal = 2u;
        for (std::size_t i = 0; i < cycle.size(); ++i) {
            const auto node = network.nodeResult(cycle[i]).value();
            if (adjacency[node.id].size() != 3u) continue;
            V3 heading{node.x - centerX, 0.f, node.z - centerZ};
            for (const auto neighbor : adjacency[node.id]) {
                if (std::find(cycle.begin(), cycle.end(), neighbor) != cycle.end()) continue;
                const auto outside = network.nodeResult(neighbor).value();
                heading            = V3{outside.x - node.x, 0.f, outside.z - node.z};
                break;
            }
            status = appendRoundaboutPoint(node, normalize(heading), "junction.roundabout.entry", entryOrdinal++);
            if (!status.ok()) return status;
        }
    }

    for (const auto& node : network.nodes()) {
        if (roundaboutNodes.contains(node.id)) continue;
        std::vector<V3> directions;
        for (const auto& edge : network.edges()) {
            auto appendDirection = [&](const RoadControlPoint& point) {
                const V3 direction{point.x - node.x, 0.f, point.z - node.z};
                if (dot(direction, direction) <= 1e-6f) return false;
                directions.push_back(normalize(direction));
                return true;
            };
            if (edge.from == node.id) {
                for (std::size_t i = 1u; i < edge.controlPoints.size(); ++i)
                    if (appendDirection(edge.controlPoints[i])) break;
            } else if (edge.to == node.id) {
                for (std::size_t i = edge.controlPoints.size() - 1u; i-- > 0u;)
                    if (appendDirection(edge.controlPoints[i])) break;
            }
        }
        if (directions.size() < 3u) continue;

        int oppositePairs = 0;
        for (std::size_t a = 0; a < directions.size(); ++a)
            for (std::size_t b = a + 1u; b < directions.size(); ++b)
                if (dot(directions[a], directions[b]) < -0.85f) ++oppositePairs;

        const char* role = "junction.multi";
        if (directions.size() == 3u) role = oppositePairs > 0 ? "junction.t" : "junction.y";
        if (directions.size() == 4u) role = oppositePairs >= 2 ? "junction.x" : "junction.multi";

        V3 heading{};
        if (directions.size() == 3u && oppositePairs > 0) {
            float leastOpposed = -2.f;
            for (const auto& candidate : directions) {
                float closest = 1.f;
                for (const auto& other : directions) {
                    if (&candidate != &other) closest = std::min(closest, dot(candidate, other));
                }
                if (closest > leastOpposed) {
                    leastOpposed = closest;
                    heading      = candidate;
                }
            }
        } else {
            heading = directions.front();
        }

        const int row = placements.add(node.x, node.y, node.z);
        placements.setNormal(row, 0.f, 1.f, 0.f);
        placements.setYaw(row, std::atan2(heading.x, heading.z) * 57.2957795f);
        placements.setPointSeed(row, deriveSeed(node.id, role));
        auto status = placements.trySetPointId(row, derivePointId(0x4a554e4300000000ull | node.id, 1u));
        if (!status.ok()) return status;
        status = placements.trySetStringAttribute(row, "road_role", role);
        if (!status.ok()) return status;
        status = placements.trySetIntAttribute(row, "road_node_id", node.id);
        if (!status.ok()) return status;
        status = placements.trySetIntAttribute(row, "road_arm_count", static_cast<std::int64_t>(directions.size()));
        if (!status.ok()) return status;
        status = placements.trySetIntAttribute(row, "road_side", 0);
        if (!status.ok()) return status;
        status = placements.trySetVectorAttribute(row, "road_direction", heading.x, heading.y, heading.z);
        if (!status.ok()) return status;

        if (directions.size() == 3u) {
            const float islandDistance = std::max(1.f, node.junctionRadius * 0.45f);
            const int   islandRow =
                placements.add(node.x + heading.x * islandDistance, node.y, node.z + heading.z * islandDistance);
            placements.setNormal(islandRow, 0.f, 1.f, 0.f);
            placements.setYaw(islandRow, std::atan2(heading.x, heading.z) * 57.2957795f);
            constexpr const char* islandRole = "junction.channelizing.island";
            placements.setPointSeed(islandRow, deriveSeed(node.id, islandRole));
            status = placements.trySetPointId(islandRow, derivePointId(0x4a554e4300000000ull | node.id, 2u));
            if (!status.ok()) return status;
            status = placements.trySetStringAttribute(islandRow, "road_role", islandRole);
            if (!status.ok()) return status;
            status = placements.trySetIntAttribute(islandRow, "road_node_id", node.id);
            if (!status.ok()) return status;
            status = placements.trySetIntAttribute(islandRow, "road_arm_count", 3);
            if (!status.ok()) return status;
            status = placements.trySetFloatAttribute(islandRow, "road_distance", islandDistance);
            if (!status.ok()) return status;
            status = placements.trySetVectorAttribute(islandRow, "road_direction", heading.x, heading.y, heading.z);
            if (!status.ok()) return status;
        }
    }

    std::vector<V3> acceptedSidePoints;
    for (const auto& source : sources) {
        std::uint64_t ordinal        = 1;
        const auto    pointNamespace = 0x524f414400000000ull | source.edge->id;
        auto          append         = [&](float distance, float lateral, const char* role, int side,
                          bool avoidConflicts) -> Result<bool> {
            auto frame = source.surface ? sampleSurfacePolyline(*source.surface, distance)
                                                         : source.spline.travelFrameResult(distance, "clamp", source.arcSamples);
            if (!frame.ok()) return Result<bool>::failure(frame.status());
            auto f = std::move(frame).takeValue();
            if (!source.surface && distance > source.length) {
                const float extension = distance - source.length;
                f.sample.x += f.forwardX * extension;
                f.sample.y += f.forwardY * extension;
                f.sample.z += f.forwardZ * extension;
            }
            const auto& terrainRule = terrainRules.at(source.edge->id);
            const bool  hasAsset    = side != 0 && terrainRule.asset.has_value();
            const float placementYaw =
                std::atan2(hasAsset ? -f.forwardX : f.forwardX, hasAsset ? -f.forwardZ : f.forwardZ);
            const float profileSide = lateral;
            lateral = scaledProfileSide(lateral, asphaltHalfWidth(*source.edge), f.sample.scaleX);
            if (side != 0) lateral += source.edge->style.sidePlacement.lateralOffset;
            V3 point{f.sample.x + f.sideX * lateral, f.sample.y + f.sideY * lateral, f.sample.z + f.sideZ * lateral};
            if (source.surface) {
                auto position =
                    sampleSurfaceProfilePoint(*source.surface, distance, profileSide, asphaltHalfWidth(*source.edge));
                if (!position.ok()) return Result<bool>::failure(position.status());
                point = position.value();
                if (side != 0)
                    point = point + V3{f.sideX, f.sideY, f.sideZ} * source.edge->style.sidePlacement.lateralOffset;
            }
            ProcgenPoint pose;
            pose.yaw     = placementYaw * 57.2957795f;
            pose.normalX = f.upX;
            pose.normalY = f.upY;
            pose.normalZ = f.upZ;
            if (hasAsset || (side != 0 && terrainRule.mode != RoadSideTerrainMode::None)) {
                pose.normalX = pose.normalZ = 0.f;
                pose.normalY                = 1.f;
            }
            if (side != 0 && source.edge->style.sideTerrain.alignment != RoadSideAlignment::None) {
                V3         forward = normalize(V3{f.forwardX, f.forwardY, f.forwardZ});
                V3         up{f.upX, f.upY, f.upZ};
                const auto alignment = source.edge->style.sideTerrain.alignment;
                if (alignment == RoadSideAlignment::TerrainNormal) {
                    auto normal = sideTerrainNormal(*terrainQuery, point.x, point.z, options, terrainWork);
                    if (!normal.ok()) return Result<bool>::failure(normal.status());
                    up      = normal.value();
                    forward = forward - up * dot(forward, up);
                    if (length(forward) < 1e-6f) {
                        const V3 axis = std::fabs(up.x) < .9f ? V3{1, 0, 0} : V3{0, 0, 1};
                        forward       = cross(axis, up);
                    }
                    forward = normalize(forward);
                } else if (alignment == RoadSideAlignment::PathForward) {
                    up = V3{0, 1, 0} - forward * forward.y;
                    if (length(up) < 1e-6f) up = V3{f.upX, f.upY, f.upZ};
                }
                up = normalize(up - forward * dot(up, forward));
                if (hasAsset) forward = forward * -1.f;  // Canonical admitted assets face local -Z.
                const float pitch = -std::asin(std::clamp(forward.y, -1.f, 1.f));
                float       yaw = std::atan2(forward.x, forward.z), roll = 0.f;
                if (std::fabs(std::cos(pitch)) < 1e-6f) {
                    const float sign = forward.y > 0.f ? -1.f : 1.f;
                    yaw              = std::atan2(up.x * sign, up.z * sign);
                } else {
                    const V3 right0{std::cos(yaw), 0, -std::sin(yaw)};
                    const V3 up0{std::sin(yaw) * std::sin(pitch), std::cos(pitch), std::cos(yaw) * std::sin(pitch)};
                    roll = std::atan2(-dot(up, right0), dot(up, up0));
                }
                pose.pitch   = pitch * 57.2957795f;
                pose.yaw     = yaw * 57.2957795f;
                pose.roll    = roll * 57.2957795f;
                pose.normalX = up.x;
                pose.normalY = up.y;
                pose.normalZ = up.z;
            }
            if (side != 0) pose.yaw += source.edge->style.sidePlacement.yawDegrees;
            if (side != 0 && source.edge->style.sideTerrain.mode != RoadSideTerrainMode::None) {
                auto height =
                    sideTerrainHeight(terrainRule, *terrainQuery, point.x, point.z, pose,
                                      hasAsset ? &edgeAssetBounds.at(source.edge->id) : nullptr, options, terrainWork);
                if (!height.ok()) return Result<bool>::failure(height.status());
                point.y = height.value();
            }
            if (side != 0) point.y += source.edge->style.sidePlacement.verticalOffset;
            if (avoidConflicts) {
                for (const auto& other : sources) {
                    if (other.edge->id == source.edge->id) continue;
                    auto closest = other.surface ? closestSurfacePolyline(*other.surface, point.x, point.y, point.z)
                                                                  : other.spline.closestPointResult(point.x, point.y, point.z, 32);
                    if (!closest.ok()) return Result<bool>::failure(closest.status());
                    const V3 delta{point.x - closest.value().x, point.y - closest.value().y,
                                   point.z - closest.value().z};
                    if (length(delta) <
                        scaledProfileSide(other.halfWidth, asphaltHalfWidth(*other.edge), closest.value().scaleX) +
                            options.sideObjectClearance)
                        return Result<bool>::success(false);
                }
                for (const auto& accepted : acceptedSidePoints)
                    if (length(point - accepted) < options.sideObjectClearance) return Result<bool>::success(false);
            }
            const int row = placements.add(point.x, point.y, point.z);
            if (hasAsset) {
                const auto& bounds = edgeAssetBounds.at(source.edge->id);
                placements.setBounds(row, bounds[0], bounds[1], bounds[2], bounds[3], bounds[4], bounds[5]);
                auto asset = placements.trySetStringAttribute(row, "asset", terrainRule.asset->asset.format());
                if (!asset.ok()) return Result<bool>::failure(asset.status());
                auto build =
                    placements.trySetStringAttribute(row, "asset_build_id", terrainRule.asset->buildId.format());
                if (!build.ok()) return Result<bool>::failure(build.status());
                auto rootPose = placements.trySetStringAttribute(
                    row, "asset_root_pose",
                    terrainRule.asset->rootPose == eve::PrefabRootPose::Authored ? "authored" : "placement");
                if (!rootPose.ok()) return Result<bool>::failure(rootPose.status());
            }
            placements.setRotation(row, pose.pitch, pose.yaw, pose.roll);
            placements.setNormal(row, pose.normalX, pose.normalY, pose.normalZ);
            placements.setPointSeed(row, deriveSeed(source.edge->id, std::string(role) + std::to_string(ordinal)));
            auto identified = placements.trySetPointId(row, derivePointId(pointNamespace, ordinal++));
            if (!identified.ok()) return Result<bool>::failure(identified.status());
            auto edgeId = placements.trySetIntAttribute(row, "road_edge_id", source.edge->id);
            if (!edgeId.ok()) return Result<bool>::failure(edgeId.status());
            auto pointRole = placements.trySetStringAttribute(row, "road_role", role);
            if (!pointRole.ok()) return Result<bool>::failure(pointRole.status());
            auto pointSide = placements.trySetIntAttribute(row, "road_side", side);
            if (!pointSide.ok()) return Result<bool>::failure(pointSide.status());
            auto along = placements.trySetFloatAttribute(row, "road_distance", distance);
            if (!along.ok()) return Result<bool>::failure(along.status());
            auto clearance = placements.trySetFloatAttribute(row, "road_clearance", options.sideObjectClearance);
            if (!clearance.ok()) return Result<bool>::failure(clearance.status());
            auto direction =
                placements.trySetVectorAttribute(row, "road_direction", f.forwardX, f.forwardY, f.forwardZ);
            if (!direction.ok()) return Result<bool>::failure(direction.status());
            if (avoidConflicts) acceptedSidePoints.push_back(point);
            return Result<bool>::success(true);
        };

        auto start = append(0.f, 0.f, "transition.start", 0, false);
        if (!start.ok()) return Result<void>::failure(start.status());
        auto end = append(source.length, 0.f, "transition.end", 0, false);
        if (!end.ok()) return Result<void>::failure(end.status());
        const auto  reference      = source.edge->style.sidePlacement.reference;
        const float lateral        = reference == RoadSideReference::Center ? 0.f
                                     : reference == RoadSideReference::AsphaltEdges
                                         ? asphaltHalfWidth(*source.edge)
                                         : source.halfWidth + options.sideObjectOffset;
        float       legacyDistance = source.sideFirst;
        for (std::size_t index = 0; index < source.sideCount; ++index) {
            const bool  legacy = source.edge->style.sideObjectDistribution == RoadSideDistribution::LegacyCentered;
            const float distance =
                legacy ? legacyDistance : source.sideFirst + static_cast<float>(index) * source.sideStep;
            if (legacy) {
                // Preserve historical cumulative float additions and its open end, including its budget upper bound.
                if (!(distance < source.sideEnd)) break;
                legacyDistance += options.sideObjectSpacing;
            }
            // Author exclusions and junction sockets remain hard constraints; only a free road end extends.
            if (source.sideEnd < source.length && distance > source.sideEnd) continue;
            if (source.edge->style.sideObjectsLeft) {
                auto left = append(distance, -lateral, "side.left", -1, true);
                if (!left.ok()) return Result<void>::failure(left.status());
            }
            if (source.edge->style.sideObjectsRight &&
                !(reference == RoadSideReference::Center && source.edge->style.sideObjectsLeft)) {
                auto right = append(distance, lateral, "side.right", 1, true);
                if (!right.ok()) return Result<void>::failure(right.status());
            }
        }
    }
    return detail::bakeTrafficControlPoints(placements, network, options);
}

}  // namespace eve::procgen::road::detail
