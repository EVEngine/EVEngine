#include "building/PlacementSystem.h"

#include "building/HeightfieldSurface.h"
#include "building/StaticMeshSurface.h"
#include "building/BuildingDef.h"
#include "building/Ghost.h"
#include "building/PlacementWorld.h"
#include "common/Diagnostic.h"
#include "grid/GridProjection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace eve::building {

namespace {

float wrapDeg(float deg) {
    deg = std::fmod(deg, 360.f);
    if (deg < 0.f) deg += 360.f;
    return deg;
}

int cardinalQuarter(float rotationDeg) {
    const float d = wrapDeg(rotationDeg);
    int q = int(std::lround(d / 90.f)) % 4;
    if (q < 0) q += 4;
    return q;
}

float length3(float x, float y, float z) { return std::sqrt(x * x + y * y + z * z); }

struct FreeFootprint {
    struct Point {
        float x = 0.f;
        float y = 0.f;
    };
    float x = 0.f;
    float y = 0.f;
    float radius = 0.f;
    float halfWidth = 0.f;
    float halfHeight = 0.f;
    float rotationRadians = 0.f;
    std::vector<Point> vertices;

    bool isPolygon() const { return vertices.size() >= 3; }
};

FreeFootprint definitionFreeFootprint(const BuildingDefinition &definition,
                                      const PlacementWorld &world, float x, float y,
                                      float rotationDeg) {
    FreeFootprint result;
    result.x = x;
    result.y = y;
    result.radius = definition.freeRadiusCells *
                    std::min(world.getGrid().cellW, world.getGrid().cellH);
    if (!definition.freeFootprintVertices.empty()) {
        for (size_t i = 0; i < definition.freeFootprintVertices.size(); i += 2) {
            result.vertices.push_back({definition.freeFootprintVertices[i] * world.getGrid().cellW,
                                       definition.freeFootprintVertices[i + 1] *
                                           world.getGrid().cellH});
            result.halfWidth = std::max(result.halfWidth, std::fabs(result.vertices.back().x));
            result.halfHeight = std::max(result.halfHeight, std::fabs(result.vertices.back().y));
        }
    } else if (definition.freeFootprintWidthCells > 0.f &&
        definition.freeFootprintHeightCells > 0.f) {
        result.halfWidth = definition.freeFootprintWidthCells * world.getGrid().cellW * 0.5f;
        result.halfHeight = definition.freeFootprintHeightCells * world.getGrid().cellH * 0.5f;
        result.vertices = {{-result.halfWidth, -result.halfHeight},
                           {result.halfWidth, -result.halfHeight},
                           {result.halfWidth, result.halfHeight},
                           {-result.halfWidth, result.halfHeight}};
    }
    result.rotationRadians = wrapDeg(rotationDeg) * 3.14159265358979323846f / 180.f;
    return result;
}

void applyDefinitionFreeFootprint(PlacedBuilding &placed, const BuildingDefinition &definition,
                                  const PlacementWorld &world) {
    const FreeFootprint footprint =
        definitionFreeFootprint(definition, world, placed.worldX, placed.worldY, 0.f);
    placed.freeRadius = footprint.radius;
    placed.freeHalfWidth = footprint.halfWidth;
    placed.freeHalfHeight = footprint.halfHeight;
    placed.freeFootprintVertices.clear();
    if (!definition.freeFootprintVertices.empty()) {
        placed.freeFootprintVertices.reserve(footprint.vertices.size() * 2);
        for (const auto &point : footprint.vertices) {
            placed.freeFootprintVertices.push_back(point.x);
            placed.freeFootprintVertices.push_back(point.y);
        }
    }
}

FreeFootprint placedFreeFootprint(const PlacedBuilding &placed) {
    FreeFootprint result;
    result.x = placed.worldX;
    result.y = placed.worldY;
    result.radius = placed.freeRadius;
    result.halfWidth = placed.freeHalfWidth;
    result.halfHeight = placed.freeHalfHeight;
    result.rotationRadians = wrapDeg(placed.rotationDeg) * 3.14159265358979323846f / 180.f;
    for (size_t i = 0; i + 1 < placed.freeFootprintVertices.size(); i += 2)
        result.vertices.push_back(
            {placed.freeFootprintVertices[i], placed.freeFootprintVertices[i + 1]});
    if (result.vertices.empty() && result.halfWidth > 0.f && result.halfHeight > 0.f)
        result.vertices = {{-result.halfWidth, -result.halfHeight},
                           {result.halfWidth, -result.halfHeight},
                           {result.halfWidth, result.halfHeight},
                           {-result.halfWidth, result.halfHeight}};
    return result;
}

std::vector<FreeFootprint::Point> worldVertices(const FreeFootprint &footprint) {
    std::vector<FreeFootprint::Point> result;
    result.reserve(footprint.vertices.size());
    const float c = std::cos(footprint.rotationRadians);
    const float s = std::sin(footprint.rotationRadians);
    for (const auto &point : footprint.vertices)
        result.push_back({footprint.x + point.x * c - point.y * s,
                          footprint.y + point.x * s + point.y * c});
    return result;
}

bool pointInPolygon(const FreeFootprint &polygon, float x, float y) {
    const auto vertices = worldVertices(polygon);
    float winding = 0.f;
    for (size_t i = 0; i < vertices.size(); ++i) {
        const auto &a = vertices[i];
        const auto &b = vertices[(i + 1) % vertices.size()];
        const float cross = (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
        if (std::fabs(cross) <= 1e-5f) continue;
        if (winding == 0.f)
            winding = cross;
        else if (cross * winding < 0.f)
            return false;
    }
    return true;
}

bool freeFootprintsOverlap(const FreeFootprint &a, const FreeFootprint &b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    if (!a.isPolygon() && !b.isPolygon()) return std::hypot(dx, dy) < a.radius + b.radius;

    auto circlePolygonOverlap = [](const FreeFootprint &circle, const FreeFootprint &polygon) {
        if (pointInPolygon(polygon, circle.x, circle.y)) return true;
        const auto vertices = worldVertices(polygon);
        const float radiusSquared = circle.radius * circle.radius;
        for (size_t i = 0; i < vertices.size(); ++i) {
            const auto &a = vertices[i];
            const auto &b = vertices[(i + 1) % vertices.size()];
            const float edgeX = b.x - a.x;
            const float edgeY = b.y - a.y;
            const float lengthSquared = edgeX * edgeX + edgeY * edgeY;
            const float t = lengthSquared > 0.f
                                ? std::clamp(((circle.x - a.x) * edgeX +
                                              (circle.y - a.y) * edgeY) /
                                                 lengthSquared,
                                             0.f, 1.f)
                                : 0.f;
            const float closestX = a.x + edgeX * t;
            const float closestY = a.y + edgeY * t;
            const float distanceX = circle.x - closestX;
            const float distanceY = circle.y - closestY;
            if (distanceX * distanceX + distanceY * distanceY < radiusSquared) return true;
        }
        return false;
    };
    if (!a.isPolygon()) return circlePolygonOverlap(a, b);
    if (!b.isPolygon()) return circlePolygonOverlap(b, a);

    const auto aVertices = worldVertices(a);
    const auto bVertices = worldVertices(b);
    for (const auto *vertices : {&aVertices, &bVertices}) {
        for (size_t i = 0; i < vertices->size(); ++i) {
            const auto &p = vertices->at(i);
            const auto &q = vertices->at((i + 1) % vertices->size());
            const float axisX = -(q.y - p.y);
            const float axisY = q.x - p.x;
            float aMin = std::numeric_limits<float>::max();
            float aMax = std::numeric_limits<float>::lowest();
            float bMin = std::numeric_limits<float>::max();
            float bMax = std::numeric_limits<float>::lowest();
            for (const auto &vertex : aVertices) {
                const float projection = vertex.x * axisX + vertex.y * axisY;
                aMin = std::min(aMin, projection);
                aMax = std::max(aMax, projection);
            }
            for (const auto &vertex : bVertices) {
                const float projection = vertex.x * axisX + vertex.y * axisY;
                bMin = std::min(bMin, projection);
                bMax = std::max(bMax, projection);
            }
            if (aMax <= bMin || bMax <= aMin) return false;
        }
    }
    return true;
}

bool freeFootprintContainsInternal(const FreeFootprint &footprint, float x, float y) {
    const float dx = x - footprint.x;
    const float dy = y - footprint.y;
    if (!footprint.isPolygon()) return std::hypot(dx, dy) <= footprint.radius;
    return pointInPolygon(footprint, x, y);
}

void cross3(float ax, float ay, float az, float bx, float by, float bz, float &x, float &y,
            float &z) {
    x = ay * bz - az * by;
    y = az * bx - ax * bz;
    z = ax * by - ay * bx;
}

bool normalizeSurfaceFrame(PlacementSystem::PlacementHit &hit) {
    if (!std::isfinite(hit.worldX) || !std::isfinite(hit.worldY) ||
        !std::isfinite(hit.worldZ) || !std::isfinite(hit.normalX) ||
        !std::isfinite(hit.normalY) || !std::isfinite(hit.normalZ)) {
        return false;
    }
    const float normalLength = length3(hit.normalX, hit.normalY, hit.normalZ);
    if (normalLength <= 1e-5f) return false;
    hit.normalX /= normalLength;
    hit.normalY /= normalLength;
    hit.normalZ /= normalLength;

    const float tangentDot = hit.tangentX * hit.normalX + hit.tangentY * hit.normalY +
                             hit.tangentZ * hit.normalZ;
    hit.tangentX -= tangentDot * hit.normalX;
    hit.tangentY -= tangentDot * hit.normalY;
    hit.tangentZ -= tangentDot * hit.normalZ;
    float tangentLength = length3(hit.tangentX, hit.tangentY, hit.tangentZ);
    if (tangentLength <= 1e-5f) {
        if (std::fabs(hit.normalY) < 0.9f) {
            hit.tangentX = 0.f;
            hit.tangentY = 1.f;
            hit.tangentZ = 0.f;
        } else {
            hit.tangentX = 1.f;
            hit.tangentY = 0.f;
            hit.tangentZ = 0.f;
        }
        const float fallbackDot = hit.tangentX * hit.normalX + hit.tangentY * hit.normalY +
                                  hit.tangentZ * hit.normalZ;
        hit.tangentX -= fallbackDot * hit.normalX;
        hit.tangentY -= fallbackDot * hit.normalY;
        hit.tangentZ -= fallbackDot * hit.normalZ;
        tangentLength = length3(hit.tangentX, hit.tangentY, hit.tangentZ);
    }
    hit.tangentX /= tangentLength;
    hit.tangentY /= tangentLength;
    hit.tangentZ /= tangentLength;
    cross3(hit.tangentX, hit.tangentY, hit.tangentZ, hit.normalX, hit.normalY,
           hit.normalZ, hit.bitangentX, hit.bitangentY, hit.bitangentZ);
    return true;
}

}  // namespace

PlacementSystem &PlacementSystem::inst() {
    static PlacementSystem instance;
    return instance;
}

std::unordered_map<std::string, PlacementSystem::ValidateFn> &PlacementSystem::validateRules() {
    return inst().validateRules_;
}

std::unordered_map<std::string, PlacementSystem::SnapFn> &PlacementSystem::snapRules() {
    return inst().snapRules_;
}

std::unordered_map<std::string, PlacementSystem::ChangeHook> &PlacementSystem::changeHooks() {
    return inst().changeHooks_;
}

std::unordered_map<std::string, PlacementSystem::SurfaceFn> &PlacementSystem::surfaces() {
    return inst().surfaces_;
}

std::unordered_map<std::string, PlacementSystem::SurfaceProviderFn> &
PlacementSystem::surfaceProviders() {
    return inst().surfaceProviders_;
}

std::vector<BuildingChangeEvent> &PlacementSystem::eventQueue() {
    return inst().eventQueue_;
}

int &PlacementSystem::instanceCounter() {
    return inst().instanceCounter_;
}

bool &PlacementSystem::builtinsReady() {
    return inst().builtinsReady_;
}

void PlacementSystem::registerValidateRule(const std::string &name, ValidateFn fn) {
    if (name.empty() || !fn) return;
    validateRules()[name] = std::move(fn);
}

void PlacementSystem::unregisterValidateRule(const std::string &name) {
    validateRules().erase(name);
}

bool PlacementSystem::hasValidateRule(const std::string &name) {
    ensureBuiltins();
    return validateRules().count(name) > 0;
}

void PlacementSystem::registerSnapRule(const std::string &name, SnapFn fn) {
    if (name.empty() || !fn) return;
    snapRules()[name] = std::move(fn);
}

void PlacementSystem::unregisterSnapRule(const std::string &name) { snapRules().erase(name); }

bool PlacementSystem::hasSnapRule(const std::string &name) {
    ensureBuiltins();
    return snapRules().count(name) > 0;
}

void PlacementSystem::registerChangeHook(const std::string &name, ChangeHook fn) {
    if (name.empty() || !fn) return;
    changeHooks()[name] = std::move(fn);
}

void PlacementSystem::unregisterChangeHook(const std::string &name) { changeHooks().erase(name); }

bool PlacementSystem::hasChangeHook(const std::string &name) {
    return changeHooks().count(name) > 0;
}

void PlacementSystem::registerSurface(const std::string &name, SurfaceFn fn) {
    if (name.empty() || !fn) return;
    surfaces()[name] = fn;
    registerSurfaceProvider(
        name, [name, fn = std::move(fn)](const PlacementWorld &world, float x, float y) {
            PlacementHit hit;
            if (!fn(world, x, y, &hit)) {
                return eve::Result<PlacementHit>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::NotFound, "placement surface did not produce a hit",
                    name, {}, "building.surface"));
            }
            return eve::Result<PlacementHit>::success(std::move(hit));
        });
}

void PlacementSystem::registerSurfaceProvider(const std::string &name, SurfaceProviderFn fn) {
    if (name.empty() || !fn) return;
    surfaceProviders()[name] = std::move(fn);
}

eve::Result<void> PlacementSystem::registerHeightfieldSurface(
    const std::string &name, std::shared_ptr<const HeightfieldSurface> surface) {
    if (name.empty() || !surface) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "heightfield registration requires a name and an owning surface", name, {},
            "building.heightfield-surface"));
    }
    registerSurfaceProvider(
        name, [surface = std::move(surface)](const PlacementWorld &world, float x, float y) {
            return surface->sample(world, x, y);
        });
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> PlacementSystem::registerStaticMeshSurface(
    const std::string &name, std::shared_ptr<const StaticMeshSurface> surface) {
    if (name.empty() || !surface) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "static mesh registration requires a name and an owning surface", name, {},
            "building.static-mesh-surface"));
    }
    registerSurfaceProvider(
        name, [surface = std::move(surface)](const PlacementWorld &world, float x, float y) {
            return surface->sample(world, x, y);
        });
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void PlacementSystem::unregisterSurface(const std::string &name) {
    surfaces().erase(name);
    surfaceProviders().erase(name);
}

bool PlacementSystem::hasSurface(const std::string &name) {
    ensureBuiltins();
    return surfaceProviders().count(name) > 0;
}

bool PlacementSystem::surfaceHit(const PlacementWorld &world, const std::string &name, float x,
                                 float y, PlacementHit *hit) {
    if (!hit) return false;
    auto result = sampleSurface(world, name, x, y);
    if (!result.ok()) return false;
    *hit = std::move(result).takeValue();
    return true;
}

eve::Result<PlacementSystem::PlacementHit>
PlacementSystem::sampleSurface(const PlacementWorld &world, const std::string &name, float x,
                               float y) {
    ensureBuiltins();
    const std::string resolvedName = name.empty() ? "plane" : name;
    auto it = surfaceProviders().find(resolvedName);
    if (it == surfaceProviders().end() || !it->second) {
        return eve::Result<PlacementHit>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "placement surface provider is not registered",
            resolvedName, {}, "building.surface"));
    }
    auto result = it->second(world, x, y);
    if (!result.ok()) return eve::Result<PlacementHit>::failure(result.status());
    PlacementHit hit = std::move(result).takeValue();
    if (!normalizeSurfaceFrame(hit)) {
        return eve::Result<PlacementHit>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "placement surface returned a non-finite position or zero normal", resolvedName,
            {}, "building.surface"));
    }
    if (hit.surfaceId.empty()) hit.surfaceId = resolvedName;
    return eve::Result<PlacementHit>::success(std::move(hit));
}

