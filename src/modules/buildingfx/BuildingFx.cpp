#include "buildingfx/BuildingFx.h"

#include "building/BuildingDef.h"
#include "building/Ghost.h"
#include "building/PlacementSystem.h"
#include "building/PlacementWorld.h"
#include "grid/GridConfig.h"
#include "common/ECS.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>

namespace eve::buildingfx {

// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

Module_IMPL(BuildingFx, new BuildingFx());

namespace {

float toFloat(const std::string &s, float fallback) {
    if (s.empty()) return fallback;
    try {
        return std::stof(s);
    } catch (...) {
        return fallback;
    }
}

graphics::Graphics *gfxOrNull() { return getModInst(graphics, Graphics); }

}  // namespace

bool BuildingFx::attach(building::PlacementWorld *world) {
    if (!world) return false;
    states_[world];  // ensure entry
    return true;
}

bool BuildingFx::detach(building::PlacementWorld *world) {
    if (!world) return false;
    auto it = states_.find(world);
    if (it == states_.end()) return false;
    destroyAll(it->second);
    states_.erase(it);
    return true;
}

bool BuildingFx::isAttached(building::PlacementWorld *world) const {
    return world != nullptr && states_.count(world) > 0;
}

int BuildingFx::getAttachedCount() const { return int(states_.size()); }

bool BuildingFx::is3d(const building::BuildingDefinition &def) const {
    return def.renderMode == "3d" || def.renderMode == "3D";
}

graphics::Mesh *BuildingFx::cubeMesh(graphics::Graphics *gfx) {
    if (!gfx) return nullptr;
    if (!cubeMesh_) cubeMesh_ = gfx->newMeshCube(1.f);
    return cubeMesh_;
}

void BuildingFx::createVisual(WorldState &st, const building::BuildingDefinition &def,
                              const building::PlacedBuilding &pb,
                              building::PlacementWorld *world, Visual &v, float alpha) {
    (void)st;
    const float cellW = world->getGrid().cellW;
    const float cellH = world->getGrid().cellH;
    int effW = def.footprintW;
    int effH = def.footprintH;
    building::PlacementSystem::effectiveFootprint(def, pb.rotationDeg, &effW, &effH);

    if (is3d(def)) {
        auto *r = graphics::Renderable3D::create();
        float wx = 0.f, wy = 0.f, wz = 0.f;
        world->cellToWorld3D(pb.originCellX, pb.originCellY, pb.elevation, wx, wy, wz);
        auto tr = r->transform();
        tr->x = wx + toFloat(def.getVisual3d("offsetX"), 0.f);
        tr->y = wy + toFloat(def.getVisual3d("offsetY"), 0.f);
        tr->z = wz + toFloat(def.getVisual3d("offsetZ"), 0.f);
        const float height = toFloat(def.getVisual3d("height"), 1.f);
        tr->sx = float(effW) * cellW;
        tr->sy = height;
        tr->sz = float(effH) * cellH;
        const float rad = pb.rotationDeg * 3.14159265f / 180.f;
        if (world->getGrid().plane == grid::GridPlane::XZ) {
            tr->yaw = rad;
        } else {
            tr->roll = rad;
        }
        auto mr = r->meshRenderer();
        mr->mesh = cubeMesh(gfxOrNull());
        mr->r = toFloat(def.getVisual3d("colorR"), 0.62f);
        mr->g = toFloat(def.getVisual3d("colorG"), 0.62f);
        mr->b = toFloat(def.getVisual3d("colorB"), 0.62f);
        mr->a = alpha;
        mr->visible = true;
        mr->castShadow = true;
        mr->receiveShadow = true;
        v.r3d = r;
    } else {
        auto *r = graphics::Renderable2D::create();
        auto tr = r->transform();
        tr->x = pb.worldX + toFloat(def.getVisual2d("offsetX"), 0.f);
        tr->y = pb.worldY + toFloat(def.getVisual2d("offsetY"), 0.f);
        tr->rot = pb.rotationDeg;
        auto sp = r->sprite();
        sp->width = float(effW) * cellW;
        sp->height = float(effH) * cellH;
        sp->r = toFloat(def.getVisual2d("colorR"), 0.72f);
        sp->g = toFloat(def.getVisual2d("colorG"), 0.72f);
        sp->b = toFloat(def.getVisual2d("colorB"), 0.72f);
        sp->a = alpha;
        sp->layer = int(toFloat(def.getVisual2d("layer"), 0.f));
        sp->visible = true;
        const std::string texPath = def.getVisual2d("texture");
        if (!texPath.empty()) {
            if (auto *gfx = gfxOrNull()) {
                if (graphics::Texture *tex = gfx->newTextureFromFile(texPath)) {
                    sp->texture = tex;
                }
            }
        }
        v.r2d = r;
    }
}

void BuildingFx::updateVisual(const building::BuildingDefinition &def,
                              const building::PlacedBuilding &pb,
                              building::PlacementWorld *world, Visual &v) {
    const float cellW = world->getGrid().cellW;
    const float cellH = world->getGrid().cellH;
    int effW = def.footprintW;
    int effH = def.footprintH;
    building::PlacementSystem::effectiveFootprint(def, pb.rotationDeg, &effW, &effH);

    if (v.r3d) {
        float wx = 0.f, wy = 0.f, wz = 0.f;
        world->cellToWorld3D(pb.originCellX, pb.originCellY, pb.elevation, wx, wy, wz);
        auto tr = v.r3d->transform();
        tr->x = wx + toFloat(def.getVisual3d("offsetX"), 0.f);
        tr->y = wy + toFloat(def.getVisual3d("offsetY"), 0.f);
        tr->z = wz + toFloat(def.getVisual3d("offsetZ"), 0.f);
        const float height = toFloat(def.getVisual3d("height"), 1.f);
        tr->sx = float(effW) * cellW;
        tr->sy = height;
        tr->sz = float(effH) * cellH;
        const float rad = pb.rotationDeg * 3.14159265f / 180.f;
        if (world->getGrid().plane == grid::GridPlane::XZ) {
            tr->yaw = rad;
        } else {
            tr->roll = rad;
        }
    } else if (v.r2d) {
        auto tr = v.r2d->transform();
        tr->x = pb.worldX + toFloat(def.getVisual2d("offsetX"), 0.f);
        tr->y = pb.worldY + toFloat(def.getVisual2d("offsetY"), 0.f);
        tr->rot = pb.rotationDeg;
        auto sp = v.r2d->sprite();
        sp->width = float(effW) * cellW;
        sp->height = float(effH) * cellH;
    }
}

void BuildingFx::destroyVisual(Visual &v) {
    if (v.r2d) {
        ecs::DestroyEntity(v.r2d);
        v.r2d = nullptr;
    }
    if (v.r3d) {
        ecs::DestroyEntity(v.r3d);
        v.r3d = nullptr;
    }
}

void BuildingFx::setVisible(Visual &v, bool visible) {
    if (v.r2d) v.r2d->sprite()->visible = visible;
    if (v.r3d) v.r3d->meshRenderer()->visible = visible;
}

void BuildingFx::destroyAll(WorldState &st) {
    for (auto &kv : st.visuals) destroyVisual(kv.second);
    st.visuals.clear();
    destroyVisual(st.ghost);
    destroyVisual(st.cursor);
    st.ghostBuildingId.clear();
    for (graphics::Renderable3D *line : st.gridLines) {
        if (line) ecs::DestroyEntity(line);
    }
    st.gridLines.clear();
    st.gridLineCount = -1;
}

void BuildingFx::sync(building::PlacementWorld *world) {
    if (!world) return;
    auto it = states_.find(world);
    if (it == states_.end()) return;
    WorldState &st = it->second;

    std::unordered_map<int, Visual> next;
    const int n = world->getBuildingCount();
    for (int i = 0; i < n; ++i) {
        const int inst = world->getBuildingInstanceAt(i);
        auto bit = world->buildings().find(inst);
        if (bit == world->buildings().end()) continue;
        const auto &pb = bit->second;
        const building::BuildingDefinition *def =
            building::BuildingRegistry::find(pb.buildingId);
        if (!def) continue;
        auto existing = st.visuals.find(inst);
        if (existing != st.visuals.end()) {
            next[inst] = existing->second;
            updateVisual(*def, pb, world, next[inst]);
        } else {
            Visual v;
            createVisual(st, *def, pb, world, v, 1.f);
            next[inst] = v;
        }
    }
    for (auto &kv : st.visuals) {
        if (next.count(kv.first) == 0) destroyVisual(kv.second);
    }
    st.visuals = std::move(next);
}

int BuildingFx::getVisualCount(building::PlacementWorld *world) const {
    auto it = states_.find(world);
    return it == states_.end() ? 0 : int(it->second.visuals.size());
}

void BuildingFx::updateGhost(building::PlacementWorld *world, building::Ghost *ghost) {
    if (!world || !ghost) {
        hideGhost(world);
        return;
    }
    auto it = states_.find(world);
    if (it == states_.end()) return;
    WorldState &st = it->second;
    const building::BuildingDefinition *def =
        building::BuildingRegistry::find(ghost->getBuildingId());
    if (!def) return;

    if (st.ghostBuildingId != ghost->getBuildingId()) {
        destroyVisual(st.ghost);
        destroyVisual(st.cursor);
        st.ghostBuildingId = ghost->getBuildingId();
    }
    if (!st.ghost.r2d && !st.ghost.r3d) {
        // 用 ghost 当前位姿构造临时 PlacedBuilding 以复用 createVisual。
        building::PlacedBuilding pb;
        pb.buildingId = ghost->getBuildingId();
        pb.originCellX = ghost->getCellX();
        pb.originCellY = ghost->getCellY();
        pb.worldX = ghost->getWorldX();
        pb.worldY = ghost->getWorldY();
        pb.elevation = ghost->getElevation();
        pb.rotationDeg = ghost->getRotationDeg();
        createVisual(st, *def, pb, world, st.ghost, 0.5f);
    }
    {
        building::PlacedBuilding pb;
        pb.buildingId = ghost->getBuildingId();
        pb.originCellX = ghost->getCellX();
        pb.originCellY = ghost->getCellY();
        pb.worldX = ghost->getWorldX();
        pb.worldY = ghost->getWorldY();
        pb.elevation = ghost->getElevation();
        pb.rotationDeg = ghost->getRotationDeg();
        updateVisual(*def, pb, world, st.ghost);
    }

    const bool valid = ghost->isValid();
    const float cr = valid ? 0.25f : 0.90f;
    const float cg = valid ? 0.85f : 0.25f;
    const float cb = valid ? 0.35f : 0.22f;
    if (st.ghost.r2d) {
        auto sp = st.ghost.r2d->sprite();
        sp->r = cr;
        sp->g = cg;
        sp->b = cb;
    }
    if (st.ghost.r3d) {
        auto mr = st.ghost.r3d->meshRenderer();
        mr->r = cr;
        mr->g = cg;
        mr->b = cb;
    }
    setVisible(st.ghost, true);

    // 占地光标。
    if (!st.cursor.r2d && !st.cursor.r3d) {
        const float cellW = world->getGrid().cellW;
        const float cellH = world->getGrid().cellH;
        int effW = def->footprintW;
        int effH = def->footprintH;
        building::PlacementSystem::effectiveFootprint(*def, ghost->getRotationDeg(), &effW, &effH);
        if (is3d(*def)) {
            auto *r = graphics::Renderable3D::create();
            auto tr = r->transform();
            float wx = 0.f, wy = 0.f, wz = 0.f;
            world->cellToWorld3D(ghost->getCellX(), ghost->getCellY(), ghost->getElevation(), wx,
                                 wy, wz);
            tr->x = wx;
            tr->y = wy + 0.03f;
            tr->z = wz;
            tr->sx = float(effW) * cellW;
            tr->sy = 0.04f;
            tr->sz = float(effH) * cellH;
            auto mr = r->meshRenderer();
            mr->mesh = cubeMesh(gfxOrNull());
            mr->r = cr;
            mr->g = cg;
            mr->b = cb;
            mr->a = 0.35f;
            st.cursor.r3d = r;
        } else {
            auto *r = graphics::Renderable2D::create();
            auto tr = r->transform();
            tr->x = ghost->getWorldX();
            tr->y = ghost->getWorldY();
            auto sp = r->sprite();
            sp->width = float(effW) * cellW;
            sp->height = float(effH) * cellH;
            sp->r = cr;
            sp->g = cg;
            sp->b = cb;
            sp->a = 0.35f;
            st.cursor.r2d = r;
        }
    } else {
        const float cellW = world->getGrid().cellW;
        const float cellH = world->getGrid().cellH;
        int effW = def->footprintW;
        int effH = def->footprintH;
        building::PlacementSystem::effectiveFootprint(*def, ghost->getRotationDeg(), &effW, &effH);
        if (st.cursor.r3d) {
            auto tr = st.cursor.r3d->transform();
            float wx = 0.f, wy = 0.f, wz = 0.f;
            world->cellToWorld3D(ghost->getCellX(), ghost->getCellY(), ghost->getElevation(), wx,
                                 wy, wz);
            tr->x = wx;
            tr->y = wy + 0.03f;
            tr->z = wz;
            tr->sx = float(effW) * cellW;
            tr->sz = float(effH) * cellH;
            auto mr = st.cursor.r3d->meshRenderer();
            mr->r = cr;
            mr->g = cg;
            mr->b = cb;
        } else if (st.cursor.r2d) {
            auto tr = st.cursor.r2d->transform();
            tr->x = ghost->getWorldX();
            tr->y = ghost->getWorldY();
            auto sp = st.cursor.r2d->sprite();
            sp->width = float(effW) * cellW;
            sp->height = float(effH) * cellH;
            sp->r = cr;
            sp->g = cg;
            sp->b = cb;
        }
    }
    setVisible(st.cursor, true);
}

void BuildingFx::hideGhost(building::PlacementWorld *world) {
    if (!world) return;
    auto it = states_.find(world);
    if (it == states_.end()) return;
    WorldState &st = it->second;
    setVisible(st.ghost, false);
    setVisible(st.cursor, false);
}

void BuildingFx::setGridVisible(building::PlacementWorld *world, bool visible) {
    if (!world) return;
    states_[world].gridVisible = visible;
}

bool BuildingFx::getGridVisible(building::PlacementWorld *world) const {
    auto it = states_.find(world);
    return it != states_.end() && it->second.gridVisible;
}

void BuildingFx::drawGrid2D(building::PlacementWorld *world, graphics::Graphics *gfx) {
    if (!world || !gfx) return;
    auto it = states_.find(world);
    if (it == states_.end() || !it->second.gridVisible) return;
    const float cellW = world->getGrid().cellW;
    const float cellH = world->getGrid().cellH;
    for (int y = 0; y < world->getHeight(); ++y) {
        for (int x = 0; x < world->getWidth(); ++x) {
            float px = 0.f, py = 0.f;
            world->cellToWorldPlane(x, y, px, py);
            gfx->drawSolidRect(px, py, cellW - 1.f, cellH - 1.f,
                               Color{0.75f, 0.78f, 0.85f, 0.06f});
        }
    }
}

void BuildingFx::rebuildGridLines(WorldState &st, building::PlacementWorld *world,
                                  graphics::Graphics *gfx, float height) {
    const int w = world->getWidth();
    const int h = world->getHeight();
    const float cellW = world->getGrid().cellW;
    const float cellH = world->getGrid().cellH;
    const int want = (w + 1) + (h + 1);
    if (int(st.gridLines.size()) == want && st.gridLineCount == want) return;
    for (graphics::Renderable3D *line : st.gridLines) {
        if (line) ecs::DestroyEntity(line);
    }
    st.gridLines.clear();
    graphics::Mesh *mesh = cubeMesh(gfx);
    const bool xz = world->getGrid().plane == grid::GridPlane::XZ;
    const float xExtent = float(w) * cellW;
    const float zExtent = float(h) * cellH;
    for (int x = 0; x <= w; ++x) {
        auto *r = graphics::Renderable3D::create();
        auto tr = r->transform();
        float px = 0.f, py = 0.f;
        world->cellToWorldPlane(x, 0, px, py);
        tr->x = px;
        tr->y = height;
        tr->z = xz ? 0.0f : 0.f;
        tr->sx = 0.03f;
        tr->sy = 0.02f;
        tr->sz = zExtent;
        auto mr = r->meshRenderer();
        mr->mesh = mesh;
        mr->r = 0.85f;
        mr->g = 0.88f;
        mr->b = 0.95f;
        mr->a = 0.5f;
        st.gridLines.push_back(r);
    }
    for (int y = 0; y <= h; ++y) {
        auto *r = graphics::Renderable3D::create();
        auto tr = r->transform();
        float px = 0.f, py = 0.f;
        world->cellToWorldPlane(0, y, px, py);
        tr->x = xz ? 0.0f : 0.f;
        tr->y = height;
        tr->z = py;
        tr->sx = xExtent;
        tr->sy = 0.02f;
        tr->sz = 0.03f;
        auto mr = r->meshRenderer();
        mr->mesh = mesh;
        mr->r = 0.85f;
        mr->g = 0.88f;
        mr->b = 0.95f;
        mr->a = 0.5f;
        st.gridLines.push_back(r);
    }
    st.gridLineCount = want;
}

void BuildingFx::drawGrid3D(building::PlacementWorld *world, graphics::Graphics *gfx,
                            float height) {
    if (!world || !gfx) return;
    auto it = states_.find(world);
    if (it == states_.end()) return;
    WorldState &st = it->second;
    rebuildGridLines(st, world, gfx, height);
    for (graphics::Renderable3D *line : st.gridLines) {
        if (line) line->meshRenderer()->visible = st.gridVisible;
    }
}

void BuildingFx::expose(ssq::Table &table) {
    auto cls = table.addClass(name, BuildingFx::create, false);
    expose(cls);
}

void BuildingFx::expose(ssq::Class &cls) {
    cls.addFunc("attach", &BuildingFx::attach);
    cls.addFunc("detach", &BuildingFx::detach);
    cls.addFunc("isAttached", &BuildingFx::isAttached);
    cls.addFunc("getAttachedCount", &BuildingFx::getAttachedCount);
    cls.addFunc("sync", &BuildingFx::sync);
    cls.addFunc("getVisualCount", &BuildingFx::getVisualCount);
    cls.addFunc("updateGhost", &BuildingFx::updateGhost);
    cls.addFunc("hideGhost", &BuildingFx::hideGhost);
    cls.addFunc("setGridVisible", &BuildingFx::setGridVisible);
    cls.addFunc("getGridVisible", &BuildingFx::getGridVisible);
    cls.addFunc("drawGrid2D", &BuildingFx::drawGrid2D);
    cls.addFunc("drawGrid3D", &BuildingFx::drawGrid3D);
}

}  // namespace eve::buildingfx
