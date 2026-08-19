#include "building/PlacementSystem.h"
#include "building/BuildingDef.h"
#include "building/Ghost.h"
#include "building/PlacementWorld.h"
#include "grid/GridProjection.h"

#include <algorithm>
#include <cmath>

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
    surfaces()[name] = std::move(fn);
}

void PlacementSystem::unregisterSurface(const std::string &name) { surfaces().erase(name); }

bool PlacementSystem::hasSurface(const std::string &name) {
    ensureBuiltins();
    return surfaces().count(name) > 0;
}

bool PlacementSystem::surfaceHit(const PlacementWorld &world, const std::string &name, float x,
                                 float y, PlacementHit *hit) {
    ensureBuiltins();
    auto &rules = surfaces();
    auto it = rules.find(name.empty() ? "plane" : name);
    if (it == rules.end() || !it->second) return false;
    return it->second(world, x, y, hit);
}

std::vector<std::string> PlacementSystem::surfaceNames() {
    ensureBuiltins();
    std::vector<std::string> names;
    names.reserve(surfaces().size());
    for (const auto &kv : surfaces()) names.push_back(kv.first);
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
        return true;
    });

    registerSurface("plane", [](const PlacementWorld &world, float x, float y,
                                PlacementHit *hit) {
        if (!hit) return false;
        const float h = planeSurfaceHeight();
        if (world.getGrid().plane == grid::GridPlane::XZ) {
            hit->worldX = x;
            hit->worldY = h;
            hit->worldZ = y;
        } else {
            hit->worldX = x;
            hit->worldY = y;
            hit->worldZ = h;
        }
        return true;
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
            const int occ = world.getOccupantInChannel(def.channel, cx, cy);
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
                const int occ = world.getAnyOccupant(nx, ny);
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
                                   int excludeInstanceId, std::string *reason) {
    ensureBuiltins();
    if (!world) {
        if (reason) *reason = "no_world";
        return false;
    }
    const BuildingDefinition *def = BuildingRegistry::find(buildingId);
    if (!def) {
        if (reason) *reason = "unknown_building";
        return false;
    }
    PlacementQuery q;
    q.buildingId = buildingId;
    q.cellX = cellX;
    q.cellY = cellY;
    q.rotationDeg = normalizeRotation(buildingId, rotationDeg);
    world->cellToWorldPlane(cellX, cellY, q.worldX, q.worldY);
    q.elevation = elevation;
    q.excludeInstanceId = excludeInstanceId;
    return runValidate(*world, *def, q, reason);
}

bool PlacementSystem::canPlace(PlacementWorld *world, const std::string &buildingId, int cellX,
                               int cellY, float rotationDeg, int excludeInstanceId,
                               std::string *reason) {
    return canPlaceElev(world, buildingId, cellX, cellY, 0.f, rotationDeg, excludeInstanceId,
                        reason);
}

void PlacementSystem::writeOccupancy(PlacementWorld &world, const BuildingDefinition &def,
                                     const PlacedBuilding &placed, int instanceId) {
    std::vector<int> &occ = world.channelOccupancy(def.channel);
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
}

int PlacementSystem::placeAt(PlacementWorld *world, const std::string &buildingId, int cellX,
                             int cellY, float rotationDeg) {
    ensureBuiltins();
    if (!world) return 0;
    const BuildingDefinition *def = BuildingRegistry::find(buildingId);
    if (!def) return 0;
    const float rot = normalizeRotation(buildingId, rotationDeg);
    if (!canPlace(world, buildingId, cellX, cellY, rot, 0, nullptr)) return 0;

    PlacedBuilding pb;
    pb.instanceId = nextInstanceId();
    pb.buildingId = buildingId;
    pb.originCellX = cellX;
    pb.originCellY = cellY;
    world->cellToWorldPlane(cellX, cellY, pb.worldX, pb.worldY);
    pb.elevation = 0.f;
    pb.rotationDeg = rot;
    pb.channel = def->channel;
    pb.tags = def->tags;

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
    emit(std::move(ev));
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
    const int id = placeAt(world, ghost->getBuildingId(), ghost->getCellX(), ghost->getCellY(),
                           ghost->getRotationDeg());
    if (id <= 0) return 0;
    // 保留吸附后的平面位姿与高度（free/自定义 snap 与 3D 放置需要）。
    auto it = world->buildings().find(id);
    if (it != world->buildings().end()) {
        it->second.worldX = ghost->getWorldX();
        it->second.worldY = ghost->getWorldY();
        it->second.elevation = ghost->getElevation();
    }
    return id;
}

bool PlacementSystem::removeBuilding(PlacementWorld *world, int instanceId) {
    if (!world || instanceId <= 0) return false;
    auto it = world->buildings().find(instanceId);
    if (it == world->buildings().end()) return false;

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

    clearOccupancy(*world, instanceId);
    world->buildings().erase(it);
    auto &order = world->instanceOrder_;
    order.erase(std::remove(order.begin(), order.end(), instanceId), order.end());
    emit(std::move(ev));
    return true;
}

bool PlacementSystem::moveBuilding(PlacementWorld *world, int instanceId, int cellX, int cellY,
                                   float rotationDeg) {
    if (!world || instanceId <= 0) return false;
    auto it = world->buildings().find(instanceId);
    if (it == world->buildings().end()) return false;

    PlacedBuilding &pb = it->second;
    const float rot =
        (rotationDeg < 0.f) ? pb.rotationDeg : normalizeRotation(pb.buildingId, rotationDeg);
    if (!canPlace(world, pb.buildingId, cellX, cellY, rot, instanceId, nullptr)) return false;

    const BuildingDefinition *def = BuildingRegistry::find(pb.buildingId);
    if (!def) return false;

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
    writeOccupancy(*world, *def, pb, instanceId);
    emit(std::move(ev));
    return true;
}

void PlacementSystem::clearBuildings(PlacementWorld *world) {
    if (!world) return;
    // Collect ids first so remove events fire consistently.
    std::vector<int> ids = world->instanceOrder_;
    for (int id : ids) removeBuilding(world, id);
    world->buildings().clear();
    world->instanceOrder_.clear();
    for (auto &kv : world->allChannels()) {
        std::fill(kv.second.begin(), kv.second.end(), 0);
    }
}

}  // namespace eve::building