eve::Result<PlacementSystem::SurfacePatch>
PlacementSystem::sampleSurfacePatch(const PlacementWorld &world, const std::string &buildingId,
                                    const std::string &surfaceName, float x, float y,
                                    float rotationDegrees) {
    const BuildingDefinition *def = BuildingRegistry::find(buildingId);
    if (!def) {
        return eve::Result<SurfacePatch>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "building definition is not registered", buildingId,
            {}, "building.surface-patch"));
    }

    const SnapResult snapped = snap(world, buildingId, x, y);
    auto anchorResult = sampleSurface(world, surfaceName, snapped.worldX, snapped.worldY);
    if (!anchorResult.ok()) return eve::Result<SurfacePatch>::failure(anchorResult.status());

    SurfacePatch patch;
    patch.anchor = std::move(anchorResult).takeValue();
    float originX = 0.f;
    float originY = 0.f;
    world.cellToWorldPlane(snapped.cellX, snapped.cellY, originX, originY);

    float minHeight = std::numeric_limits<float>::max();
    float maxHeight = std::numeric_limits<float>::lowest();
    const auto slopeDegrees = [&](const PlacementHit &hit) {
        const float upDot = world.getGrid().plane == grid::GridPlane::XZ ? hit.normalY
                                                                         : hit.normalZ;
        return std::acos(std::clamp(upDot, -1.f, 1.f)) * 180.f / 3.14159265358979323846f;
    };
    patch.maxSlopeDegrees = 0.f;
    bool failed = false;
    eve::Status failureStatus;
    const auto appendSample = [&](float sampleX, float sampleY, int cellX, int cellY,
                                  bool anchor) {
        if (failed) return false;
        PlacementHit hit;
        if (anchor) {
            hit = patch.anchor;
        } else {
            auto sample = sampleSurface(world, surfaceName, sampleX, sampleY);
            if (!sample.ok()) {
                failed = true;
                failureStatus = sample.status();
                return false;
            }
            hit = std::move(sample).takeValue();
        }
        if (hit.surfaceId != patch.anchor.surfaceId ||
            hit.surfaceRevision != patch.anchor.surfaceRevision) {
            failed = true;
            failureStatus = eve::Status::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict,
                "footprint crosses a surface identity or revision boundary", hit.surfaceId,
                {}, "building.surface-patch"));
            return false;
        }
        const float height =
            world.getGrid().plane == grid::GridPlane::XZ ? hit.worldY : hit.worldZ;
        minHeight = std::min(minHeight, height);
        maxHeight = std::max(maxHeight, height);
        patch.maxSlopeDegrees = std::max(patch.maxSlopeDegrees, slopeDegrees(hit));
        patch.samples.push_back({cellX, cellY, std::move(hit)});
        return true;
    };
    if (def->placementKind == "free" &&
        (!def->freeFootprintVertices.empty() ||
         (def->freeFootprintWidthCells > 0.f && def->freeFootprintHeightCells > 0.f))) {
        appendSample(snapped.worldX, snapped.worldY, snapped.cellX, snapped.cellY, true);
        const FreeFootprint footprint = definitionFreeFootprint(
            *def, world, snapped.worldX, snapped.worldY,
            normalizeRotation(buildingId, rotationDegrees));
        for (const auto &point : worldVertices(footprint)) {
            int cellX = 0;
            int cellY = 0;
            grid::worldToCell(world.getGrid(), point.x, point.y, cellX, cellY,
                              world.getWidth(), world.getHeight());
            if (!appendSample(point.x, point.y, cellX, cellY, false)) break;
        }
    } else {
        foreachFootprintCell(
            *def, snapped.cellX, snapped.cellY,
            normalizeRotation(buildingId, rotationDegrees), [&](int cellX, int cellY) {
                float cellWorldX = 0.f;
                float cellWorldY = 0.f;
                world.cellToWorldPlane(cellX, cellY, cellWorldX, cellWorldY);
                return appendSample(snapped.worldX + cellWorldX - originX,
                                    snapped.worldY + cellWorldY - originY, cellX, cellY,
                                    cellX == snapped.cellX && cellY == snapped.cellY);
            });
    }
    if (failed) return eve::Result<SurfacePatch>::failure(std::move(failureStatus));
    if (patch.samples.empty()) {
        return eve::Result<SurfacePatch>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "building footprint has no occupied cells",
            buildingId, {}, "building.surface-patch"));
    }
    patch.heightDelta = maxHeight - minHeight;
    return eve::Result<SurfacePatch>::success(std::move(patch));
}

std::vector<std::string> PlacementSystem::surfaceNames() {
    ensureBuiltins();
    std::vector<std::string> names;
    names.reserve(surfaceProviders().size());
    for (const auto &kv : surfaceProviders()) names.push_back(kv.first);
    return names;
}

void PlacementSystem::setPlaneSurfaceHeight(float h) { planeSurfaceHeight() = h; }

float PlacementSystem::getPlaneSurfaceHeight() { return planeSurfaceHeight(); }

float &PlacementSystem::planeSurfaceHeight() {
    return inst().planeSurfaceHeight_;
}

void PlacementSystem::ensureBuiltins() {
    if (builtinsReady()) return;
    builtinsReady() = true;

    registerSnapRule("grid", [](const PlacementWorld &world, float worldX, float worldY) {
        SnapResult r;
        int cx = 0, cy = 0;
        grid::worldToCell(world.getGrid(), worldX, worldY, cx, cy, world.getWidth(),
                          world.getHeight());
        r.cellX = cx;
        r.cellY = cy;
        world.cellToWorldPlane(cx, cy, r.worldX, r.worldY);
        return r;
    });

    registerSnapRule("cell", [](const PlacementWorld &world, float worldX, float worldY) {
        SnapResult r;
        r.cellX = int(std::floor(worldX + 0.5f));
        r.cellY = int(std::floor(worldY + 0.5f));
        r.worldX = float(r.cellX);
        r.worldY = float(r.cellY);
        (void)world;
        return r;
    });

    registerSnapRule("free", [](const PlacementWorld &world, float worldX, float worldY) {
        SnapResult r;
        r.worldX = worldX;
        r.worldY = worldY;
        r.cellX = world.worldToCellX(worldX);
        r.cellY = world.worldToCellY(worldY);
        return r;
    });

    // default: bounds + occupancy + terrain + adjacency
    registerValidateRule("default", [](const PlacementWorld &world, const PlacementQuery &q,
                                       std::string *reason) {
        const BuildingDefinition *def = BuildingRegistry::find(q.buildingId);
        if (!def) {
            if (reason) *reason = "unknown_building";
            return false;
        }
        if (!checkBoundsAndOccupancy(world, *def, q, true, reason)) return false;
        if (!checkTerrain(world, *def, q, reason)) return false;
        if (!checkAdjacency(world, *def, q, reason)) return false;
        if (!checkSurfacePatch(*def, q, reason)) return false;
        if (!checkStructuralSupport(world, *def, q, reason)) return false;
        return true;
    });

    registerValidateRule("boundsOnly", [](const PlacementWorld &world, const PlacementQuery &q,
                                          std::string *reason) {
        const BuildingDefinition *def = BuildingRegistry::find(q.buildingId);
        if (!def) {
            if (reason) *reason = "unknown_building";
            return false;
        }
        return checkBoundsAndOccupancy(world, *def, q, false, reason);
    });

    registerValidateRule("overlapOk", [](const PlacementWorld &world, const PlacementQuery &q,
                                         std::string *reason) {
        const BuildingDefinition *def = BuildingRegistry::find(q.buildingId);
        if (!def) {
            if (reason) *reason = "unknown_building";
            return false;
        }
        if (!checkBoundsAndOccupancy(world, *def, q, false, reason)) return false;
        if (!checkTerrain(world, *def, q, reason)) return false;
        if (!checkAdjacency(world, *def, q, reason)) return false;
        if (!checkSurfacePatch(*def, q, reason)) return false;
        if (!checkStructuralSupport(world, *def, q, reason)) return false;
        return true;
    });

    registerSurfaceProvider("plane", [](const PlacementWorld &world, float x, float y) {
        PlacementHit hit;
        const float h = planeSurfaceHeight();
        if (world.getGrid().plane == grid::GridPlane::XZ) {
            hit.worldX = x;
            hit.worldY = h;
            hit.worldZ = y;
        } else {
            hit.worldX = x;
            hit.worldY = y;
            hit.worldZ = h;
            hit.normalX = 0.f;
            hit.normalY = 0.f;
            hit.normalZ = 1.f;
        }
        hit.surfaceId = "plane";
        return eve::Result<PlacementHit>::success(std::move(hit));
    });
}

int PlacementSystem::nextInstanceId() { return ++instanceCounter(); }

void PlacementSystem::pushEvent(BuildingChangeEvent ev) { emit(std::move(ev)); }

void PlacementSystem::pollEvents(std::vector<BuildingChangeEvent> &out) {
    out = eventQueue();
    eventQueue().clear();
}

void PlacementSystem::clearEvents() { eventQueue().clear(); }

const std::vector<BuildingChangeEvent> &PlacementSystem::events() { return eventQueue(); }

void PlacementSystem::emit(BuildingChangeEvent ev) {
    eventQueue().push_back(ev);
    for (auto &kv : changeHooks()) {
        if (kv.second) kv.second(ev);
    }
}

SnapResult PlacementSystem::snapWithMode(const PlacementWorld &world, const std::string &mode,
                                         float worldX, float worldY) {
    ensureBuiltins();
    auto &rules = snapRules();
    auto it = rules.find(mode.empty() ? "grid" : mode);
    if (it == rules.end() || !it->second) {
        it = rules.find("grid");
    }
    if (it != rules.end() && it->second) return it->second(world, worldX, worldY);
    SnapResult r;
    r.cellX = world.worldToCellX(worldX);
    r.cellY = world.worldToCellY(worldY);
    r.worldX = world.cellToWorldX(r.cellX);
    r.worldY = world.cellToWorldY(r.cellY);
    return r;
}

SnapResult PlacementSystem::snap(const PlacementWorld &world, const std::string &buildingId,
                                 float worldX, float worldY) {
    ensureBuiltins();
    std::string mode = world.getSnapMode();
    if (const BuildingDefinition *def = BuildingRegistry::find(buildingId)) {
        if (!def->snapMode.empty()) mode = def->snapMode;
    }
    return snapWithMode(world, mode, worldX, worldY);
}

SnapResult PlacementSystem::snap3D(const PlacementWorld &world, const std::string &buildingId,
                                   float worldX, float worldY, float worldZ) {
    float px = worldX;
    float py = worldY;
    float elev = worldZ;
    if (world.getGrid().plane == grid::GridPlane::XZ) {
        // 世界坐标 (x, y=高度, z)：网格平面第二轴取 z，垂直高度取 y。
        py = worldZ;
        elev = worldY;
    }
    SnapResult s = snap(world, buildingId, px, py);
    s.elevation = elev;
    return s;
}

float PlacementSystem::normalizeRotation(const std::string &buildingId, float rotationDeg) {
    const BuildingDefinition *def = BuildingRegistry::find(buildingId);
    const std::string mode = def ? def->rotationMode : "cardinal";
    if (mode == "none") return 0.f;
    if (mode == "free") return wrapDeg(rotationDeg);
    if (mode == "hex") {
        const float d = wrapDeg(rotationDeg);
        const int steps = int(std::lround(d / 60.f)) % 6;
        return float((steps < 0 ? steps + 6 : steps) * 60);
    }
    // cardinal（默认）
    return float(cardinalQuarter(rotationDeg) * 90);
}

void PlacementSystem::effectiveFootprint(const BuildingDefinition &def, float rotationDeg, int *outW,
                                         int *outH) {
    int w = def.footprintW;
    int h = def.footprintH;
    if (def.rotationMode == "hex") {
        const int steps = int(std::lround(wrapDeg(rotationDeg) / 60.f)) % 6;
        grid::rotatedFootprintSize(def.footprintW, def.footprintH, def.footprintMask, steps, true,
                                   w, h);
    } else if (def.rotationMode == "cardinal" || def.rotationMode.empty()) {
        const int q = cardinalQuarter(rotationDeg);
        if (q == 1 || q == 3) std::swap(w, h);
    }
    if (outW) *outW = w;
    if (outH) *outH = h;
}

bool PlacementSystem::foreachFootprintCell(const BuildingDefinition &def, int originCellX,
                                           int originCellY, float rotationDeg,
                                           const std::function<bool(int cx, int cy)> &fn) {
    if (!fn) return false;
    const bool hex = def.rotationMode == "hex";
    const int steps =
        hex ? int(std::lround(wrapDeg(rotationDeg) / 60.f)) % 6 : cardinalQuarter(rotationDeg);
    bool ok = true;
    grid::foreachRotatedFootprint(def.footprintW, def.footprintH, def.footprintMask, steps, hex,
                                  [&](int lx, int ly) {
                                      if (!fn(originCellX + lx, originCellY + ly)) ok = false;
                                  });
    return ok;
}

bool PlacementSystem::checkBoundsAndOccupancy(const PlacementWorld &world,
                                              const BuildingDefinition &def,
                                              const PlacementQuery &q, bool checkOccupancy,
                                              std::string *reason) {
    bool ok = true;
    foreachFootprintCell(def, q.cellX, q.cellY, q.rotationDeg, [&](int cx, int cy) {
        if (!world.inBounds(cx, cy)) {
            if (reason) *reason = "out_of_bounds";
            ok = false;
            return false;
        }
        if (checkOccupancy) {
            const int occ = world.getOccupantAtLevel(def.channel, cx, cy, q.level);
            if (occ != 0 && occ != q.excludeInstanceId) {
                if (reason) *reason = "occupied";
                ok = false;
                return false;
            }
        }
        return true;
    });
    return ok;
}

bool PlacementSystem::checkTerrain(const PlacementWorld &world, const BuildingDefinition &def,
                                   const PlacementQuery &q, std::string *reason) {
    if (def.requireTerrain.empty() && def.forbidTerrain.empty()) return true;
    bool ok = true;
    foreachFootprintCell(def, q.cellX, q.cellY, q.rotationDeg, [&](int cx, int cy) {
        const int sem = world.getTerrain(cx, cy);
        if (!def.forbidTerrain.empty()) {
            if (std::find(def.forbidTerrain.begin(), def.forbidTerrain.end(), sem) !=
                def.forbidTerrain.end()) {
                if (reason) *reason = "terrain_forbidden";
                ok = false;
                return false;
            }
        }
        if (!def.requireTerrain.empty()) {
            if (std::find(def.requireTerrain.begin(), def.requireTerrain.end(), sem) ==
                def.requireTerrain.end()) {
                if (reason) *reason = "terrain_mismatch";
                ok = false;
                return false;
            }
        }
        return true;
    });
    return ok;
}

bool PlacementSystem::checkAdjacency(const PlacementWorld &world, const BuildingDefinition &def,
                                     const PlacementQuery &q, std::string *reason) {
    const bool needTag = !def.requireAdjacentTag.empty();
    const bool needTerrain = def.requireAdjacentTerrain >= 0;
    if (!needTag && !needTerrain) return true;

    // Collect footprint cells + expand by 1 (4-neigh).
    std::vector<std::pair<int, int>> cells;
    foreachFootprintCell(def, q.cellX, q.cellY, q.rotationDeg, [&](int cx, int cy) {
        cells.emplace_back(cx, cy);
        return true;
    });

    auto isFootprint = [&](int x, int y) {
        return std::find(cells.begin(), cells.end(), std::make_pair(x, y)) != cells.end();
    };

    bool tagOk = !needTag;
    bool terrainOk = !needTerrain;
    static const int dx[4] = {1, -1, 0, 0};
    static const int dy[4] = {0, 0, 1, -1};

    for (const auto &c : cells) {
        for (int i = 0; i < 4; ++i) {
            const int nx = c.first + dx[i];
            const int ny = c.second + dy[i];
            if (isFootprint(nx, ny)) continue;
            if (!world.inBounds(nx, ny)) continue;

            if (needTag && !tagOk) {
                const int occ = world.getAnyOccupantAtLevel(nx, ny, q.level);
                if (occ != 0 && occ != q.excludeInstanceId) {
                    auto it = world.buildings().find(occ);
                    if (it != world.buildings().end()) {
                        const PlacedBuilding &pb = it->second;
                        bool hit = pb.hasTag(def.requireAdjacentTag);
                        if (!hit) {
                            if (const BuildingDefinition *odef =
                                    BuildingRegistry::find(pb.buildingId)) {
                                hit = odef->hasTag(def.requireAdjacentTag);
                            }
                        }
                        if (hit) tagOk = true;
                    }
                }
            }
            if (needTerrain && !terrainOk) {
                if (world.getTerrain(nx, ny) == def.requireAdjacentTerrain) terrainOk = true;
            }
            if (tagOk && terrainOk) return true;
        }
    }

    if (needTag && !tagOk) {
        if (reason) *reason = "adjacency_tag";
        return false;
    }
    if (needTerrain && !terrainOk) {
        if (reason) *reason = "adjacency_terrain";
        return false;
    }
    return true;
}

bool PlacementSystem::runValidate(const PlacementWorld &world, const BuildingDefinition &def,
                                  const PlacementQuery &q, std::string *reason) {
    ensureBuiltins();
    std::string rule = def.validateRule.empty() ? world.getValidateRule() : def.validateRule;
    if (rule.empty()) rule = "default";
    auto &rules = validateRules();
    auto it = rules.find(rule);
    if (it == rules.end() || !it->second) {
        it = rules.find("default");
    }
    if (it == rules.end() || !it->second) {
        if (reason) *reason = "validate_rejected";
        return false;
    }
    return it->second(world, q, reason);
}

bool PlacementSystem::canPlaceElev(PlacementWorld *world, const std::string &buildingId, int cellX,
                                   int cellY, float elevation, float rotationDeg,
                                   int excludeInstanceId, std::string *reason, int level) {
    const BuildingDefinition *definition = BuildingRegistry::find(buildingId);
    if (definition && definition->placementKind != "cell") {
        if (reason) {
            if (definition->placementKind == "edge")
                *reason = "edge_requires_edge_address";
            else if (definition->placementKind == "corner")
                *reason = "corner_requires_corner_address";
            else
                *reason = "free_requires_free_address";
        }
        return false;
    }
    PlacementQuery q;
    q.buildingId = buildingId;
    q.cellX = cellX;
    q.cellY = cellY;
    q.level = level == std::numeric_limits<int>::min() && world ? world->getActiveLevel() : level;
    q.rotationDeg = normalizeRotation(buildingId, rotationDeg);
    if (world) world->cellToWorldPlane(cellX, cellY, q.worldX, q.worldY);
    q.elevation = elevation;
    q.excludeInstanceId = excludeInstanceId;
    return canPlaceQuery(world, q, reason);
}

bool PlacementSystem::checkSurfacePatch(const BuildingDefinition &def, const PlacementQuery &q,
                                        std::string *reason) {
    if (q.surfaceSampleCount <= 0) return true;
    if (q.surfaceMaxSlopeDegrees > def.maxSurfaceSlopeDegrees + 1e-4f) {
        if (reason) *reason = "surface_slope";
        return false;
    }
    if (def.maxSurfaceHeightDelta >= 0.f &&
        q.surfaceHeightDelta > def.maxSurfaceHeightDelta + 1e-4f) {
        if (reason) *reason = "surface_height_delta";
        return false;
    }
    return true;
}

