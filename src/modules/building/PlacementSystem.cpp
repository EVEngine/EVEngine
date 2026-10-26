#include "building/PlacementInternal.h"

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

using namespace placement_detail;

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
    if (normalizeSurfaceFrame(hit) != SurfaceFrameStatus::Normalized) {
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

int PlacementSystem::nextInstanceId(PlacementWorld &world) { return ++world.instanceCounter_; }

void PlacementSystem::pushEvent(BuildingChangeEvent ev) { emit(std::move(ev)); }

void PlacementSystem::pollEvents(std::vector<BuildingChangeEvent> &out) {
    out = eventQueue();
    eventQueue().clear();
}

void PlacementSystem::clearEvents() { eventQueue().clear(); }

const std::vector<BuildingChangeEvent> &PlacementSystem::events() { return eventQueue(); }

void PlacementSystem::emit(BuildingChangeEvent ev) {
    eventQueue().push_back(ev);
    const auto hooks = changeHooks();
    for (const auto &kv : hooks) {
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


}  // namespace eve::building