bool PlacementSystem::checkStructuralSupport(const PlacementWorld &world,
                                             const BuildingDefinition &def,
                                             const PlacementQuery &q, std::string *reason) {
    if (def.supportMode == "corner_below") {
        if (def.placementKind != "corner") {
            if (reason) *reason = "support_mode_mismatch";
            return false;
        }
        const int instanceId = world.getAnyCornerOccupantAtLevel(q.cellX, q.cellY, q.level - 1);
        const auto found = world.buildings().find(instanceId);
        if (instanceId == 0 || found == world.buildings().end()) {
            if (reason) *reason = "support_missing";
            return false;
        }
        if (def.supportTag.empty()) return true;
        const BuildingDefinition *support = BuildingRegistry::find(found->second.buildingId);
        const bool tagged = found->second.hasTag(def.supportTag) ||
                            (support && support->hasTag(def.supportTag));
        if (!tagged && reason) *reason = "support_missing";
        return tagged;
    }
    if (def.supportMode != "cell_below") return true;
    bool supported = true;
    foreachFootprintCell(def, q.cellX, q.cellY, q.rotationDeg, [&](int cellX, int cellY) {
        const int instanceId = world.getAnyOccupantAtLevel(cellX, cellY, q.level - 1);
        const auto found = world.buildings().find(instanceId);
        if (instanceId == 0 || found == world.buildings().end()) {
            supported = false;
        } else if (!def.supportTag.empty()) {
            const PlacedBuilding &placed = found->second;
            const BuildingDefinition *support = BuildingRegistry::find(placed.buildingId);
            supported = placed.hasTag(def.supportTag) ||
                        (support && support->hasTag(def.supportTag));
        }
        if (!supported && reason) *reason = "support_missing";
        return supported;
    });
    return supported;
}

std::vector<int> PlacementSystem::collectStructuralSupports(const PlacementWorld &world,
                                                            const BuildingDefinition &def,
                                                            const PlacementQuery &q) {
    std::vector<int> result;
    if (def.supportMode == "corner_below") {
        const int support = world.getAnyCornerOccupantAtLevel(q.cellX, q.cellY, q.level - 1);
        if (support != 0) result.push_back(support);
        return result;
    }
    if (def.supportMode != "cell_below") return result;
    foreachFootprintCell(def, q.cellX, q.cellY, q.rotationDeg, [&](int cellX, int cellY) {
        const int support = world.getAnyOccupantAtLevel(cellX, cellY, q.level - 1);
        if (support != 0 && std::find(result.begin(), result.end(), support) == result.end())
            result.push_back(support);
        return true;
    });
    return result;
}

bool PlacementSystem::hasStructuralDependents(const PlacementWorld &world, int instanceId) {
    for (const auto &[id, placed] : world.buildings_) {
        (void)id;
        if (std::find(placed.supportInstanceIds.begin(), placed.supportInstanceIds.end(),
                      instanceId) != placed.supportInstanceIds.end())
            return true;
    }
    return false;
}

bool PlacementSystem::replacementPreservesStructuralDependents(
    const PlacementWorld &world, int instanceId, const BuildingDefinition &replacement) {
    const auto sourceIt = world.buildings_.find(instanceId);
    if (sourceIt == world.buildings_.end()) return false;
    const PlacedBuilding &source = sourceIt->second;

    std::unordered_set<uint64_t> replacementCells;
    if (replacement.placementKind != "edge") {
        foreachFootprintCell(replacement, source.originCellX, source.originCellY,
                             source.rotationDeg, [&](int x, int y) {
                                 replacementCells.insert((uint64_t(uint32_t(x)) << 32U) |
                                                         uint32_t(y));
                                 return true;
                             });
    }

    for (const auto &[candidateId, dependent] : world.buildings_) {
        (void)candidateId;
        if (std::find(dependent.supportInstanceIds.begin(),
                      dependent.supportInstanceIds.end(), instanceId) ==
            dependent.supportInstanceIds.end())
            continue;
        const BuildingDefinition *dependentDef = BuildingRegistry::find(dependent.buildingId);
        if (!dependentDef) return false;
        const bool tagMatches = dependentDef->supportTag.empty() ||
                                replacement.hasTag(dependentDef->supportTag);
        if (!tagMatches) return false;
        if (dependentDef->supportMode == "corner_below") {
            if (replacement.placementKind != "corner" || source.corner != dependent.corner)
                return false;
            continue;
        }
        if (dependentDef->supportMode != "cell_below" || replacement.placementKind != "cell")
            return false;
        bool covered = true;
        foreachFootprintCell(*dependentDef, dependent.originCellX, dependent.originCellY,
                             dependent.rotationDeg, [&](int x, int y) {
                                 if (world.getAnyOccupantAtLevel(x, y, dependent.level - 1) !=
                                     instanceId)
                                    return true;
                                 const uint64_t key = (uint64_t(uint32_t(x)) << 32U) | uint32_t(y);
                                 covered = replacementCells.contains(key);
                                 return covered;
                             });
        if (!covered) return false;
    }
    return true;
}

bool PlacementSystem::canPlaceQuery(PlacementWorld *world, const PlacementQuery &q,
                                    std::string *reason) {
    ensureBuiltins();
    if (!world) {
        if (reason) *reason = "no_world";
        return false;
    }
    const BuildingDefinition *def = BuildingRegistry::find(q.buildingId);
    if (!def) {
        if (reason) *reason = "unknown_building";
        return false;
    }
    return runValidate(*world, *def, q, reason);
}

bool PlacementSystem::canPlace(PlacementWorld *world, const std::string &buildingId, int cellX,
                               int cellY, float rotationDeg, int excludeInstanceId,
                               std::string *reason) {
    return canPlaceElev(world, buildingId, cellX, cellY, 0.f, rotationDeg, excludeInstanceId,
                        reason);
}

eve::Result<EdgeAddress> PlacementSystem::canonicalEdge(int cellX, int cellY,
                                                        const std::string &direction) {
    EdgeAddress edge;
    edge.x = cellX;
    edge.y = cellY;
    if (direction == "north" || direction == "n") {
        edge.axis = EdgeAxis::Horizontal;
    } else if (direction == "south" || direction == "s") {
        edge.axis = EdgeAxis::Horizontal;
        ++edge.y;
    } else if (direction == "west" || direction == "w") {
        edge.axis = EdgeAxis::Vertical;
    } else if (direction == "east" || direction == "e") {
        edge.axis = EdgeAxis::Vertical;
        ++edge.x;
    } else {
        return eve::Result<EdgeAddress>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "edge direction must be north/east/south/west",
            direction, {}, "building.edge"));
    }
    return eve::Result<EdgeAddress>::success(edge);
}

bool PlacementSystem::canPlaceEdge(PlacementWorld *world, const std::string &buildingId, int cellX,
                                   int cellY, const std::string &direction, int excludeInstanceId,
                                   std::string *reason, int level) {
    if (!world) {
        if (reason) *reason = "no_world";
        return false;
    }
    const BuildingDefinition *def = BuildingRegistry::find(buildingId);
    if (!def) {
        if (reason) *reason = "unknown_building";
        return false;
    }
    if (def->placementKind != "edge") {
        if (reason) *reason = "not_edge_building";
        return false;
    }
    auto result = canonicalEdge(cellX, cellY, direction);
    if (!result.ok()) {
        if (reason) *reason = "invalid_edge_direction";
        return false;
    }
    const EdgeAddress edge = std::move(result).takeValue();
    const bool inBounds = edge.axis == EdgeAxis::Horizontal
                              ? edge.x >= 0 && edge.x < world->width_ && edge.y >= 0 &&
                                    edge.y <= world->height_
                              : edge.x >= 0 && edge.x <= world->width_ && edge.y >= 0 &&
                                    edge.y < world->height_;
    if (!inBounds) {
        if (reason) *reason = "edge_out_of_bounds";
        return false;
    }
    const int resolvedLevel =
        level == std::numeric_limits<int>::min() ? world->getActiveLevel() : level;
    const auto *channels = world->findEdgeChannels(edge.axis, resolvedLevel);
    const size_t index = edge.axis == EdgeAxis::Horizontal
                             ? size_t(edge.y) * size_t(world->width_) + size_t(edge.x)
                             : size_t(edge.y) * size_t(world->width_ + 1) + size_t(edge.x);
    if (!channels) return true;
    const auto channelIt = channels->find(def->channel);
    const int occupant = channelIt != channels->end() && index < channelIt->second.size()
                             ? channelIt->second[index]
                             : 0;
    if (occupant != 0 && occupant != excludeInstanceId) {
        if (reason) *reason = "edge_occupied";
        return false;
    }
    return true;
}

int PlacementSystem::placeEdge(PlacementWorld *world, const std::string &buildingId, int cellX,
                               int cellY, const std::string &direction) {
    if (!canPlaceEdge(world, buildingId, cellX, cellY, direction, 0, nullptr)) return 0;
    const BuildingDefinition *def = BuildingRegistry::find(buildingId);
    auto address = canonicalEdge(cellX, cellY, direction);
    if (!def || !address.ok()) return 0;
    const EdgeAddress edge = std::move(address).takeValue();

    PlacedBuilding placed;
    placed.instanceId = nextInstanceId();
    placed.buildingId = buildingId;
    placed.placementKind = "edge";
    placed.edge = edge;
    placed.originCellX = edge.x;
    placed.originCellY = edge.y;
    placed.level = world->getActiveLevel();
    placed.channel = def->channel;
    placed.tags = def->tags;
    float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
    world->cellToWorldPlane(edge.x, edge.y, ax, ay);
    world->cellToWorldPlane(edge.x + (edge.axis == EdgeAxis::Horizontal ? 1 : 0),
                            edge.y + (edge.axis == EdgeAxis::Vertical ? 1 : 0), bx, by);
    placed.worldX = (ax + bx) * 0.5f;
    placed.worldY = (ay + by) * 0.5f;
    placed.rotationDeg = edge.axis == EdgeAxis::Horizontal ? 0.f : 90.f;

    auto &channels = world->edgeChannels(edge.axis, placed.level);
    auto &occupancy = channels[def->channel];
    const size_t required = edge.axis == EdgeAxis::Horizontal
                                ? size_t(world->width_) * size_t(world->height_ + 1)
                                : size_t(world->width_ + 1) * size_t(world->height_);
    if (occupancy.size() != required) occupancy.assign(required, 0);
    const size_t index = edge.axis == EdgeAxis::Horizontal
                             ? size_t(edge.y) * size_t(world->width_) + size_t(edge.x)
                             : size_t(edge.y) * size_t(world->width_ + 1) + size_t(edge.x);
    occupancy[index] = placed.instanceId;
    world->buildings()[placed.instanceId] = placed;
    world->instanceOrder_.push_back(placed.instanceId);

    BuildingChangeEvent event;
    event.action = "place";
    event.worldId = world->getId();
    event.buildingId = buildingId;
    event.instanceId = placed.instanceId;
    event.cellX = edge.x;
    event.cellY = edge.y;
    event.rotationDeg = placed.rotationDeg;
    event.worldX = placed.worldX;
    event.worldY = placed.worldY;
    event.channel = placed.channel;
    if (world->publishEvents_) emit(std::move(event));
    return placed.instanceId;
}

bool PlacementSystem::canPlaceCorner(PlacementWorld *world, const std::string &buildingId,
                                     int vertexX, int vertexY, int excludeInstanceId,
                                     std::string *reason, int level) {
    if (!world) {
        if (reason) *reason = "no_world";
        return false;
    }
    const BuildingDefinition *def = BuildingRegistry::find(buildingId);
    if (!def) {
        if (reason) *reason = "unknown_building";
        return false;
    }
    if (def->placementKind != "corner") {
        if (reason) *reason = "not_corner_building";
        return false;
    }
    if (vertexX < 0 || vertexX > world->width_ || vertexY < 0 || vertexY > world->height_) {
        if (reason) *reason = "corner_out_of_bounds";
        return false;
    }
    const int resolvedLevel =
        level == std::numeric_limits<int>::min() ? world->getActiveLevel() : level;
    const int occupant =
        world->getCornerOccupantAtLevel(def->channel, vertexX, vertexY, resolvedLevel);
    if (occupant != 0 && occupant != excludeInstanceId) {
        if (reason) *reason = "corner_occupied";
        return false;
    }
    PlacementQuery query;
    query.buildingId = buildingId;
    query.cellX = vertexX;
    query.cellY = vertexY;
    query.level = resolvedLevel;
    query.excludeInstanceId = excludeInstanceId;
    return checkStructuralSupport(*world, *def, query, reason);
}

eve::Result<PlacedBuilding>
PlacementSystem::placeCornerResult(PlacementWorld *world, const std::string &buildingId,
                                   int vertexX, int vertexY) {
    std::string reason;
    if (!canPlaceCorner(world, buildingId, vertexX, vertexY, 0, &reason)) {
        return eve::Result<PlacedBuilding>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "corner placement rejected without mutation", reason,
            {}, "building.corner"));
    }
    const BuildingDefinition *def = BuildingRegistry::find(buildingId);
    if (!def) {
        return eve::Result<PlacedBuilding>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "corner definition was not found", buildingId, {},
            "building.corner"));
    }

    PlacedBuilding placed;
    placed.instanceId = nextInstanceId();
    placed.buildingId = buildingId;
    placed.placementKind = "corner";
    placed.corner = CornerAddress{vertexX, vertexY};
    placed.originCellX = vertexX;
    placed.originCellY = vertexY;
    placed.level = world->getActiveLevel();
    placed.channel = def->channel;
    placed.tags = def->tags;
    world->cellToWorldPlane(vertexX, vertexY, placed.worldX, placed.worldY);
    PlacementQuery supportQuery;
    supportQuery.cellX = vertexX;
    supportQuery.cellY = vertexY;
    supportQuery.level = placed.level;
    placed.supportInstanceIds = collectStructuralSupports(*world, *def, supportQuery);

    auto &occupancy = world->cornerChannels(placed.level)[def->channel];
    const size_t required = size_t(world->width_ + 1) * size_t(world->height_ + 1);
    if (occupancy.size() != required) occupancy.assign(required, 0);
    occupancy[size_t(vertexY) * size_t(world->width_ + 1) + size_t(vertexX)] =
        placed.instanceId;
    world->buildings_[placed.instanceId] = placed;
    world->instanceOrder_.push_back(placed.instanceId);

    BuildingChangeEvent event;
    event.action = "place";
    event.worldId = world->getId();
    event.buildingId = buildingId;
    event.instanceId = placed.instanceId;
    event.cellX = vertexX;
    event.cellY = vertexY;
    event.level = placed.level;
    event.worldX = placed.worldX;
    event.worldY = placed.worldY;
    event.channel = placed.channel;
    if (world->publishEvents_) emit(std::move(event));
    return eve::Result<PlacedBuilding>::success(std::move(placed));
}

int PlacementSystem::placeCorner(PlacementWorld *world, const std::string &buildingId,
                                 int vertexX, int vertexY) {
    auto result = placeCornerResult(world, buildingId, vertexX, vertexY);
    return result.ok() ? result.value().instanceId : 0;
}

bool PlacementSystem::canPlaceFree(PlacementWorld *world, const std::string &buildingId,
                                   float worldX, float worldY, int excludeInstanceId,
                                   std::string *reason, int level, float rotationDeg) {
    if (!world) {
        if (reason) *reason = "no_world";
        return false;
    }
    const BuildingDefinition *def = BuildingRegistry::find(buildingId);
    if (!def) {
        if (reason) *reason = "unknown_building";
        return false;
    }
    if (def->placementKind != "free") {
        if (reason) *reason = "not_free_building";
        return false;
    }
    if (!std::isfinite(worldX) || !std::isfinite(worldY)) {
        if (reason) *reason = "free_invalid_anchor";
        return false;
    }
    int cellX = 0;
    int cellY = 0;
    grid::worldToCell(*world->grid_, worldX, worldY, cellX, cellY, world->width_,
                      world->height_);
    if (cellX < 0 || cellX >= world->width_ || cellY < 0 || cellY >= world->height_) {
        if (reason) *reason = "free_out_of_bounds";
        return false;
    }
    const int resolvedLevel =
        level == std::numeric_limits<int>::min() ? world->getActiveLevel() : level;
    const FreeFootprint candidate =
        definitionFreeFootprint(*def, *world, worldX, worldY,
                                normalizeRotation(buildingId, rotationDeg));
    for (const auto &[id, placed] : world->buildings_) {
        if (id == excludeInstanceId || placed.placementKind != "free" ||
            placed.level != resolvedLevel || placed.channel != def->channel)
            continue;
        if (freeFootprintsOverlap(placedFreeFootprint(placed), candidate)) {
            if (reason) *reason = "free_overlap";
            return false;
        }
    }
    PlacementQuery query;
    query.buildingId = buildingId;
    query.cellX = cellX;
    query.cellY = cellY;
    query.worldX = worldX;
    query.worldY = worldY;
    query.level = resolvedLevel;
    query.excludeInstanceId = excludeInstanceId;
    return checkStructuralSupport(*world, *def, query, reason);
}

bool PlacementSystem::containsFreePoint(const PlacedBuilding &placed, float worldX,
                                        float worldY) {
    return placed.placementKind == "free" &&
           freeFootprintContainsInternal(placedFreeFootprint(placed), worldX, worldY);
}

eve::Result<PlacedBuilding>
PlacementSystem::placeFreeResult(PlacementWorld *world, const std::string &buildingId,
                                 float worldX, float worldY, float elevation,
                                 float rotationDeg) {
    std::string reason;
    if (!canPlaceFree(world, buildingId, worldX, worldY, 0, &reason,
                      std::numeric_limits<int>::min(), rotationDeg)) {
        return eve::Result<PlacedBuilding>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "free placement rejected without mutation", reason,
            {}, "building.free"));
    }
    const BuildingDefinition *def = BuildingRegistry::find(buildingId);
    PlacedBuilding placed;
    placed.instanceId = nextInstanceId();
    placed.buildingId = buildingId;
    placed.placementKind = "free";
    placed.level = world->getActiveLevel();
    placed.worldX = worldX;
    placed.worldY = worldY;
    placed.elevation = elevation;
    placed.rotationDeg = normalizeRotation(buildingId, rotationDeg);
    applyDefinitionFreeFootprint(placed, *def, *world);
    grid::worldToCell(*world->grid_, worldX, worldY, placed.originCellX,
                      placed.originCellY, world->width_, world->height_);
    placed.channel = def->channel;
    placed.tags = def->tags;
    world->buildings_[placed.instanceId] = placed;
    world->instanceOrder_.push_back(placed.instanceId);

    BuildingChangeEvent event;
    event.action = "place";
    event.worldId = world->getId();
    event.buildingId = buildingId;
    event.instanceId = placed.instanceId;
    event.cellX = placed.originCellX;
    event.cellY = placed.originCellY;
    event.level = placed.level;
    event.rotationDeg = placed.rotationDeg;
    event.worldX = placed.worldX;
    event.worldY = placed.worldY;
    event.elevation = placed.elevation;
    event.channel = placed.channel;
    if (world->publishEvents_) emit(std::move(event));
    return eve::Result<PlacedBuilding>::success(std::move(placed));
}

eve::Result<PlacedBuilding>
PlacementSystem::placeFreeSurfaceResult(PlacementWorld *world,
                                        const std::string &buildingId,
                                        const SurfacePatch &patch, float rotationDeg) {
    if (!world) {
        return eve::Result<PlacedBuilding>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "free surface placement requires a world",
            {}, {}, "building.free"));
    }
    const BuildingDefinition *def = BuildingRegistry::find(buildingId);
    if (!def || def->placementKind != "free") {
        return eve::Result<PlacedBuilding>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "free surface placement requires a free definition", buildingId, {},
            "building.free"));
    }
    const float planeX = patch.anchor.worldX;
    const float planeY = world->getGrid().plane == grid::GridPlane::XZ
                             ? patch.anchor.worldZ
                             : patch.anchor.worldY;
    const float elevation = world->getGrid().plane == grid::GridPlane::XZ
                                ? patch.anchor.worldY
                                : patch.anchor.worldZ;
    std::string reason;
    if (!canPlaceFree(world, buildingId, planeX, planeY, 0, &reason,
                      std::numeric_limits<int>::min(), rotationDeg) ||
        patch.maxSlopeDegrees > def->maxSurfaceSlopeDegrees ||
        (def->maxSurfaceHeightDelta >= 0.f &&
         patch.heightDelta > def->maxSurfaceHeightDelta)) {
        if (reason.empty())
            reason = patch.maxSlopeDegrees > def->maxSurfaceSlopeDegrees
                         ? "surface_slope"
                         : "surface_height_delta";
        return eve::Result<PlacedBuilding>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict,
            "free surface placement rejected without mutation", reason, {},
            "building.free"));
    }

    PlacedBuilding placed;
    placed.instanceId = nextInstanceId();
    placed.buildingId = buildingId;
    placed.placementKind = "free";
    placed.level = world->getActiveLevel();
    placed.worldX = planeX;
    placed.worldY = planeY;
    placed.elevation = elevation;
    placed.rotationDeg = normalizeRotation(buildingId, rotationDeg);
    applyDefinitionFreeFootprint(placed, *def, *world);
    grid::worldToCell(*world->grid_, planeX, planeY, placed.originCellX,
                      placed.originCellY, world->width_, world->height_);
    placed.channel = def->channel;
    placed.tags = def->tags;
    placed.surfaceId = patch.anchor.surfaceId;
    placed.surfaceRevision = patch.anchor.surfaceRevision;
    placed.surfaceNormalX = patch.anchor.normalX;
    placed.surfaceNormalY = patch.anchor.normalY;
    placed.surfaceNormalZ = patch.anchor.normalZ;
    placed.surfaceTangentX = patch.anchor.tangentX;
    placed.surfaceTangentY = patch.anchor.tangentY;
    placed.surfaceTangentZ = patch.anchor.tangentZ;
    placed.surfaceSampleCount = static_cast<int>(patch.samples.size());
    placed.surfaceMaxSlopeDegrees = patch.maxSlopeDegrees;
    placed.surfaceHeightDelta = patch.heightDelta;
    world->buildings_[placed.instanceId] = placed;
    world->instanceOrder_.push_back(placed.instanceId);

    BuildingChangeEvent event;
    event.action = "place";
    event.worldId = world->getId();
    event.buildingId = buildingId;
    event.instanceId = placed.instanceId;
    event.cellX = placed.originCellX;
    event.cellY = placed.originCellY;
    event.level = placed.level;
    event.rotationDeg = placed.rotationDeg;
    event.worldX = placed.worldX;
    event.worldY = placed.worldY;
    event.elevation = placed.elevation;
    event.channel = placed.channel;
    if (world->publishEvents_) emit(std::move(event));
    return eve::Result<PlacedBuilding>::success(std::move(placed));
}

int PlacementSystem::placeFree(PlacementWorld *world, const std::string &buildingId,
                               float worldX, float worldY, float elevation,
                               float rotationDeg) {
    auto result = placeFreeResult(world, buildingId, worldX, worldY, elevation, rotationDeg);
    return result.ok() ? std::move(result).takeValue().instanceId : 0;
}

uint8_t PlacementSystem::edgeConnectionMask(const PlacementWorld &world, int instanceId) {
    const auto placedIt = world.buildings_.find(instanceId);
    if (placedIt == world.buildings_.end() || placedIt->second.placementKind != "edge") return 0;
    const PlacedBuilding &placed = placedIt->second;
    const BuildingDefinition *def = BuildingRegistry::find(placed.buildingId);
    if (!def) return 0;
    const std::string group = def->connectionGroup.empty() ? def->id : def->connectionGroup;

    auto occupantAt = [&](const EdgeAddress &edge) {
        const auto *channels = world.findEdgeChannels(edge.axis, placed.level);
        const bool inBounds = edge.axis == EdgeAxis::Horizontal
                                  ? edge.x >= 0 && edge.x < world.width_ && edge.y >= 0 &&
                                        edge.y <= world.height_
                                  : edge.x >= 0 && edge.x <= world.width_ && edge.y >= 0 &&
                                        edge.y < world.height_;
        if (!channels || !inBounds) return 0;
        const auto channelIt = channels->find(placed.channel);
        if (channelIt == channels->end()) return 0;
        const size_t index = edge.axis == EdgeAxis::Horizontal
                                 ? size_t(edge.y) * size_t(world.width_) + size_t(edge.x)
                                 : size_t(edge.y) * size_t(world.width_ + 1) + size_t(edge.x);
        return index < channelIt->second.size() ? channelIt->second[index] : 0;
    };
    auto connects = [&](const EdgeAddress &edge) {
        const int otherId = occupantAt(edge);
        if (otherId == 0 || otherId == instanceId) return false;
        const auto otherIt = world.buildings_.find(otherId);
        if (otherIt == world.buildings_.end()) return false;
        const BuildingDefinition *otherDef = BuildingRegistry::find(otherIt->second.buildingId);
        if (!otherDef) return false;
        const std::string otherGroup = otherDef->connectionGroup.empty() ? otherDef->id
                                                                         : otherDef->connectionGroup;
        return group == otherGroup;
    };

    const EdgeAddress e = placed.edge;
    EdgeAddress candidates[6];
    if (e.axis == EdgeAxis::Horizontal) {
        candidates[0] = {e.x - 1, e.y, EdgeAxis::Horizontal};
        candidates[1] = {e.x + 1, e.y, EdgeAxis::Horizontal};
        candidates[2] = {e.x, e.y - 1, EdgeAxis::Vertical};
        candidates[3] = {e.x, e.y, EdgeAxis::Vertical};
        candidates[4] = {e.x + 1, e.y - 1, EdgeAxis::Vertical};
        candidates[5] = {e.x + 1, e.y, EdgeAxis::Vertical};
    } else {
        candidates[0] = {e.x, e.y - 1, EdgeAxis::Vertical};
        candidates[1] = {e.x, e.y + 1, EdgeAxis::Vertical};
        candidates[2] = {e.x - 1, e.y, EdgeAxis::Horizontal};
        candidates[3] = {e.x, e.y, EdgeAxis::Horizontal};
        candidates[4] = {e.x - 1, e.y + 1, EdgeAxis::Horizontal};
        candidates[5] = {e.x, e.y + 1, EdgeAxis::Horizontal};
    }
    uint8_t mask = 0;
    for (int i = 0; i < 6; ++i)
        if (connects(candidates[i])) mask |= uint8_t(1u << i);
    return mask;
}

std::string PlacementSystem::edgeVariant(const PlacementWorld &world, int instanceId) {
    const uint8_t mask = edgeConnectionMask(world, instanceId);
    int count = 0;
    for (int bit = 0; bit < 6; ++bit)
        if ((mask & uint8_t(1u << bit)) != 0) ++count;
    if (count == 0) return "isolated";
    if (count == 1) return "end";
    if (count == 2) return (mask & 0x03u) == 0x03u ? "straight" : "corner";
    if (count == 3) return "tee";
    return "cross";
}

eve::Result<PlacementSystem::EdgePathPreview>
PlacementSystem::previewEdgePath(PlacementWorld *world, const std::string &buildingId,
                                 const std::vector<CornerAddress> &vertices) {
    if (!world || vertices.size() < 2) {
        return eve::Result<EdgePathPreview>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "edge path preview requires a world and at least two vertices", buildingId, {},
            "building.edge-path"));
    }
    const BuildingDefinition *definition = BuildingRegistry::find(buildingId);
    if (!definition || definition->placementKind != "edge") {
        return eve::Result<EdgePathPreview>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "edge path preview requires an edge building definition", buildingId, {},
            "building.edge-path"));
    }
    EdgePathPreview preview;
    preview.buildingId = buildingId;
    preview.level = world->getActiveLevel();
    std::unordered_set<std::string> uniqueEdges;
    for (size_t i = 1; i < vertices.size(); ++i) {
        const CornerAddress start = vertices[i - 1];
        const CornerAddress end = vertices[i];
        const bool horizontal = start.y == end.y;
        const bool vertical = start.x == end.x;
        if (horizontal == vertical) {
            return eve::Result<EdgePathPreview>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument,
                "each edge path segment must be non-empty and axis-aligned",
                std::to_string(i - 1), {}, "building.edge-path"));
        }
        const int length = horizontal ? std::abs(end.x - start.x) : std::abs(end.y - start.y);
        for (int step = 0; step < length; ++step) {
            EdgeAddress edge{horizontal ? std::min(start.x, end.x) + step : start.x,
                             vertical ? std::min(start.y, end.y) + step : start.y,
                             horizontal ? EdgeAxis::Horizontal : EdgeAxis::Vertical};
            const std::string key = std::to_string(edge.x) + ":" + std::to_string(edge.y) +
                                    ":" + (horizontal ? "h" : "v");
            if (!uniqueEdges.insert(key).second) {
                return eve::Result<EdgePathPreview>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict,
                    "edge path contains the same canonical edge more than once",
                    "edge_path_duplicate", {}, "building.edge-path"));
            }
            const char *direction = horizontal ? "north" : "west";
            std::string reason;
            if (!canPlaceEdge(world, buildingId, edge.x, edge.y, direction, 0, &reason)) {
                return eve::Result<EdgePathPreview>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict,
                    "edge path preflight rejected without mutation", reason, {},
                    "building.edge-path"));
            }
            preview.edges.push_back(edge);
        }
    }
    return eve::Result<EdgePathPreview>::success(std::move(preview));
}

eve::Result<std::vector<CornerAddress>> PlacementSystem::sampleEdgeCubicBezier(
    const std::vector<EdgeCurvePoint> &controlPoints, int subdivisions) {
    constexpr int kMaxSubdivisions = 4096;
    constexpr size_t kMaxRasterEdges = 65536;
    if (controlPoints.size() != 4 || subdivisions < 2 || subdivisions > kMaxSubdivisions) {
        return eve::Result<std::vector<CornerAddress>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "cubic edge curve requires four controls and 2..4096 subdivisions", {}, {},
            "building.edge-curve"));
    }
    for (const EdgeCurvePoint &point : controlPoints) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
            return eve::Result<std::vector<CornerAddress>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "edge curve controls must be finite", {},
                {}, "building.edge-curve"));
        }
    }

    auto roundedPoint = [&](int sample) -> eve::Result<CornerAddress> {
        const double t = static_cast<double>(sample) / static_cast<double>(subdivisions);
        const double u = 1.0 - t;
        const double x = u * u * u * controlPoints[0].x +
                         3.0 * u * u * t * controlPoints[1].x +
                         3.0 * u * t * t * controlPoints[2].x +
                         t * t * t * controlPoints[3].x;
        const double y = u * u * u * controlPoints[0].y +
                         3.0 * u * u * t * controlPoints[1].y +
                         3.0 * u * t * t * controlPoints[2].y +
                         t * t * t * controlPoints[3].y;
        constexpr double kIntMin = static_cast<double>(std::numeric_limits<int>::min()) + 1.0;
        constexpr double kIntMax = static_cast<double>(std::numeric_limits<int>::max()) - 1.0;
        if (x < kIntMin || x > kIntMax || y < kIntMin || y > kIntMax) {
            return eve::Result<CornerAddress>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "edge curve exceeds grid coordinate range",
                {}, {}, "building.edge-curve"));
        }
        return eve::Result<CornerAddress>::success(
            {static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y))});
    };

    auto first = roundedPoint(0);
    if (!first.ok())
        return eve::Result<std::vector<CornerAddress>>::failure(first.status());
    std::vector<CornerAddress> vertices;
    vertices.push_back(std::move(first).takeValue());
    for (int sample = 1; sample <= subdivisions; ++sample) {
        auto targetResult = roundedPoint(sample);
        if (!targetResult.ok())
            return eve::Result<std::vector<CornerAddress>>::failure(targetResult.status());
        const CornerAddress target = std::move(targetResult).takeValue();
        CornerAddress cursor = vertices.back();
        while (cursor != target) {
            const int remainingX = target.x - cursor.x;
            const int remainingY = target.y - cursor.y;
            if (remainingX != 0 &&
                (remainingY == 0 || std::abs(remainingX) >= std::abs(remainingY)))
                cursor.x += remainingX > 0 ? 1 : -1;
            else
                cursor.y += remainingY > 0 ? 1 : -1;
            vertices.push_back(cursor);
            if (vertices.size() - 1 > kMaxRasterEdges) {
                return eve::Result<std::vector<CornerAddress>>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument,
                    "edge curve rasterization exceeds 65536 unit edges", {}, {},
                    "building.edge-curve"));
            }
        }
    }
    if (vertices.size() < 2) {
        return eve::Result<std::vector<CornerAddress>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "edge curve rasterization must cover at least one unit edge", {}, {},
            "building.edge-curve"));
    }
    return eve::Result<std::vector<CornerAddress>>::success(std::move(vertices));
}

eve::Result<PlacementSystem::EdgePathPreview> PlacementSystem::previewEdgeCubicBezier(
    PlacementWorld *world, const std::string &buildingId,
    const std::vector<EdgeCurvePoint> &controlPoints, int subdivisions) {
    auto vertices = sampleEdgeCubicBezier(controlPoints, subdivisions);
    if (!vertices.ok()) return eve::Result<EdgePathPreview>::failure(vertices.status());
    return previewEdgePath(world, buildingId, std::move(vertices).takeValue());
}

eve::Result<PlacementSystem::EdgePathPlacement> PlacementSystem::placeEdgeCubicBezier(
    PlacementWorld *world, const std::string &buildingId,
    const std::vector<EdgeCurvePoint> &controlPoints, int subdivisions) {
    auto preview = previewEdgeCubicBezier(world, buildingId, controlPoints, subdivisions);
    if (!preview.ok()) return eve::Result<EdgePathPlacement>::failure(preview.status());
    return commitEdgePath(world, std::move(preview).takeValue(), &controlPoints, subdivisions);
}

eve::Result<PlacementSystem::EdgeCurveSurface> PlacementSystem::sampleEdgeCurveSurface(
    const PlacementWorld &world, const std::string &surfaceName,
    const std::vector<EdgeCurvePoint> &controlPoints, int subdivisions) {
    auto validated = sampleEdgeCubicBezier(controlPoints, subdivisions);
    if (!validated.ok()) return eve::Result<EdgeCurveSurface>::failure(validated.status());
    if (surfaceName.empty())
        return eve::Result<EdgeCurveSurface>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "curve surface provider name is required", {},
            {}, "building.edge-curve-surface"));
    float ox = 0.f, oy = 0.f, xx = 0.f, xy = 0.f, yx = 0.f, yy = 0.f;
    world.cellToWorldPlane(0, 0, ox, oy);
    world.cellToWorldPlane(1, 0, xx, xy);
    world.cellToWorldPlane(0, 1, yx, yy);
    xx -= ox;
    xy -= oy;
    yx -= ox;
    yy -= oy;
    EdgeCurveSurface result;
    result.providerName = surfaceName;
    result.samples.reserve(static_cast<size_t>(subdivisions + 1));
    for (int index = 0; index <= subdivisions; ++index) {
        const double t = static_cast<double>(index) / subdivisions;
        const double u = 1.0 - t;
        const double gx = u * u * u * controlPoints[0].x +
                          3.0 * u * u * t * controlPoints[1].x +
                          3.0 * u * t * t * controlPoints[2].x +
                          t * t * t * controlPoints[3].x;
        const double gy = u * u * u * controlPoints[0].y +
                          3.0 * u * u * t * controlPoints[1].y +
                          3.0 * u * t * t * controlPoints[2].y +
                          t * t * t * controlPoints[3].y;
        auto sampled = sampleSurface(world, surfaceName,
                                     ox + static_cast<float>(gx) * xx +
                                         static_cast<float>(gy) * yx,
                                     oy + static_cast<float>(gx) * xy +
                                         static_cast<float>(gy) * yy);
        if (!sampled.ok()) return eve::Result<EdgeCurveSurface>::failure(sampled.status());
        const PlacementHit &hit = sampled.value();
        if (index == 0) {
            result.surfaceId = hit.surfaceId;
            result.surfaceRevision = hit.surfaceRevision;
        } else if (hit.surfaceId != result.surfaceId ||
                   hit.surfaceRevision != result.surfaceRevision) {
            return eve::Result<EdgeCurveSurface>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict,
                "curve crosses a surface identity or revision boundary", hit.surfaceId, {},
                "building.edge-curve-surface"));
        }
        result.samples.push_back({hit.worldX, hit.worldY, hit.worldZ, hit.normalX, hit.normalY,
                                  hit.normalZ});
    }
    return eve::Result<EdgeCurveSurface>::success(std::move(result));
}

eve::Result<PlacementSystem::EdgePathPlacement>
PlacementSystem::placeEdgeCubicBezierOnSurface(
    PlacementWorld *world, const std::string &buildingId,
    const std::vector<EdgeCurvePoint> &controlPoints, int subdivisions,
    const std::string &surfaceName) {
    if (!world)
        return eve::Result<EdgePathPlacement>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "curve surface placement requires a world",
            {}, {}, "building.edge-curve-surface"));
    auto preview = previewEdgeCubicBezier(world, buildingId, controlPoints, subdivisions);
    if (!preview.ok()) return eve::Result<EdgePathPlacement>::failure(preview.status());
    auto surface = sampleEdgeCurveSurface(*world, surfaceName, controlPoints, subdivisions);
    if (!surface.ok()) return eve::Result<EdgePathPlacement>::failure(surface.status());
    return commitEdgePath(world, std::move(preview).takeValue(), &controlPoints, subdivisions,
                          &surface.value());
}

eve::Result<PlacementSystem::EdgeLinePlacement>
PlacementSystem::placeEdgeLine(PlacementWorld *world, const std::string &buildingId,
                               int startVertexX, int startVertexY, int endVertexX,
                               int endVertexY) {
    auto path = placeEdgePath(world, buildingId,
                              {{startVertexX, startVertexY}, {endVertexX, endVertexY}});
    if (!path.ok()) return eve::Result<EdgeLinePlacement>::failure(path.status());
    EdgeLinePlacement result;
    result.instanceIds = std::move(path).takeValue().instanceIds;
    return eve::Result<EdgeLinePlacement>::success(std::move(result));
}

eve::Result<PlacementSystem::EdgePathPlacement>
PlacementSystem::placeEdgePath(PlacementWorld *world, const std::string &buildingId,
                               const std::vector<CornerAddress> &vertices) {
    auto previewResult = previewEdgePath(world, buildingId, vertices);
    if (!previewResult.ok())
        return eve::Result<EdgePathPlacement>::failure(previewResult.status());
    return commitEdgePath(world, std::move(previewResult).takeValue());
}

eve::Result<PlacementSystem::EdgePathPlacement> PlacementSystem::commitEdgePath(
    PlacementWorld *world, EdgePathPreview preview,
    const std::vector<EdgeCurvePoint> *curveControls, int curveSubdivisions,
    const EdgeCurveSurface *curveSurface) {
    const BuildingDefinition *def = BuildingRegistry::find(preview.buildingId);
    const std::vector<EdgeAddress> &edges = preview.edges;
    const EdgeCurveGroupId curveGroupId =
        curveControls ? EdgeCurveGroupId{world->nextEdgeCurveGroupId_++} : EdgeCurveGroupId{};

    EdgePathPlacement result;
    result.instanceIds.reserve(edges.size());
    std::vector<BuildingChangeEvent> events;
    events.reserve(edges.size());
    for (const EdgeAddress &edge : edges) {
        PlacedBuilding placed;
        placed.instanceId = nextInstanceId();
        placed.buildingId = preview.buildingId;
        placed.placementKind = "edge";
        placed.edge = edge;
        placed.edgeCurveGroupId = curveGroupId;
        placed.originCellX = edge.x;
        placed.originCellY = edge.y;
        placed.level = preview.level;
        placed.channel = def->channel;
        placed.tags = def->tags;
        float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
        world->cellToWorldPlane(edge.x, edge.y, ax, ay);
        world->cellToWorldPlane(edge.x + (edge.axis == EdgeAxis::Horizontal ? 1 : 0),
                                edge.y + (edge.axis == EdgeAxis::Vertical ? 1 : 0), bx, by);
        placed.worldX = (ax + bx) * 0.5f;
        placed.worldY = (ay + by) * 0.5f;
        placed.rotationDeg = edge.axis == EdgeAxis::Horizontal ? 0.f : 90.f;

        auto &channels = world->edgeChannels(edge.axis, placed.level);
        auto &occupancy = channels[def->channel];
        const size_t required = edge.axis == EdgeAxis::Horizontal
                                    ? size_t(world->width_) * size_t(world->height_ + 1)
                                    : size_t(world->width_ + 1) * size_t(world->height_);
        if (occupancy.size() != required) occupancy.assign(required, 0);
        const size_t index = edge.axis == EdgeAxis::Horizontal
                                 ? size_t(edge.y) * size_t(world->width_) + size_t(edge.x)
                                 : size_t(edge.y) * size_t(world->width_ + 1) + size_t(edge.x);
        occupancy[index] = placed.instanceId;
        world->buildings()[placed.instanceId] = placed;
        world->instanceOrder_.push_back(placed.instanceId);
        result.instanceIds.push_back(placed.instanceId);

        BuildingChangeEvent event;
        event.action = "place";
        event.worldId = world->getId();
        event.buildingId = preview.buildingId;
        event.instanceId = placed.instanceId;
        event.cellX = edge.x;
        event.cellY = edge.y;
        event.level = placed.level;
        event.rotationDeg = placed.rotationDeg;
        event.worldX = placed.worldX;
        event.worldY = placed.worldY;
        event.channel = placed.channel;
        events.push_back(std::move(event));
    }
    if (curveControls) {
        EdgeCurveGroup group;
        group.id = curveGroupId;
        group.buildingId = preview.buildingId;
        group.level = preview.level;
        group.controlPoints = *curveControls;
        group.subdivisions = curveSubdivisions;
        group.instanceIds = result.instanceIds;
        if (curveSurface) {
            group.surfaceProviderName = curveSurface->providerName;
            group.surfaceId = curveSurface->surfaceId;
            group.surfaceRevision = curveSurface->surfaceRevision;
            group.surfaceSamples = curveSurface->samples;
        }
        world->edgeCurveGroups_.emplace(group.id.value, std::move(group));
    }
    if (world->publishEvents_)
        for (BuildingChangeEvent &event : events) emit(std::move(event));
    return eve::Result<EdgePathPlacement>::success(std::move(result));
}

eve::Result<PlacementSystem::AreaPreview>
PlacementSystem::previewArea(PlacementWorld *world, const std::string &buildingId,
                             const std::vector<std::pair<int, int>> &anchors,
                             float rotationDeg) {
    if (!world || anchors.empty()) {
        return eve::Result<AreaPreview>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "area preview requires a world and anchors",
            buildingId, {}, "building.area"));
    }
    const BuildingDefinition *def = BuildingRegistry::find(buildingId);
    if (!def || def->placementKind == "edge") {
        return eve::Result<AreaPreview>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "area tools require a cell building definition",
            buildingId, {}, "building.area"));
    }
    AreaPreview preview;
    preview.buildingId = buildingId;
    preview.level = world->getActiveLevel();
    preview.rotationDeg = normalizeRotation(buildingId, rotationDeg);
    std::unordered_set<uint64_t> reserved;
    for (const auto &[x, y] : anchors) {
        AreaCellPreview cell{x, y, false, {}};
        if (!canPlace(world, buildingId, x, y, preview.rotationDeg, 0, &cell.reason)) {
            ++preview.rejectedCount;
            preview.cells.push_back(std::move(cell));
            continue;
        }
        bool overlaps = false;
        std::vector<uint64_t> footprint;
        foreachFootprintCell(*def, x, y, preview.rotationDeg, [&](int cx, int cy) {
            const uint64_t key = (uint64_t(uint32_t(cx)) << 32u) | uint32_t(cy);
            footprint.push_back(key);
            if (reserved.count(key) != 0) overlaps = true;
            return true;
        });
        if (overlaps) {
            cell.reason = "area_candidate_overlap";
            ++preview.rejectedCount;
        } else {
            cell.accepted = true;
            ++preview.acceptedCount;
            reserved.insert(footprint.begin(), footprint.end());
        }
        preview.cells.push_back(std::move(cell));
    }
    return eve::Result<AreaPreview>::success(std::move(preview));
}

eve::Result<PlacementSystem::AreaPreview>
PlacementSystem::previewRectangle(PlacementWorld *world, const std::string &buildingId,
                                  int minCellX, int minCellY, int maxCellX, int maxCellY,
                                  float rotationDeg) {
    if (minCellX > maxCellX) std::swap(minCellX, maxCellX);
    if (minCellY > maxCellY) std::swap(minCellY, maxCellY);
    std::vector<std::pair<int, int>> anchors;
    for (int y = minCellY; y <= maxCellY; ++y)
        for (int x = minCellX; x <= maxCellX; ++x) anchors.emplace_back(x, y);
    return previewArea(world, buildingId, anchors, rotationDeg);
}

eve::Result<PlacementSystem::AreaPreview>
PlacementSystem::previewBrush(PlacementWorld *world, const std::string &buildingId,
                              int centerCellX, int centerCellY, int radius, float rotationDeg) {
    if (radius < 0) {
        return eve::Result<AreaPreview>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "brush radius must be non-negative",
            std::to_string(radius), {}, "building.area"));
    }
    std::vector<std::pair<int, int>> anchors;
    for (int y = centerCellY - radius; y <= centerCellY + radius; ++y)
        for (int x = centerCellX - radius; x <= centerCellX + radius; ++x)
            if ((x - centerCellX) * (x - centerCellX) + (y - centerCellY) * (y - centerCellY) <=
                radius * radius)
                anchors.emplace_back(x, y);
    return previewArea(world, buildingId, anchors, rotationDeg);
}

eve::Result<PlacementSystem::AreaPreview>
PlacementSystem::previewRectangleOutline(PlacementWorld *world,
                                         const std::string &buildingId, int minCellX,
                                         int minCellY, int maxCellX, int maxCellY,
                                         float rotationDeg) {
    if (minCellX > maxCellX) std::swap(minCellX, maxCellX);
    if (minCellY > maxCellY) std::swap(minCellY, maxCellY);
    std::vector<std::pair<int, int>> anchors;
    for (int x = minCellX; x <= maxCellX; ++x) anchors.emplace_back(x, minCellY);
    if (maxCellY != minCellY)
        for (int x = minCellX; x <= maxCellX; ++x) anchors.emplace_back(x, maxCellY);
    for (int y = minCellY + 1; y < maxCellY; ++y) {
        anchors.emplace_back(minCellX, y);
        if (maxCellX != minCellX) anchors.emplace_back(maxCellX, y);
    }
    std::sort(anchors.begin(), anchors.end(), [](const auto &lhs, const auto &rhs) {
        return lhs.second == rhs.second ? lhs.first < rhs.first : lhs.second < rhs.second;
    });
    return previewArea(world, buildingId, anchors, rotationDeg);
}

eve::Result<PlacementSystem::PatternPreview>
PlacementSystem::previewPattern(PlacementWorld *world, const std::string &buildingId,
                                const PatternRequest &request) {
    const auto failure = [&](std::string message) {
        return eve::Result<PatternPreview>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, std::move(message), buildingId, {},
            "building.pattern"));
    };
    const auto corners = [&]() -> eve::Result<std::vector<CornerAddress>> {
        std::vector<CornerAddress> result;
        result.reserve(request.points.size());
        for (const EdgeCurvePoint &point : request.points) {
            if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
                std::abs(point.x - std::round(point.x)) > 0.0001f ||
                std::abs(point.y - std::round(point.y)) > 0.0001f)
                return eve::Result<std::vector<CornerAddress>>::failure(
                    eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument,
                        "discrete pattern points must be finite integer grid vertices",
                        buildingId, {}, "building.pattern"));
            result.push_back(
                {static_cast<int>(std::lround(point.x)), static_cast<int>(std::lround(point.y))});
        }
        return eve::Result<std::vector<CornerAddress>>::success(std::move(result));
    };

    PatternPreview preview;
    preview.request = request;
    switch (request.kind) {
        case PatternKind::EdgeLine:
        case PatternKind::EdgePath: {
            if ((request.kind == PatternKind::EdgeLine && request.points.size() != 2) ||
                (request.kind == PatternKind::EdgePath && request.points.size() < 2))
                return failure(request.kind == PatternKind::EdgeLine
                                   ? "edge-line pattern requires exactly two points"
                                   : "edge-path pattern requires at least two points");
            if (!request.surfaceName.empty())
                return failure("custom surfaces are currently supported only by cubic curves");
            auto vertices = corners();
            if (!vertices.ok())
                return eve::Result<PatternPreview>::failure(vertices.status());
            auto edge = previewEdgePath(world, buildingId, vertices.value());
            if (!edge.ok()) return eve::Result<PatternPreview>::failure(edge.status());
            preview.edge = std::move(edge).takeValue();
            break;
        }
        case PatternKind::EdgeCubicBezier: {
            if (request.points.size() != 4)
                return failure("cubic edge pattern requires exactly four control points");
            auto edge = previewEdgeCubicBezier(world, buildingId, request.points,
                                               request.subdivisions);
            if (!edge.ok()) return eve::Result<PatternPreview>::failure(edge.status());
            if (!request.surfaceName.empty()) {
                auto surface = sampleEdgeCurveSurface(*world, request.surfaceName,
                                                      request.points, request.subdivisions);
                if (!surface.ok())
                    return eve::Result<PatternPreview>::failure(surface.status());
            }
            preview.edge = std::move(edge).takeValue();
            break;
        }
        case PatternKind::RectangleFill:
        case PatternKind::RectangleOutline: {
            if (request.points.size() != 2)
                return failure("rectangle pattern requires exactly two corner points");
            if (!request.surfaceName.empty())
                return failure("area patterns do not accept a custom surface name");
            auto vertices = corners();
            if (!vertices.ok())
                return eve::Result<PatternPreview>::failure(vertices.status());
            const CornerAddress &a = vertices.value()[0];
            const CornerAddress &b = vertices.value()[1];
            auto area = request.kind == PatternKind::RectangleFill
                            ? previewRectangle(world, buildingId, a.x, a.y, b.x, b.y,
                                               request.rotationDeg)
                            : previewRectangleOutline(world, buildingId, a.x, a.y, b.x, b.y,
                                                      request.rotationDeg);
            if (!area.ok()) return eve::Result<PatternPreview>::failure(area.status());
            preview.area = std::move(area).takeValue();
            break;
        }
        case PatternKind::CircleBrush: {
            if (request.points.size() != 1)
                return failure("circle-brush pattern requires exactly one center point");
            if (!request.surfaceName.empty())
                return failure("area patterns do not accept a custom surface name");
            auto vertices = corners();
            if (!vertices.ok())
                return eve::Result<PatternPreview>::failure(vertices.status());
            auto area = previewBrush(world, buildingId, vertices.value()[0].x,
                                     vertices.value()[0].y, request.radius,
                                     request.rotationDeg);
            if (!area.ok()) return eve::Result<PatternPreview>::failure(area.status());
            preview.area = std::move(area).takeValue();
            break;
        }
    }
    return eve::Result<PatternPreview>::success(std::move(preview));
}

eve::Result<PlacementSystem::PatternPlacement>
PlacementSystem::placePattern(PlacementWorld *world, const std::string &buildingId,
                              const PatternRequest &request) {
    auto expanded = previewPattern(world, buildingId, request);
    if (!expanded.ok()) return eve::Result<PatternPlacement>::failure(expanded.status());
    PatternPlacement result;
    result.preview = std::move(expanded).takeValue();
    if (request.kind == PatternKind::EdgeLine || request.kind == PatternKind::EdgePath) {
        auto placed = commitEdgePath(world, result.preview.edge);
        if (!placed.ok()) return eve::Result<PatternPlacement>::failure(placed.status());
        result.instanceIds = std::move(placed).takeValue().instanceIds;
    } else if (request.kind == PatternKind::EdgeCubicBezier) {
        auto placed = [&]() -> eve::Result<EdgePathPlacement> {
            if (request.surfaceName.empty())
                return commitEdgePath(world, result.preview.edge,
                                      &result.preview.request.points,
                                      request.subdivisions);
            auto surface = sampleEdgeCurveSurface(*world, request.surfaceName,
                                                  result.preview.request.points,
                                                  request.subdivisions);
            if (!surface.ok())
                return eve::Result<EdgePathPlacement>::failure(surface.status());
            const EdgeCurveSurface frames = std::move(surface).takeValue();
            return commitEdgePath(world, result.preview.edge,
                                  &result.preview.request.points,
                                  request.subdivisions, &frames);
        }();
        if (!placed.ok()) return eve::Result<PatternPlacement>::failure(placed.status());
        result.instanceIds = std::move(placed).takeValue().instanceIds;
    } else {
        auto placed = commitArea(world, result.preview.area);
        if (!placed.ok()) return eve::Result<PatternPlacement>::failure(placed.status());
        result.instanceIds = std::move(placed).takeValue().instanceIds;
    }
    return eve::Result<PatternPlacement>::success(std::move(result));
}

eve::Result<PlacementSystem::AreaPlacement>
PlacementSystem::commitArea(PlacementWorld *world, AreaPreview preview) {
    if (!world || preview.rejectedCount != 0 || preview.cells.empty()) {
        return eve::Result<AreaPlacement>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "area commit requires a fully accepted preview",
            std::to_string(preview.rejectedCount), {}, "building.area"));
    }
    const BuildingDefinition *def = BuildingRegistry::find(preview.buildingId);
    if (!def) {
        return eve::Result<AreaPlacement>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "area definition was not found", preview.buildingId, {},
            "building.area"));
    }
    AreaPlacement placement;
    placement.preview = std::move(preview);
    std::vector<BuildingChangeEvent> events;
    for (const AreaCellPreview &cell : placement.preview.cells) {
        PlacedBuilding pb;
        pb.instanceId = nextInstanceId();
        pb.buildingId = placement.preview.buildingId;
        pb.originCellX = cell.cellX;
        pb.originCellY = cell.cellY;
        pb.level = placement.preview.level;
        pb.rotationDeg = placement.preview.rotationDeg;
        pb.channel = def->channel;
        pb.tags = def->tags;
        world->cellToWorldPlane(cell.cellX, cell.cellY, pb.worldX, pb.worldY);
        PlacementQuery supportQuery;
        supportQuery.cellX = pb.originCellX;
        supportQuery.cellY = pb.originCellY;
        supportQuery.level = pb.level;
        supportQuery.rotationDeg = pb.rotationDeg;
        pb.supportInstanceIds = collectStructuralSupports(*world, *def, supportQuery);
        writeOccupancy(*world, *def, pb, pb.instanceId);
        world->buildings()[pb.instanceId] = pb;
        world->instanceOrder_.push_back(pb.instanceId);
        placement.instanceIds.push_back(pb.instanceId);
        BuildingChangeEvent event;
        event.action = "place";
        event.worldId = world->getId();
        event.buildingId = pb.buildingId;
        event.instanceId = pb.instanceId;
        event.cellX = pb.originCellX;
        event.cellY = pb.originCellY;
        event.rotationDeg = pb.rotationDeg;
        event.worldX = pb.worldX;
        event.worldY = pb.worldY;
        event.channel = pb.channel;
        events.push_back(std::move(event));
    }
    if (world->publishEvents_)
        for (BuildingChangeEvent &event : events) emit(std::move(event));
    return eve::Result<AreaPlacement>::success(std::move(placement));
}

eve::Result<PlacementSystem::AreaPlacement>
PlacementSystem::placeRectangle(PlacementWorld *world, const std::string &buildingId,
                                int minCellX, int minCellY, int maxCellX, int maxCellY,
                                float rotationDeg) {
    auto preview = previewRectangle(world, buildingId, minCellX, minCellY, maxCellX, maxCellY,
                                    rotationDeg);
    if (!preview.ok()) return eve::Result<AreaPlacement>::failure(preview.status());
    return commitArea(world, std::move(preview).takeValue());
}

eve::Result<PlacementSystem::AreaPlacement>
PlacementSystem::placeBrush(PlacementWorld *world, const std::string &buildingId, int centerCellX,
                            int centerCellY, int radius, float rotationDeg) {
    auto preview = previewBrush(world, buildingId, centerCellX, centerCellY, radius, rotationDeg);
    if (!preview.ok()) return eve::Result<AreaPlacement>::failure(preview.status());
    return commitArea(world, std::move(preview).takeValue());
}

eve::Result<PlacementSystem::AreaPlacement>
PlacementSystem::placeRectangleOutline(PlacementWorld *world,
                                       const std::string &buildingId, int minCellX,
                                       int minCellY, int maxCellX, int maxCellY,
                                       float rotationDeg) {
    auto preview = previewRectangleOutline(world, buildingId, minCellX, minCellY,
                                           maxCellX, maxCellY, rotationDeg);
    if (!preview.ok()) return eve::Result<AreaPlacement>::failure(preview.status());
    return commitArea(world, std::move(preview).takeValue());
}

void PlacementSystem::writeOccupancy(PlacementWorld &world, const BuildingDefinition &def,
                                     const PlacedBuilding &placed, int instanceId) {
    std::vector<int> &occ = world.channelOccupancy(def.channel, placed.level);
    foreachFootprintCell(def, placed.originCellX, placed.originCellY, placed.rotationDeg,
                         [&](int cx, int cy) {
                             if (world.inBounds(cx, cy)) {
                                 const size_t idx =
                                     size_t(cy) * size_t(world.getWidth()) + size_t(cx);
                                 if (idx < occ.size()) occ[idx] = instanceId;
                             }
                             return true;
                         });
}

void PlacementSystem::clearOccupancy(PlacementWorld &world, int instanceId) {
    // 默认通道（""）占用存于 occupancy_，附加通道存于 allChannels_，两者都要清。
    for (int &v : world.occupancy()) {
        if (v == instanceId) v = 0;
    }
    for (auto &kv : world.allChannels()) {
        for (int &v : kv.second) {
            if (v == instanceId) v = 0;
        }
    }
    for (auto &kv : world.horizontalEdgeChannels_) {
        for (int &v : kv.second)
            if (v == instanceId) v = 0;
    }
    for (auto &kv : world.verticalEdgeChannels_) {
        for (int &v : kv.second)
            if (v == instanceId) v = 0;
    }
    for (auto &kv : world.cornerChannels_) {
        for (int &v : kv.second)
            if (v == instanceId) v = 0;
    }
    for (auto &level : world.cellChannelsByLevel_)
        for (auto &channel : level.second)
            for (int &value : channel.second)
                if (value == instanceId) value = 0;
    for (auto &levels : {&world.horizontalEdgesByLevel_, &world.verticalEdgesByLevel_})
        for (auto &level : *levels)
            for (auto &channel : level.second)
                for (int &value : channel.second)
                    if (value == instanceId) value = 0;
    for (auto &level : world.cornersByLevel_)
        for (auto &channel : level.second)
            for (int &value : channel.second)
                if (value == instanceId) value = 0;
}

int PlacementSystem::placeAt(PlacementWorld *world, const std::string &buildingId, int cellX,
                             int cellY, float rotationDeg) {
    ensureBuiltins();
    if (!world) return 0;
    const BuildingDefinition *def = BuildingRegistry::find(buildingId);
    if (!def) return 0;
    if (def->placementKind != "cell") return 0;
    const float rot = normalizeRotation(buildingId, rotationDeg);
    if (!canPlace(world, buildingId, cellX, cellY, rot, 0, nullptr)) return 0;

    PlacedBuilding pb;
    pb.instanceId = nextInstanceId();
    pb.buildingId = buildingId;
    pb.originCellX = cellX;
    pb.originCellY = cellY;
    pb.level = world->getActiveLevel();
    world->cellToWorldPlane(cellX, cellY, pb.worldX, pb.worldY);
    pb.elevation = 0.f;
    pb.rotationDeg = rot;
    pb.channel = def->channel;
    pb.tags = def->tags;
    PlacementQuery supportQuery;
    supportQuery.cellX = pb.originCellX;
    supportQuery.cellY = pb.originCellY;
    supportQuery.level = pb.level;
    supportQuery.rotationDeg = pb.rotationDeg;
    pb.supportInstanceIds = collectStructuralSupports(*world, *def, supportQuery);

    writeOccupancy(*world, *def, pb, pb.instanceId);
    world->buildings()[pb.instanceId] = pb;
    world->instanceOrder_.push_back(pb.instanceId);

    BuildingChangeEvent ev;
    ev.action = "place";
    ev.worldId = world->getId();
    ev.buildingId = buildingId;
    ev.instanceId = pb.instanceId;
    ev.cellX = cellX;
    ev.cellY = cellY;
    ev.rotationDeg = rot;
    ev.worldX = pb.worldX;
    ev.worldY = pb.worldY;
    ev.elevation = pb.elevation;
    ev.channel = pb.channel;
    if (world->publishEvents_) emit(std::move(ev));
    return pb.instanceId;
}

int PlacementSystem::placeAtWorld(PlacementWorld *world, const std::string &buildingId,
                                  float worldX, float worldY, float rotationDeg) {
    ensureBuiltins();
    if (!world) return 0;
    const SnapResult s = snap(*world, buildingId, worldX, worldY);
    const int id = placeAt(world, buildingId, s.cellX, s.cellY, rotationDeg);
    if (id <= 0) return 0;
    // Preserve free / custom snap world pose (cell placement alone snaps to cell origin).
    auto it = world->buildings().find(id);
    if (it != world->buildings().end()) {
        it->second.worldX = s.worldX;
        it->second.worldY = s.worldY;
    }
    return id;
}

int PlacementSystem::placeAtWorld3D(PlacementWorld *world, const std::string &buildingId,
                                    float worldX, float worldY, float worldZ,
                                    float rotationDeg) {
    ensureBuiltins();
    if (!world) return 0;
    const SnapResult s = snap3D(*world, buildingId, worldX, worldY, worldZ);
    const int id = placeAt(world, buildingId, s.cellX, s.cellY, rotationDeg);
    if (id <= 0) return 0;
    auto it = world->buildings().find(id);
    if (it != world->buildings().end()) {
        it->second.worldX = s.worldX;
        it->second.worldY = s.worldY;
        it->second.elevation = s.elevation;
    }
    return id;
}

int PlacementSystem::placeGhost(PlacementWorld *world, Ghost *ghost) {
    if (!world || !ghost) return 0;
    if (!ghost->validate(world)) return 0;
    const BuildingDefinition *def = BuildingRegistry::find(ghost->getBuildingId());
    if (!def) return 0;
    if (def->placementKind == "edge") {
        const char *direction = ghost->edge_.axis == EdgeAxis::Horizontal ? "north" : "west";
        return placeEdge(world, ghost->getBuildingId(), ghost->edge_.x, ghost->edge_.y, direction);
    }
    if (def->placementKind == "corner") {
        if (ghost->placementKind_ != "corner") return 0;
        return placeCorner(world, ghost->getBuildingId(), ghost->corner_.x, ghost->corner_.y);
    }
    if (def->placementKind == "free") {
        if (ghost->placementKind_ != "free") return 0;
        if (!ghost->surfaceId_.empty()) {
            SurfacePatch patch;
            if (world->getGrid().plane == grid::GridPlane::XZ) {
                patch.anchor.worldX = ghost->worldX_;
                patch.anchor.worldY = ghost->elevation_;
                patch.anchor.worldZ = ghost->worldY_;
            } else {
                patch.anchor.worldX = ghost->worldX_;
                patch.anchor.worldY = ghost->worldY_;
                patch.anchor.worldZ = ghost->elevation_;
            }
            patch.anchor.surfaceId = ghost->surfaceId_;
            patch.anchor.surfaceRevision = ghost->surfaceRevision_;
            patch.anchor.normalX = ghost->surfaceNormalX_;
            patch.anchor.normalY = ghost->surfaceNormalY_;
            patch.anchor.normalZ = ghost->surfaceNormalZ_;
            patch.anchor.tangentX = ghost->surfaceTangentX_;
            patch.anchor.tangentY = ghost->surfaceTangentY_;
            patch.anchor.tangentZ = ghost->surfaceTangentZ_;
            patch.samples.resize(static_cast<size_t>(std::max(ghost->surfaceSampleCount_, 0)));
            patch.maxSlopeDegrees = ghost->surfaceMaxSlopeDegrees_;
            patch.heightDelta = ghost->surfaceHeightDelta_;
            auto result = placeFreeSurfaceResult(world, ghost->getBuildingId(), patch,
                                                 ghost->rotationDeg_);
            return result.ok() ? std::move(result).takeValue().instanceId : 0;
        }
        return placeFree(world, ghost->getBuildingId(), ghost->worldX_, ghost->worldY_,
                         ghost->elevation_, ghost->rotationDeg_);
    }

    PlacedBuilding placed;
    placed.instanceId = nextInstanceId();
    placed.buildingId = ghost->getBuildingId();
    placed.originCellX = ghost->getCellX();
    placed.originCellY = ghost->getCellY();
    placed.level = world->getActiveLevel();
    placed.worldX = ghost->getWorldX();
    placed.worldY = ghost->getWorldY();
    placed.elevation = ghost->getElevation();
    placed.rotationDeg = ghost->getRotationDeg();
    placed.channel = def->channel;
    placed.tags = def->tags;
    placed.surfaceId = ghost->surfaceId_;
    placed.surfaceRevision = ghost->surfaceRevision_;
    placed.surfaceNormalX = ghost->surfaceNormalX_;
    placed.surfaceNormalY = ghost->surfaceNormalY_;
    placed.surfaceNormalZ = ghost->surfaceNormalZ_;
    placed.surfaceTangentX = ghost->surfaceTangentX_;
    placed.surfaceTangentY = ghost->surfaceTangentY_;
    placed.surfaceTangentZ = ghost->surfaceTangentZ_;
    placed.surfaceSampleCount = ghost->surfaceSampleCount_;
    placed.surfaceMaxSlopeDegrees = ghost->surfaceMaxSlopeDegrees_;
    placed.surfaceHeightDelta = ghost->surfaceHeightDelta_;
    PlacementQuery supportQuery;
    supportQuery.cellX = placed.originCellX;
    supportQuery.cellY = placed.originCellY;
    supportQuery.level = placed.level;
    supportQuery.rotationDeg = placed.rotationDeg;
    placed.supportInstanceIds = collectStructuralSupports(*world, *def, supportQuery);

    writeOccupancy(*world, *def, placed, placed.instanceId);
    world->buildings()[placed.instanceId] = placed;
    world->instanceOrder_.push_back(placed.instanceId);

    BuildingChangeEvent event;
    event.action = "place";
    event.worldId = world->getId();
    event.buildingId = placed.buildingId;
    event.instanceId = placed.instanceId;
    event.cellX = placed.originCellX;
    event.cellY = placed.originCellY;
    event.rotationDeg = placed.rotationDeg;
    event.worldX = placed.worldX;
    event.worldY = placed.worldY;
    event.elevation = placed.elevation;
    event.channel = placed.channel;
    if (world->publishEvents_) emit(std::move(event));
    return placed.instanceId;
}

PlacementRestoreStatus PlacementSystem::restoreExact(PlacementWorld *world,
                                                      const PlacedBuilding &placed,
                                                      std::string *reason) {
    ensureBuiltins();
    if (!world || placed.instanceId <= 0 || world->hasBuilding(placed.instanceId)) {
        if (reason) *reason = "instance_id_conflict";
        return PlacementRestoreStatus::Rejected;
    }
    const BuildingDefinition *def = BuildingRegistry::find(placed.buildingId);
    if (!def) {
        if (reason) *reason = "unknown_building";
        return PlacementRestoreStatus::Rejected;
    }
    if (def->placementKind != placed.placementKind) {
        if (reason) *reason = "placement_domain_mismatch";
        return PlacementRestoreStatus::Rejected;
    }
    if (def->placementKind == "free") {
        if (!canPlaceFree(world, placed.buildingId, placed.worldX, placed.worldY, 0, reason,
                          placed.level, placed.rotationDeg))
            return PlacementRestoreStatus::Rejected;
        PlacedBuilding restored = placed;
        restored.placementKind = "free";
        applyDefinitionFreeFootprint(restored, *def, *world);
        grid::worldToCell(*world->grid_, restored.worldX, restored.worldY,
                          restored.originCellX, restored.originCellY, world->width_,
                          world->height_);
        restored.supportInstanceIds.clear();
        world->buildings_[restored.instanceId] = restored;
        world->instanceOrder_.push_back(restored.instanceId);
        instanceCounter() = std::max(instanceCounter(), restored.instanceId);
        BuildingChangeEvent event;
        event.action = "place";
        event.worldId = world->getId();
        event.buildingId = restored.buildingId;
        event.instanceId = restored.instanceId;
        event.cellX = restored.originCellX;
        event.cellY = restored.originCellY;
        event.level = restored.level;
        event.rotationDeg = restored.rotationDeg;
        event.worldX = restored.worldX;
        event.worldY = restored.worldY;
        event.elevation = restored.elevation;
        event.channel = restored.channel;
        if (world->publishEvents_) emit(std::move(event));
        return PlacementRestoreStatus::Restored;
    }
    if (def->placementKind == "corner" || placed.placementKind == "corner") {
        if (def->placementKind != "corner" || placed.placementKind != "corner") {
            if (reason) *reason = "placement_domain_mismatch";
            return PlacementRestoreStatus::Rejected;
        }
        if (!canPlaceCorner(world, placed.buildingId, placed.corner.x, placed.corner.y, 0,
                            reason, placed.level))
            return PlacementRestoreStatus::Rejected;
        PlacedBuilding restored = placed;
        restored.originCellX = restored.corner.x;
        restored.originCellY = restored.corner.y;
        PlacementQuery query;
        query.cellX = restored.corner.x;
        query.cellY = restored.corner.y;
        query.level = restored.level;
        restored.supportInstanceIds = collectStructuralSupports(*world, *def, query);
        auto &occupancy = world->cornerChannels(restored.level)[def->channel];
        const size_t required = size_t(world->width_ + 1) * size_t(world->height_ + 1);
        if (occupancy.size() != required) occupancy.assign(required, 0);
        occupancy[size_t(restored.corner.y) * size_t(world->width_ + 1) +
                  size_t(restored.corner.x)] = restored.instanceId;
        world->buildings_[restored.instanceId] = restored;
        world->instanceOrder_.push_back(restored.instanceId);
        instanceCounter() = std::max(instanceCounter(), restored.instanceId);
        BuildingChangeEvent event;
        event.action = "place";
        event.worldId = world->getId();
        event.buildingId = restored.buildingId;
        event.instanceId = restored.instanceId;
        event.cellX = restored.corner.x;
        event.cellY = restored.corner.y;
        event.level = restored.level;
        event.worldX = restored.worldX;
        event.worldY = restored.worldY;
        event.elevation = restored.elevation;
        event.channel = restored.channel;
        if (world->publishEvents_) emit(std::move(event));
        return PlacementRestoreStatus::Restored;
    }
    if (def->placementKind == "edge" || placed.placementKind == "edge") {
        const char *direction = placed.edge.axis == EdgeAxis::Horizontal ? "north" : "west";
        if (!canPlaceEdge(world, placed.buildingId, placed.edge.x, placed.edge.y, direction, 0,
                          reason, placed.level))
            return PlacementRestoreStatus::Rejected;
        PlacedBuilding restored = placed;
        restored.placementKind = "edge";
        const auto curveGroup = world->edgeCurveGroups_.find(restored.edgeCurveGroupId.value);
        if (!restored.edgeCurveGroupId || curveGroup == world->edgeCurveGroups_.end() ||
            std::find(curveGroup->second.instanceIds.begin(), curveGroup->second.instanceIds.end(),
                      restored.instanceId) == curveGroup->second.instanceIds.end())
            restored.edgeCurveGroupId = {};
        // Support links are a derived cell-domain cache and must never be trusted from a snapshot.
        restored.supportInstanceIds.clear();
        auto &channels = world->edgeChannels(restored.edge.axis, restored.level);
        auto &occupancy = channels[def->channel];
        const size_t required = restored.edge.axis == EdgeAxis::Horizontal
                                    ? size_t(world->width_) * size_t(world->height_ + 1)
                                    : size_t(world->width_ + 1) * size_t(world->height_);
        if (occupancy.size() != required) occupancy.assign(required, 0);
        const size_t index = restored.edge.axis == EdgeAxis::Horizontal
                                 ? size_t(restored.edge.y) * size_t(world->width_) + size_t(restored.edge.x)
                                 : size_t(restored.edge.y) * size_t(world->width_ + 1) + size_t(restored.edge.x);
        occupancy[index] = restored.instanceId;
        world->buildings()[restored.instanceId] = restored;
        world->instanceOrder_.push_back(restored.instanceId);
        instanceCounter() = std::max(instanceCounter(), restored.instanceId);
        BuildingChangeEvent event;
        event.action = "place";
        event.worldId = world->getId();
        event.buildingId = restored.buildingId;
        event.instanceId = restored.instanceId;
        event.cellX = restored.edge.x;
        event.cellY = restored.edge.y;
        event.rotationDeg = restored.rotationDeg;
        event.worldX = restored.worldX;
        event.worldY = restored.worldY;
        event.elevation = restored.elevation;
        event.channel = restored.channel;
        if (world->publishEvents_) emit(std::move(event));
        return PlacementRestoreStatus::Restored;
    }
    const float rotation = normalizeRotation(placed.buildingId, placed.rotationDeg);
    if (!canPlaceElev(world, placed.buildingId, placed.originCellX, placed.originCellY,
                      placed.elevation, rotation, 0, reason, placed.level))
        return PlacementRestoreStatus::Rejected;
    PlacedBuilding restored = placed;
    restored.rotationDeg = rotation;
    restored.channel = def->channel;
    if (restored.tags.empty()) restored.tags = def->tags;
    PlacementQuery supportQuery;
    supportQuery.cellX = restored.originCellX;
    supportQuery.cellY = restored.originCellY;
    supportQuery.level = restored.level;
    supportQuery.rotationDeg = restored.rotationDeg;
    restored.supportInstanceIds = collectStructuralSupports(*world, *def, supportQuery);
    writeOccupancy(*world, *def, restored, restored.instanceId);
    world->buildings()[restored.instanceId] = restored;
    world->instanceOrder_.push_back(restored.instanceId);
    instanceCounter() = std::max(instanceCounter(), restored.instanceId);

    BuildingChangeEvent event;
    event.action = "place";
    event.worldId = world->getId();
    event.buildingId = restored.buildingId;
    event.instanceId = restored.instanceId;
    event.cellX = restored.originCellX;
    event.cellY = restored.originCellY;
    event.rotationDeg = restored.rotationDeg;
    event.worldX = restored.worldX;
    event.worldY = restored.worldY;
    event.elevation = restored.elevation;
    event.channel = restored.channel;
    if (world->publishEvents_) emit(std::move(event));
    return PlacementRestoreStatus::Restored;
}

eve::Result<EdgeCurveGroup> PlacementSystem::restoreEdgeCurveGroupExact(
    PlacementWorld *world, const EdgeCurveGroup &group,
    const std::vector<PlacedBuilding> &members) {
    if (!world || !group.id || group.controlPoints.size() != 4 || group.subdivisions < 2 ||
        group.subdivisions > 4096 || members.empty() ||
        group.instanceIds.size() != members.size()) {
        return eve::Result<EdgeCurveGroup>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "curve group restore requires a world, identity, four controls, valid subdivisions and matching members",
            {}, {}, "building.edge-curve-group.restore"));
    }
    std::unordered_set<int> memberIds;
    for (size_t index = 0; index < members.size(); ++index) {
        const PlacedBuilding &member = members[index];
        if (member.instanceId <= 0 || member.instanceId != group.instanceIds[index] ||
            member.placementKind != "edge" || member.buildingId != group.buildingId ||
            member.level != group.level || !memberIds.insert(member.instanceId).second) {
            return eve::Result<EdgeCurveGroup>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument,
                "curve group members must be unique ordered edges with matching definition and level",
                std::to_string(member.instanceId), {}, "building.edge-curve-group.restore"));
        }
    }
    for (const EdgeCurveControlPoint &point : group.controlPoints) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
            return eve::Result<EdgeCurveGroup>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "curve group controls must be finite", {},
                {}, "building.edge-curve-group.restore"));
        }
    }
    if ((!group.surfaceSamples.empty() &&
         group.surfaceSamples.size() != static_cast<size_t>(group.subdivisions + 1)) ||
        (group.surfaceSamples.empty() &&
         (!group.surfaceProviderName.empty() || !group.surfaceId.empty() ||
          group.surfaceRevision != 0))) {
        return eve::Result<EdgeCurveGroup>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "curve group surface metadata requires exactly subdivisions plus one frames", {},
            {}, "building.edge-curve-group.restore"));
    }
    for (const EdgeCurveSurfaceSample &sample : group.surfaceSamples) {
        const float normalLength = std::sqrt(sample.normalX * sample.normalX +
                                             sample.normalY * sample.normalY +
                                             sample.normalZ * sample.normalZ);
        if (!std::isfinite(sample.worldX) || !std::isfinite(sample.worldY) ||
            !std::isfinite(sample.worldZ) || !std::isfinite(normalLength) ||
            normalLength <= 1e-5f) {
            return eve::Result<EdgeCurveGroup>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument,
                "curve group surface frames require finite positions and non-zero normals", {},
                {}, "building.edge-curve-group.restore"));
        }
    }

    std::unique_ptr<PlacementWorld> candidate = world->cloneState();
    if (!candidate || candidate->edgeCurveGroups_.contains(group.id.value)) {
        return eve::Result<EdgeCurveGroup>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "curve group identity is already in use",
            std::to_string(group.id.value), {}, "building.edge-curve-group.restore"));
    }
    for (const PlacedBuilding &member : members) {
        std::string reason;
        if (restoreExact(candidate.get(), member, &reason) != PlacementRestoreStatus::Restored) {
            return eve::Result<EdgeCurveGroup>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict,
                "curve group member restore was rejected without destination mutation", reason,
                {}, "building.edge-curve-group.restore"));
        }
    }
    for (int instanceId : group.instanceIds)
        candidate->buildings_.at(instanceId).edgeCurveGroupId = group.id;
    candidate->edgeCurveGroups_.emplace(group.id.value, group);
    candidate->nextEdgeCurveGroupId_ =
        std::max(candidate->nextEdgeCurveGroupId_, group.id.value + 1);
    world->swapState(*candidate);
    return eve::Result<EdgeCurveGroup>::success(group);
}

void PlacementSystem::dissolveEdgeCurveGroup(PlacementWorld *world, EdgeCurveGroupId groupId) {
    if (!world || !groupId) return;
    const auto group = world->edgeCurveGroups_.find(groupId.value);
    if (group == world->edgeCurveGroups_.end()) return;
    for (int memberId : group->second.instanceIds) {
        const auto member = world->buildings_.find(memberId);
        if (member != world->buildings_.end() && member->second.edgeCurveGroupId == groupId)
            member->second.edgeCurveGroupId = {};
    }
    world->edgeCurveGroups_.erase(group);
}

bool PlacementSystem::removeBuildingUnchecked(PlacementWorld *world, int instanceId) {
    if (!world || instanceId <= 0) return false;
    auto it = world->buildings().find(instanceId);
    if (it == world->buildings().end()) return false;

    const EdgeCurveGroupId curveGroupId = it->second.edgeCurveGroupId;
    BuildingChangeEvent ev;
    ev.action = "remove";
    ev.worldId = world->getId();
    ev.buildingId = it->second.buildingId;
    ev.instanceId = instanceId;
    ev.cellX = it->second.originCellX;
    ev.cellY = it->second.originCellY;
    ev.rotationDeg = it->second.rotationDeg;
    ev.worldX = it->second.worldX;
    ev.worldY = it->second.worldY;
    ev.elevation = it->second.elevation;
    ev.channel = it->second.channel;

    dissolveEdgeCurveGroup(world, curveGroupId);
    clearOccupancy(*world, instanceId);
    world->buildings().erase(it);
    auto &order = world->instanceOrder_;
    order.erase(std::remove(order.begin(), order.end(), instanceId), order.end());
    if (world->publishEvents_) emit(std::move(ev));
    return true;
}

bool PlacementSystem::moveBuilding(PlacementWorld *world, int instanceId, int cellX, int cellY,
                                   float rotationDeg) {
    auto result = moveBuildingResult(world, instanceId, cellX, cellY, rotationDeg);
    return result.ok();
}

bool PlacementSystem::removeBuilding(PlacementWorld *world, int instanceId) {
    if (!world || !world->hasBuilding(instanceId) || hasStructuralDependents(*world, instanceId))
        return false;
    return removeBuildingUnchecked(world, instanceId);
}

eve::Result<StructuralRemovalReceipt>
PlacementSystem::removeBuildingCascadeResult(PlacementWorld *world, int instanceId) {
    if (!world || instanceId <= 0) {
        return eve::Result<StructuralRemovalReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "cascade removal requires a world and positive instance id",
            std::to_string(instanceId), {}, "building.structure"));
    }
    if (!world->hasBuilding(instanceId)) {
        return eve::Result<StructuralRemovalReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "cascade removal source instance was not found",
            std::to_string(instanceId), {}, "building.structure"));
    }
    std::vector<int> ordered;
    std::unordered_set<int> visiting;
    std::unordered_set<int> visited;
    bool cycle = false;
    std::function<void(int)> collect = [&](int supportId) {
        if (visited.contains(supportId)) return;
        if (!visiting.insert(supportId).second) {
            cycle = true;
            return;
        }
        for (const auto &[candidateId, placed] : world->buildings_) {
            if (std::find(placed.supportInstanceIds.begin(), placed.supportInstanceIds.end(),
                          supportId) != placed.supportInstanceIds.end())
                collect(candidateId);
        }
        visiting.erase(supportId);
        visited.insert(supportId);
        ordered.push_back(supportId);
    };
    collect(instanceId);
    if (cycle) {
        return eve::Result<StructuralRemovalReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "structural dependency cycle prevents removal",
            "support_cycle", {}, "building.structure"));
    }

    StructuralRemovalReceipt receipt;
    receipt.removed.reserve(ordered.size());
    for (int id : ordered) receipt.removed.push_back(world->buildings_.at(id));
    const bool publishEvents = world->publishEvents_;
    world->publishEvents_ = false;
    for (int id : ordered) removeBuildingUnchecked(world, id);
    world->publishEvents_ = publishEvents;
    if (publishEvents) {
        for (const PlacedBuilding &removed : receipt.removed) {
            BuildingChangeEvent event;
            event.action = "remove";
            event.worldId = world->getId();
            event.buildingId = removed.buildingId;
            event.instanceId = removed.instanceId;
            event.cellX = removed.originCellX;
            event.cellY = removed.originCellY;
            event.level = removed.level;
            event.rotationDeg = removed.rotationDeg;
            event.worldX = removed.worldX;
            event.worldY = removed.worldY;
            event.elevation = removed.elevation;
            event.channel = removed.channel;
            emit(std::move(event));
        }
    }
    return eve::Result<StructuralRemovalReceipt>::success(std::move(receipt));
}

int PlacementSystem::removeBuildingCascade(PlacementWorld *world, int instanceId) {
    auto result = removeBuildingCascadeResult(world, instanceId);
    return result.ok() ? int(result.value().removed.size()) : 0;
}

eve::Result<StructuralLinkRebuildReceipt>
PlacementSystem::rebuildStructuralLinksResult(PlacementWorld *world) {
    if (!world) {
        return eve::Result<StructuralLinkRebuildReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "structural rebuild requires a world", {}, {},
            "building.structure"));
    }
    auto candidate = world->cloneState();
    if (!candidate) {
        return eve::Result<StructuralLinkRebuildReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation,
            "failed to clone structural rebuild candidate", {}, {},
            "building.structure"));
    }

    StructuralLinkRebuildReceipt receipt;
    for (int id : candidate->instanceOrder_) {
        auto placedIt = candidate->buildings_.find(id);
        if (placedIt == candidate->buildings_.end()) continue;
        PlacedBuilding &placed = placedIt->second;
        ++receipt.inspectedCount;
        const BuildingDefinition *def = BuildingRegistry::find(placed.buildingId);
        if (!def) {
            return eve::Result<StructuralLinkRebuildReceipt>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::NotFound,
                "structural rebuild encountered an unknown building definition",
                std::to_string(id), {}, "building.structure"));
        }

        std::vector<int> rebuilt;
        if (def->supportMode != "none") {
            PlacementQuery query;
            query.buildingId = placed.buildingId;
            query.cellX = placed.originCellX;
            query.cellY = placed.originCellY;
            query.level = placed.level;
            query.rotationDeg = placed.rotationDeg;
            std::string reason;
            if (!checkStructuralSupport(*candidate, *def, query, &reason)) {
                return eve::Result<StructuralLinkRebuildReceipt>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                           "structural rebuild found an invalid dependency",
                                           std::to_string(id) + ":" + reason, {},
                                           "building.structure"));
            }
            rebuilt = collectStructuralSupports(*candidate, *def, query);
        }
        if (rebuilt != placed.supportInstanceIds) {
            placed.supportInstanceIds = std::move(rebuilt);
            ++receipt.changedCount;
        }
    }
    world->swapState(*candidate);
    return eve::Result<StructuralLinkRebuildReceipt>::success(std::move(receipt));
}

eve::Result<PlacementEditReceipt>
PlacementSystem::moveBuildingResult(PlacementWorld *world, int instanceId, int cellX, int cellY,
                                    float rotationDeg) {
    if (!world || instanceId <= 0) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "move requires a world and positive instance id",
            std::to_string(instanceId), {}, "building.edit"));
    }
    auto it = world->buildings().find(instanceId);
    if (it == world->buildings().end()) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "move source instance was not found",
            std::to_string(instanceId), {}, "building.edit"));
    }

    PlacedBuilding &pb = it->second;
    if (hasStructuralDependents(*world, instanceId)) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "move would invalidate structural dependents",
            "support_in_use", {}, "building.structure"));
    }
    if (pb.placementKind != "cell") {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "cell move cannot target a non-cell instance",
            std::to_string(instanceId), {}, "building.edit"));
    }
    const float rot =
        (rotationDeg < 0.f) ? pb.rotationDeg : normalizeRotation(pb.buildingId, rotationDeg);
    std::string reason;
    if (!canPlaceElev(world, pb.buildingId, cellX, cellY, pb.elevation, rot, instanceId, &reason,
                      pb.level)) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "move preflight rejected without mutation", reason, {},
            "building.edit"));
    }

    const BuildingDefinition *def = BuildingRegistry::find(pb.buildingId);
    if (!def) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "move definition was not found", pb.buildingId, {},
            "building.edit"));
    }
    PlacementEditReceipt receipt;
    receipt.before = pb;

    BuildingChangeEvent ev;
    ev.action = "move";
    ev.worldId = world->getId();
    ev.buildingId = pb.buildingId;
    ev.instanceId = instanceId;
    ev.otherCellX = pb.originCellX;
    ev.otherCellY = pb.originCellY;
    ev.cellX = cellX;
    ev.cellY = cellY;
    ev.rotationDeg = rot;
    float px = 0.f, py = 0.f;
    world->cellToWorldPlane(cellX, cellY, px, py);
    ev.worldX = px;
    ev.worldY = py;
    ev.elevation = pb.elevation;
    ev.channel = pb.channel;

    clearOccupancy(*world, instanceId);
    pb.originCellX = cellX;
    pb.originCellY = cellY;
    world->cellToWorldPlane(cellX, cellY, pb.worldX, pb.worldY);
    pb.rotationDeg = rot;
    PlacementQuery supportQuery;
    supportQuery.cellX = pb.originCellX;
    supportQuery.cellY = pb.originCellY;
    supportQuery.level = pb.level;
    supportQuery.rotationDeg = pb.rotationDeg;
    pb.supportInstanceIds = collectStructuralSupports(*world, *def, supportQuery);
    writeOccupancy(*world, *def, pb, instanceId);
    receipt.after = pb;
    if (world->publishEvents_) emit(std::move(ev));
    return eve::Result<PlacementEditReceipt>::success(std::move(receipt));
}

eve::Result<PlacementEditReceipt>
PlacementSystem::moveEdgeResult(PlacementWorld *world, int instanceId, int cellX, int cellY,
                                const std::string &direction) {
    if (!world || instanceId <= 0) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "edge move requires a world and positive instance id", std::to_string(instanceId),
            {}, "building.edit"));
    }
    auto it = world->buildings().find(instanceId);
    if (it == world->buildings().end()) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "edge move source instance was not found",
            std::to_string(instanceId), {}, "building.edit"));
    }
    PlacedBuilding &pb = it->second;
    if (pb.placementKind != "edge") {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "edge move requires an edge instance",
            std::to_string(instanceId), {}, "building.edit"));
    }
    if (hasStructuralDependents(*world, instanceId)) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "edge move would invalidate structural dependents",
            "support_in_use", {}, "building.structure"));
    }
    std::string reason;
    if (!canPlaceEdge(world, pb.buildingId, cellX, cellY, direction, instanceId, &reason,
                      pb.level)) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "edge move preflight rejected without mutation", reason,
            {}, "building.edit"));
    }
    auto addressResult = canonicalEdge(cellX, cellY, direction);
    if (!addressResult.ok())
        return eve::Result<PlacementEditReceipt>::failure(addressResult.status());
    const EdgeAddress target = std::move(addressResult).takeValue();
    const BuildingDefinition *def = BuildingRegistry::find(pb.buildingId);
    if (!def) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "edge move definition was not found", pb.buildingId, {},
            "building.edit"));
    }

    PlacementEditReceipt receipt;
    receipt.before = pb;
    BuildingChangeEvent event;
    event.action = "move";
    event.worldId = world->getId();
    event.buildingId = pb.buildingId;
    event.instanceId = instanceId;
    event.otherCellX = pb.edge.x;
    event.otherCellY = pb.edge.y;
    event.cellX = target.x;
    event.cellY = target.y;
    event.channel = pb.channel;

    dissolveEdgeCurveGroup(world, pb.edgeCurveGroupId);
    clearOccupancy(*world, instanceId);
    pb.edge = target;
    pb.originCellX = target.x;
    pb.originCellY = target.y;
    float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
    world->cellToWorldPlane(target.x, target.y, ax, ay);
    world->cellToWorldPlane(target.x + (target.axis == EdgeAxis::Horizontal ? 1 : 0),
                            target.y + (target.axis == EdgeAxis::Vertical ? 1 : 0), bx, by);
    pb.worldX = (ax + bx) * 0.5f;
    pb.worldY = (ay + by) * 0.5f;
    pb.rotationDeg = target.axis == EdgeAxis::Horizontal ? 0.f : 90.f;
    auto &channels = world->edgeChannels(target.axis, pb.level);
    auto &occupancy = channels[def->channel];
    const size_t required = target.axis == EdgeAxis::Horizontal
                                ? size_t(world->width_) * size_t(world->height_ + 1)
                                : size_t(world->width_ + 1) * size_t(world->height_);
    if (occupancy.size() != required) occupancy.assign(required, 0);
    const size_t index = target.axis == EdgeAxis::Horizontal
                             ? size_t(target.y) * size_t(world->width_) + size_t(target.x)
                             : size_t(target.y) * size_t(world->width_ + 1) + size_t(target.x);
    occupancy[index] = instanceId;
    event.rotationDeg = pb.rotationDeg;
    event.worldX = pb.worldX;
    event.worldY = pb.worldY;
    event.elevation = pb.elevation;
    receipt.after = pb;
    if (world->publishEvents_) emit(std::move(event));
    return eve::Result<PlacementEditReceipt>::success(std::move(receipt));
}

eve::Result<PlacementEditReceipt>
PlacementSystem::moveCornerResult(PlacementWorld *world, int instanceId, int vertexX,
                                  int vertexY) {
    if (!world || instanceId <= 0) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "corner move requires a world and positive instance id", std::to_string(instanceId),
            {}, "building.edit"));
    }
    auto found = world->buildings_.find(instanceId);
    if (found == world->buildings_.end()) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "corner move source instance was not found",
            std::to_string(instanceId), {}, "building.edit"));
    }
    PlacedBuilding &placed = found->second;
    if (placed.placementKind != "corner") {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "corner move requires a corner instance",
            std::to_string(instanceId), {}, "building.edit"));
    }
    if (hasStructuralDependents(*world, instanceId)) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "corner move would invalidate structural dependents",
            "support_in_use", {}, "building.structure"));
    }
    std::string reason;
    if (!canPlaceCorner(world, placed.buildingId, vertexX, vertexY, instanceId, &reason,
                        placed.level)) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "corner move preflight rejected without mutation",
            reason, {}, "building.edit"));
    }
    const BuildingDefinition *definition = BuildingRegistry::find(placed.buildingId);
    if (!definition) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "corner move definition was not found",
            placed.buildingId, {}, "building.edit"));
    }

    PlacementEditReceipt receipt;
    receipt.before = placed;
    clearOccupancy(*world, instanceId);
    placed.corner = CornerAddress{vertexX, vertexY};
    placed.originCellX = vertexX;
    placed.originCellY = vertexY;
    world->cellToWorldPlane(vertexX, vertexY, placed.worldX, placed.worldY);
    PlacementQuery query;
    query.cellX = vertexX;
    query.cellY = vertexY;
    query.level = placed.level;
    placed.supportInstanceIds = collectStructuralSupports(*world, *definition, query);
    auto &occupancy = world->cornerChannels(placed.level)[definition->channel];
    const size_t required = size_t(world->width_ + 1) * size_t(world->height_ + 1);
    if (occupancy.size() != required) occupancy.assign(required, 0);
    occupancy[size_t(vertexY) * size_t(world->width_ + 1) + size_t(vertexX)] = instanceId;
    receipt.after = placed;

    BuildingChangeEvent event;
    event.action = "move";
    event.worldId = world->getId();
    event.buildingId = placed.buildingId;
    event.instanceId = instanceId;
    event.otherCellX = receipt.before.corner.x;
    event.otherCellY = receipt.before.corner.y;
    event.cellX = vertexX;
    event.cellY = vertexY;
    event.level = placed.level;
    event.worldX = placed.worldX;
    event.worldY = placed.worldY;
    event.elevation = placed.elevation;
    event.channel = placed.channel;
    if (world->publishEvents_) emit(std::move(event));
    return eve::Result<PlacementEditReceipt>::success(std::move(receipt));
}

eve::Result<PlacementEditReceipt>
PlacementSystem::moveFreeResult(PlacementWorld *world, int instanceId, float worldX,
                                float worldY, float elevation, float rotationDeg) {
    if (!world || instanceId <= 0) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "free move requires a world and positive instance id", std::to_string(instanceId),
            {}, "building.edit"));
    }
    auto found = world->buildings_.find(instanceId);
    if (found == world->buildings_.end() || found->second.placementKind != "free") {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "free move source instance was not found",
            std::to_string(instanceId), {}, "building.edit"));
    }
    PlacedBuilding &placed = found->second;
    std::string reason;
    if (!canPlaceFree(world, placed.buildingId, worldX, worldY, instanceId, &reason,
                      placed.level, rotationDeg)) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "free move preflight rejected without mutation",
            reason, {}, "building.edit"));
    }
    PlacementEditReceipt receipt;
    receipt.before = placed;
    placed.worldX = worldX;
    placed.worldY = worldY;
    placed.elevation = elevation;
    placed.rotationDeg = normalizeRotation(placed.buildingId, rotationDeg);
    grid::worldToCell(*world->grid_, worldX, worldY, placed.originCellX,
                      placed.originCellY, world->width_, world->height_);
    receipt.after = placed;
    BuildingChangeEvent event;
    event.action = "move";
    event.worldId = world->getId();
    event.buildingId = placed.buildingId;
    event.instanceId = instanceId;
    event.cellX = placed.originCellX;
    event.cellY = placed.originCellY;
    event.level = placed.level;
    event.rotationDeg = placed.rotationDeg;
    event.worldX = placed.worldX;
    event.worldY = placed.worldY;
    event.elevation = placed.elevation;
    event.channel = placed.channel;
    if (world->publishEvents_) emit(std::move(event));
    return eve::Result<PlacementEditReceipt>::success(std::move(receipt));
}

eve::Result<PlacementEditReceipt>
PlacementSystem::moveFreeSurfaceResult(PlacementWorld *world, int instanceId,
                                       const SurfacePatch &patch, float rotationDeg) {
    if (!world || instanceId <= 0) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "free surface move requires a world and positive instance id",
            std::to_string(instanceId), {}, "building.edit"));
    }
    auto found = world->buildings_.find(instanceId);
    if (found == world->buildings_.end() || found->second.placementKind != "free") {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "free surface move source instance was not found",
            std::to_string(instanceId), {}, "building.edit"));
    }
    PlacedBuilding &placed = found->second;
    const BuildingDefinition *def = BuildingRegistry::find(placed.buildingId);
    if (!def) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "free surface move definition was not found",
            placed.buildingId, {}, "building.edit"));
    }
    const float planeX = patch.anchor.worldX;
    const float planeY = world->getGrid().plane == grid::GridPlane::XZ
                             ? patch.anchor.worldZ
                             : patch.anchor.worldY;
    const float elevation = world->getGrid().plane == grid::GridPlane::XZ
                                ? patch.anchor.worldY
                                : patch.anchor.worldZ;
    std::string reason;
    if (!canPlaceFree(world, placed.buildingId, planeX, planeY, instanceId, &reason,
                      placed.level, rotationDeg) ||
        patch.maxSlopeDegrees > def->maxSurfaceSlopeDegrees ||
        (def->maxSurfaceHeightDelta >= 0.f &&
         patch.heightDelta > def->maxSurfaceHeightDelta)) {
        if (reason.empty())
            reason = patch.maxSlopeDegrees > def->maxSurfaceSlopeDegrees
                         ? "surface_slope"
                         : "surface_height_delta";
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict,
            "free surface move preflight rejected without mutation", reason, {},
            "building.edit"));
    }

    PlacementEditReceipt receipt;
    receipt.before = placed;
    placed.worldX = planeX;
    placed.worldY = planeY;
    placed.elevation = elevation;
    placed.rotationDeg = normalizeRotation(placed.buildingId, rotationDeg);
    grid::worldToCell(*world->grid_, planeX, planeY, placed.originCellX,
                      placed.originCellY, world->width_, world->height_);
    placed.surfaceId = patch.anchor.surfaceId;
    placed.surfaceRevision = patch.anchor.surfaceRevision;
    placed.surfaceNormalX = patch.anchor.normalX;
    placed.surfaceNormalY = patch.anchor.normalY;
    placed.surfaceNormalZ = patch.anchor.normalZ;
    placed.surfaceTangentX = patch.anchor.tangentX;
    placed.surfaceTangentY = patch.anchor.tangentY;
    placed.surfaceTangentZ = patch.anchor.tangentZ;
    placed.surfaceSampleCount = static_cast<int>(patch.samples.size());
    placed.surfaceMaxSlopeDegrees = patch.maxSlopeDegrees;
    placed.surfaceHeightDelta = patch.heightDelta;
    receipt.after = placed;

    BuildingChangeEvent event;
    event.action = "move";
    event.worldId = world->getId();
    event.buildingId = placed.buildingId;
    event.instanceId = instanceId;
    event.cellX = placed.originCellX;
    event.cellY = placed.originCellY;
    event.level = placed.level;
    event.rotationDeg = placed.rotationDeg;
    event.worldX = placed.worldX;
    event.worldY = placed.worldY;
    event.elevation = placed.elevation;
    event.channel = placed.channel;
    if (world->publishEvents_) emit(std::move(event));
    return eve::Result<PlacementEditReceipt>::success(std::move(receipt));
}

eve::Result<PlacementEditReceipt>
PlacementSystem::replaceBuildingResult(PlacementWorld *world, int instanceId,
                                       const std::string &replacementBuildingId) {
    if (!world || instanceId <= 0 || replacementBuildingId.empty()) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "replace requires a world, positive instance id and replacement definition",
            replacementBuildingId, {}, "building.edit"));
    }
    auto it = world->buildings().find(instanceId);
    if (it == world->buildings().end()) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "replace source instance was not found",
            std::to_string(instanceId), {}, "building.edit"));
    }
    PlacedBuilding &pb = it->second;
    const BuildingDefinition *replacement = BuildingRegistry::find(replacementBuildingId);
    if (!replacement) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "replacement definition was not found",
            replacementBuildingId, {}, "building.edit"));
    }
    const bool sourceEdge = pb.placementKind == "edge";
    const bool sourceCorner = pb.placementKind == "corner";
    const bool sourceFree = pb.placementKind == "free";
    if (pb.placementKind != replacement->placementKind) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "replacement must use the same placement domain as the source",
            replacementBuildingId, {}, "building.edit"));
    }
    if (hasStructuralDependents(*world, instanceId) &&
        !replacementPreservesStructuralDependents(*world, instanceId, *replacement)) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "replace would invalidate structural dependents",
            "support_in_use", {}, "building.structure"));
    }

    std::string reason;
    if (sourceEdge) {
        const char *direction = pb.edge.axis == EdgeAxis::Horizontal ? "north" : "west";
        if (!canPlaceEdge(world, replacementBuildingId, pb.edge.x, pb.edge.y, direction, instanceId,
                          &reason, pb.level)) {
            return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "replace preflight rejected without mutation", reason,
                {}, "building.edit"));
        }
    } else if (sourceCorner) {
        if (!canPlaceCorner(world, replacementBuildingId, pb.corner.x, pb.corner.y, instanceId,
                            &reason, pb.level)) {
            return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "replace preflight rejected without mutation",
                reason, {}, "building.edit"));
        }
    } else if (sourceFree) {
        if (!canPlaceFree(world, replacementBuildingId, pb.worldX, pb.worldY, instanceId,
                          &reason, pb.level, pb.rotationDeg)) {
            return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "replace preflight rejected without mutation",
                reason, {}, "building.edit"));
        }
    } else if (!canPlaceElev(world, replacementBuildingId, pb.originCellX, pb.originCellY,
                             pb.elevation, pb.rotationDeg, instanceId, &reason, pb.level)) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "replace preflight rejected without mutation", reason, {},
            "building.edit"));
    }

    PlacementEditReceipt receipt;
    receipt.before = pb;
    BuildingChangeEvent event;
    event.action = "replace";
    event.worldId = world->getId();
    event.otherBuildingId = pb.buildingId;
    event.buildingId = replacementBuildingId;
    event.instanceId = instanceId;
    event.otherCellX = pb.originCellX;
    event.otherCellY = pb.originCellY;

    dissolveEdgeCurveGroup(world, pb.edgeCurveGroupId);
    clearOccupancy(*world, instanceId);
    pb.buildingId = replacementBuildingId;
    pb.placementKind = replacement->placementKind;
    pb.channel = replacement->channel;
    pb.tags = replacement->tags;
    if (sourceFree) {
        applyDefinitionFreeFootprint(pb, *replacement, *world);
    }
    pb.rotationDeg = sourceEdge ? pb.rotationDeg
                                : normalizeRotation(replacementBuildingId, pb.rotationDeg);
    if (sourceEdge) {
        pb.supportInstanceIds.clear();
        auto &channels = world->edgeChannels(pb.edge.axis, pb.level);
        auto &occupancy = channels[replacement->channel];
        const size_t required = pb.edge.axis == EdgeAxis::Horizontal
                                    ? size_t(world->width_) * size_t(world->height_ + 1)
                                    : size_t(world->width_ + 1) * size_t(world->height_);
        if (occupancy.size() != required) occupancy.assign(required, 0);
        const size_t index = pb.edge.axis == EdgeAxis::Horizontal
                                 ? size_t(pb.edge.y) * size_t(world->width_) + size_t(pb.edge.x)
                                 : size_t(pb.edge.y) * size_t(world->width_ + 1) + size_t(pb.edge.x);
        occupancy[index] = instanceId;
    } else if (sourceCorner) {
        PlacementQuery supportQuery;
        supportQuery.cellX = pb.corner.x;
        supportQuery.cellY = pb.corner.y;
        supportQuery.level = pb.level;
        pb.supportInstanceIds = collectStructuralSupports(*world, *replacement, supportQuery);
        auto &occupancy = world->cornerChannels(pb.level)[replacement->channel];
        const size_t required = size_t(world->width_ + 1) * size_t(world->height_ + 1);
        if (occupancy.size() != required) occupancy.assign(required, 0);
        occupancy[size_t(pb.corner.y) * size_t(world->width_ + 1) + size_t(pb.corner.x)] =
            instanceId;
    } else {
        PlacementQuery supportQuery;
        supportQuery.cellX = pb.originCellX;
        supportQuery.cellY = pb.originCellY;
        supportQuery.level = pb.level;
        supportQuery.rotationDeg = pb.rotationDeg;
        pb.supportInstanceIds = collectStructuralSupports(*world, *replacement, supportQuery);
        writeOccupancy(*world, *replacement, pb, instanceId);
    }
    event.cellX = pb.originCellX;
    event.cellY = pb.originCellY;
    event.rotationDeg = pb.rotationDeg;
    event.worldX = pb.worldX;
    event.worldY = pb.worldY;
    event.elevation = pb.elevation;
    event.channel = pb.channel;
    receipt.after = pb;
    if (world->publishEvents_) emit(std::move(event));
    return eve::Result<PlacementEditReceipt>::success(std::move(receipt));
}

void PlacementSystem::clearBuildings(PlacementWorld *world) {
    if (!world) return;
    // Collect ids first so remove events fire consistently.
    std::vector<int> ids = world->instanceOrder_;
    for (int id : ids)
        if (world->hasBuilding(id)) removeBuildingCascade(world, id);
    world->buildings().clear();
    world->edgeCurveGroups_.clear();
    world->instanceOrder_.clear();
    for (auto &kv : world->allChannels()) {
        std::fill(kv.second.begin(), kv.second.end(), 0);
    }
    for (auto &kv : world->horizontalEdgeChannels_) std::fill(kv.second.begin(), kv.second.end(), 0);
    for (auto &kv : world->verticalEdgeChannels_) std::fill(kv.second.begin(), kv.second.end(), 0);
    world->cellChannelsByLevel_.clear();
    world->horizontalEdgesByLevel_.clear();
    world->verticalEdgesByLevel_.clear();
}

}  // namespace eve::building
