#include "editor/Editor.h"

#include "editor/Brush.h"
#include "editor/EditorDock.h"
#include "editor/EditorHistory.h"
#include "editor/EditorInspector.h"
#include "editor/EditorToolbar.h"
#include "editor/GizmoManager.h"
#include "editor/TileBuffer.h"
#include "editor/TransformGizmo.h"

#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#ifdef EVENGINE_HAS_PROCGEN
#include "procgen/heightmap/Heightmap.h"
#endif

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace eve::editor {

Module_IMPL(Editor, new Editor());

#ifdef EVENGINE_HAS_PROCGEN
namespace {

#ifdef EVENGINE_HAS_PROCGEN
struct HeightmapArrays {
    std::vector<float> pos;
    std::vector<float> nrm;
    std::vector<float> uv;
    std::vector<uint32_t> idx;
};

/** Terrain mesh: two triangles per cell, 6 vertices per quad. When
 *  smoothNormals is set, vertex normals come from the height-field gradient
 *  (continuous bowls); otherwise each triangle is flat-shaded. */
void buildHeightmapArrays(const eve::procgen::Heightmap &hm, float cell, float hScale,
                          HeightmapArrays &out, bool smoothNormals) {
    out.pos.clear();
    out.nrm.clear();
    out.uv.clear();
    out.idx.clear();
    const int w = hm.getWidth();
    const int h = hm.getHeight();
    if (w < 2 || h < 2) return;
    const float uw = float(w - 1);
    const float uh = float(h - 1);

    // Per-grid-vertex normals from central differences of the height field.
    std::vector<float> smoothNrm;
    if (smoothNormals) {
        smoothNrm.resize(size_t(w) * size_t(h) * 3);
        auto hs = [&](int x, int z) {
            x = std::clamp(x, 0, w - 1);
            z = std::clamp(z, 0, h - 1);
            return hm.height(x, z);
        };
        for (int z = 0; z < h; ++z) {
            for (int x = 0; x < w; ++x) {
                const float dhdx = (hs(x + 1, z) - hs(x - 1, z)) * 0.5f * hScale / cell;
                const float dhdz = (hs(x, z + 1) - hs(x, z - 1)) * 0.5f * hScale / cell;
                float nx = -dhdx;
                float ny = 1.f;
                float nz = -dhdz;
                const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (len > 1e-8f) {
                    nx /= len;
                    ny /= len;
                    nz /= len;
                }
                float *n = &smoothNrm[(size_t(z) * w + x) * 3];
                n[0] = nx;
                n[1] = ny;
                n[2] = nz;
            }
        }
    }

    auto addTri = [&](float ax, float az, float ay, float bx, float bz, float by, float cx,
                      float cz, float cy) {
        const float p0x = ax * cell, p0y = ay * hScale, p0z = az * cell;
        const float p1x = bx * cell, p1y = by * hScale, p1z = bz * cell;
        const float p2x = cx * cell, p2y = cy * hScale, p2z = cz * cell;
        const uint32_t base = uint32_t(out.pos.size() / 3);
        out.pos.insert(out.pos.end(), {p0x, p0y, p0z, p1x, p1y, p1z, p2x, p2y, p2z});
        if (smoothNormals) {
            const float *n0 = &smoothNrm[(size_t(int(az)) * w + int(ax)) * 3];
            const float *n1 = &smoothNrm[(size_t(int(bz)) * w + int(bx)) * 3];
            const float *n2 = &smoothNrm[(size_t(int(cz)) * w + int(cx)) * 3];
            out.nrm.insert(out.nrm.end(),
                           {n0[0], n0[1], n0[2], n1[0], n1[1], n1[2], n2[0], n2[1], n2[2]});
        } else {
            const float e1x = p1x - p0x, e1y = p1y - p0y, e1z = p1z - p0z;
            const float e2x = p2x - p0x, e2y = p2y - p0y, e2z = p2z - p0z;
            float nx = e1y * e2z - e1z * e2y;
            float ny = e1z * e2x - e1x * e2z;
            float nz = e1x * e2y - e1y * e2x;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-8f) {
                nx /= len;
                ny /= len;
                nz /= len;
            }
            out.nrm.insert(out.nrm.end(), {nx, ny, nz, nx, ny, nz, nx, ny, nz});
        }
        out.uv.insert(out.uv.end(),
                      {ax / uw, az / uh, bx / uw, bz / uh, cx / uw, cz / uh});
        out.idx.insert(out.idx.end(), {base, base + 1, base + 2});
    };

    for (int y = 0; y < h - 1; ++y) {
        for (int x = 0; x < w - 1; ++x) {
            const float h00 = hm.height(x, y);
            const float h10 = hm.height(x + 1, y);
            const float h01 = hm.height(x, y + 1);
            const float h11 = hm.height(x + 1, y + 1);
            // Grid is XZ; y in the function is the XZ "row" axis.
            addTri(float(x), float(y), h00, float(x + 1), float(y), h10, float(x), float(y + 1), h01);
            addTri(float(x + 1), float(y), h10, float(x + 1), float(y + 1), h11, float(x),
                   float(y + 1), h01);
        }
    }
}
#endif

}  // namespace
#endif

TransformGizmo *Editor::newGizmo() { return new TransformGizmo(); }

GizmoManager *Editor::newGizmoManager() { return new GizmoManager(); }

TileBuffer *Editor::newTileBuffer(int width, int height) { return new TileBuffer(width, height); }

Brush *Editor::newBrush() { return new Brush(); }

EditorToolbar *Editor::newToolbar() { return new EditorToolbar(); }

EditorInspector *Editor::newInspector() { return new EditorInspector(); }

EditorDock *Editor::newDock() { return new EditorDock(); }

EditorHistory *Editor::newHistory() { return new EditorHistory(); }

#ifdef EVENGINE_HAS_PROCGEN
graphics::Mesh *Editor::newHeightmapMesh(procgen::Heightmap *hm, float cellSize,
                                         float heightScale) {
    auto *gfx = eve::ModuleManager::getInstance<graphics::Graphics>("Graphics");
    if (!gfx || !hm) return nullptr;
    HeightmapArrays a;
    buildHeightmapArrays(*hm, cellSize, heightScale, a, false);
    if (a.idx.empty()) return nullptr;
    return gfx->newMeshFromArrays(a.pos.data(), a.nrm.data(), a.uv.data(),
                                  int(a.pos.size() / 3), a.idx.data(), int(a.idx.size()));
}

bool Editor::updateHeightmapMesh(graphics::Mesh *mesh, graphics::Graphics *gfx,
                                 procgen::Heightmap *hm, float cellSize, float heightScale) {
    if (!mesh || !gfx || !hm) return false;
    HeightmapArrays a;
    buildHeightmapArrays(*hm, cellSize, heightScale, a, false);
    if (a.idx.empty()) return false;
    return gfx->updateMeshVertices(mesh, a.pos.data(), a.nrm.data(), a.uv.data(),
                                   int(a.pos.size() / 3), a.idx.data(), int(a.idx.size()));
}

graphics::Mesh *Editor::newHeightmapMeshSmooth(procgen::Heightmap *hm, float cellSize,
                                               float heightScale) {
    auto *gfx = eve::ModuleManager::getInstance<graphics::Graphics>("Graphics");
    if (!gfx || !hm) return nullptr;
    HeightmapArrays a;
    buildHeightmapArrays(*hm, cellSize, heightScale, a, true);
    if (a.idx.empty()) return nullptr;
    return gfx->newMeshFromArrays(a.pos.data(), a.nrm.data(), a.uv.data(),
                                  int(a.pos.size() / 3), a.idx.data(), int(a.idx.size()));
}

bool Editor::updateHeightmapMeshSmooth(graphics::Mesh *mesh, graphics::Graphics *gfx,
                                       procgen::Heightmap *hm, float cellSize,
                                       float heightScale) {
    if (!mesh || !gfx || !hm) return false;
    HeightmapArrays a;
    buildHeightmapArrays(*hm, cellSize, heightScale, a, true);
    if (a.idx.empty()) return false;
    return gfx->updateMeshVertices(mesh, a.pos.data(), a.nrm.data(), a.uv.data(),
                                   int(a.pos.size() / 3), a.idx.data(), int(a.idx.size()));
}
#endif

void Editor::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Editor::create, false);
    expose(cls);

    auto gizmo = table.addClass<TransformGizmo>(
        "TransformGizmo",
        std::function<TransformGizmo *()>([]() -> TransformGizmo * { return nullptr; }), true);
    gizmo.addFunc("setMode", &TransformGizmo::setMode);
    gizmo.addFunc("getMode", &TransformGizmo::getMode);
    gizmo.addFunc("setSpace", &TransformGizmo::setSpace);
    gizmo.addFunc("getSpace", &TransformGizmo::getSpace);
    gizmo.addFunc("setSize", &TransformGizmo::setSize);
    gizmo.addFunc("getSize", &TransformGizmo::getSize);
    gizmo.addFunc("setPosition", &TransformGizmo::setPosition);
    gizmo.addFunc("getPositionX", &TransformGizmo::getPositionX);
    gizmo.addFunc("getPositionY", &TransformGizmo::getPositionY);
    gizmo.addFunc("getPositionZ", &TransformGizmo::getPositionZ);
    gizmo.addFunc("setRotationEuler", &TransformGizmo::setRotationEuler);
    gizmo.addFunc("getRotationX", &TransformGizmo::getRotationX);
    gizmo.addFunc("getRotationY", &TransformGizmo::getRotationY);
    gizmo.addFunc("getRotationZ", &TransformGizmo::getRotationZ);
    gizmo.addFunc("setScale", &TransformGizmo::setScale);
    gizmo.addFunc("getScaleX", &TransformGizmo::getScaleX);
    gizmo.addFunc("getScaleY", &TransformGizmo::getScaleY);
    gizmo.addFunc("getScaleZ", &TransformGizmo::getScaleZ);
    gizmo.addFunc("setBounds", &TransformGizmo::setBounds);
    gizmo.addFunc("getBoundsMinX", &TransformGizmo::getBoundsMinX);
    gizmo.addFunc("getBoundsMinY", &TransformGizmo::getBoundsMinY);
    gizmo.addFunc("getBoundsMinZ", &TransformGizmo::getBoundsMinZ);
    gizmo.addFunc("getBoundsMaxX", &TransformGizmo::getBoundsMaxX);
    gizmo.addFunc("getBoundsMaxY", &TransformGizmo::getBoundsMaxY);
    gizmo.addFunc("getBoundsMaxZ", &TransformGizmo::getBoundsMaxZ);
    gizmo.addFunc("setSnapTranslate", &TransformGizmo::setSnapTranslate);
    gizmo.addFunc("setSnapRotate", &TransformGizmo::setSnapRotate);
    gizmo.addFunc("setSnapScale", &TransformGizmo::setSnapScale);
    gizmo.addFunc("getSnapTranslateX", &TransformGizmo::getSnapTranslateX);
    gizmo.addFunc("getSnapTranslateY", &TransformGizmo::getSnapTranslateY);
    gizmo.addFunc("getSnapTranslateZ", &TransformGizmo::getSnapTranslateZ);
    gizmo.addFunc("getSnapRotate", &TransformGizmo::getSnapRotate);
    gizmo.addFunc("getSnapScale", &TransformGizmo::getSnapScale);
    gizmo.addFunc("getMatrix", &TransformGizmo::getMatrix);
    gizmo.addFunc("pick", &TransformGizmo::pick);
    gizmo.addFunc("beginDrag", &TransformGizmo::beginDrag);
    gizmo.addFunc("updateDrag", &TransformGizmo::updateDrag);
    gizmo.addFunc("endDrag", &TransformGizmo::endDrag);
    gizmo.addFunc("isDragging", &TransformGizmo::isDragging);
    gizmo.addFunc("isHovered", &TransformGizmo::isHovered);
    gizmo.addFunc("getActiveAxis", &TransformGizmo::getActiveAxis);
    gizmo.addFunc("getHoverAxis", &TransformGizmo::getHoverAxis);
    gizmo.addFunc("rebuildParts", &TransformGizmo::rebuildParts);
    gizmo.addFunc("getPartCount", &TransformGizmo::getPartCount);
    gizmo.addFunc("getPartKind", &TransformGizmo::getPartKind);
    gizmo.addFunc("getPartAxis", &TransformGizmo::getPartAxis);
    gizmo.addFunc("getPartColorR", &TransformGizmo::getPartColorR);
    gizmo.addFunc("getPartColorG", &TransformGizmo::getPartColorG);
    gizmo.addFunc("getPartColorB", &TransformGizmo::getPartColorB);
    gizmo.addFunc("getPartColorA", &TransformGizmo::getPartColorA);
    gizmo.addFunc("getPartOriginX", &TransformGizmo::getPartOriginX);
    gizmo.addFunc("getPartOriginY", &TransformGizmo::getPartOriginY);
    gizmo.addFunc("getPartOriginZ", &TransformGizmo::getPartOriginZ);
    gizmo.addFunc("getPartDirX", &TransformGizmo::getPartDirX);
    gizmo.addFunc("getPartDirY", &TransformGizmo::getPartDirY);
    gizmo.addFunc("getPartDirZ", &TransformGizmo::getPartDirZ);
    gizmo.addFunc("getPartLength", &TransformGizmo::getPartLength);
    gizmo.addFunc("getPartRadius", &TransformGizmo::getPartRadius);

    auto mgr = table.addClass<GizmoManager>(
        "GizmoManager",
        std::function<GizmoManager *()>([]() -> GizmoManager * { return nullptr; }), true);
    mgr.addFunc("getGizmo", &GizmoManager::getGizmo);
    mgr.addFunc("setPositionEnabled", &GizmoManager::setPositionEnabled);
    mgr.addFunc("setRotationEnabled", &GizmoManager::setRotationEnabled);
    mgr.addFunc("setScaleEnabled", &GizmoManager::setScaleEnabled);
    mgr.addFunc("setBoundEnabled", &GizmoManager::setBoundEnabled);
    mgr.addFunc("getPositionEnabled", &GizmoManager::getPositionEnabled);
    mgr.addFunc("getRotationEnabled", &GizmoManager::getRotationEnabled);
    mgr.addFunc("getScaleEnabled", &GizmoManager::getScaleEnabled);
    mgr.addFunc("getBoundEnabled", &GizmoManager::getBoundEnabled);
    mgr.addFunc("attach", &GizmoManager::attach);
    mgr.addFunc("detach", &GizmoManager::detach);
    mgr.addFunc("isAttached", &GizmoManager::isAttached);
    mgr.addFunc("pick", &GizmoManager::pick);
    mgr.addFunc("beginDrag", &GizmoManager::beginDrag);
    mgr.addFunc("updateDrag", &GizmoManager::updateDrag);
    mgr.addFunc("endDrag", &GizmoManager::endDrag);
    mgr.addFunc("isDragging", &GizmoManager::isDragging);
    mgr.addFunc("isHovered", &GizmoManager::isHovered);

    auto buf = table.addClass<TileBuffer>(
        "TileBuffer", std::function<TileBuffer *()>([]() -> TileBuffer * { return nullptr; }),
        true);
    buf.addFunc("getWidth", &TileBuffer::getWidth);
    buf.addFunc("getHeight", &TileBuffer::getHeight);
    buf.addFunc("resize", &TileBuffer::resize);
    buf.addFunc("clear", &TileBuffer::clear);
    buf.addFunc("fill", &TileBuffer::fill);
    buf.addFunc("setGid", &TileBuffer::setGid);
    buf.addFunc("getGid", &TileBuffer::getGid);
    buf.addFunc("inBounds", &TileBuffer::inBounds);

    auto brush = table.addClass<Brush>(
        "Brush", std::function<Brush *()>([]() -> Brush * { return nullptr; }), true);
    brush.addFunc("setTool", &Brush::setTool);
    brush.addFunc("getTool", &Brush::getTool);
    brush.addFunc("setSize", &Brush::setSize);
    brush.addFunc("getSize", &Brush::getSize);
    brush.addFunc("setShape", &Brush::setShape);
    brush.addFunc("getShape", &Brush::getShape);
    brush.addFunc("setTile", &Brush::setTile);
    brush.addFunc("getTile", &Brush::getTile);
    brush.addFunc("setEraseTile", &Brush::setEraseTile);
    brush.addFunc("getEraseTile", &Brush::getEraseTile);
    brush.addFunc("setStampSize", &Brush::setStampSize);
    brush.addFunc("getStampWidth", &Brush::getStampWidth);
    brush.addFunc("getStampHeight", &Brush::getStampHeight);
    brush.addFunc("setStampTile", &Brush::setStampTile);
    brush.addFunc("getStampTile", &Brush::getStampTile);
    brush.addFunc("clearStamp", &Brush::clearStamp);
    brush.addFunc("paintAt", &Brush::paintAt);
    brush.addFunc("eraseAt", &Brush::eraseAt);
    brush.addFunc("floodFill", &Brush::floodFill);
    brush.addFunc("paintLine", &Brush::paintLine);
    brush.addFunc("paintRect", &Brush::paintRect);
    brush.addFunc("previewAt", &Brush::previewAt);
    brush.addFunc("previewLine", &Brush::previewLine);
    brush.addFunc("previewRect", &Brush::previewRect);
    brush.addFunc("getPreviewCount", &Brush::getPreviewCount);
    brush.addFunc("getPreviewX", &Brush::getPreviewX);
    brush.addFunc("getPreviewY", &Brush::getPreviewY);
    brush.addFunc("getPreviewGid", &Brush::getPreviewGid);
    brush.addFunc("getChangeCount", &Brush::getChangeCount);
    brush.addFunc("getChangeX", &Brush::getChangeX);
    brush.addFunc("getChangeY", &Brush::getChangeY);
    brush.addFunc("getChangeOldGid", &Brush::getChangeOldGid);
    brush.addFunc("getChangeNewGid", &Brush::getChangeNewGid);

    auto tb = table.addClass<EditorToolbar>(
        "EditorToolbar",
        std::function<EditorToolbar *()>([]() -> EditorToolbar * { return nullptr; }), true);
    tb.addFunc("clear", &EditorToolbar::clear);
    tb.addFunc("addTool", &EditorToolbar::addTool);
    tb.addFunc("setShortcut", &EditorToolbar::setShortcut);
    tb.addFunc("setActive", &EditorToolbar::setActive);
    tb.addFunc("getActive", &EditorToolbar::getActive);
    tb.addFunc("matchShortcut", &EditorToolbar::matchShortcut);
    tb.addFunc("getToolCount", &EditorToolbar::getToolCount);
    tb.addFunc("getToolId", &EditorToolbar::getToolId);
    tb.addFunc("getToolLabel", &EditorToolbar::getToolLabel);
    tb.addFunc("getToolShortcut", &EditorToolbar::getToolShortcut);

    auto insp = table.addClass<EditorInspector>(
        "EditorInspector",
        std::function<EditorInspector *()>([]() -> EditorInspector * { return nullptr; }), true);
    insp.addFunc("clear", &EditorInspector::clear);
    insp.addFunc("addFloat", &EditorInspector::addFloat);
    insp.addFunc("addFloat3", &EditorInspector::addFloat3);
    insp.addFunc("addBool", &EditorInspector::addBool);
    insp.addFunc("addString", &EditorInspector::addString);
    insp.addFunc("addChoice", &EditorInspector::addChoice);
    insp.addFunc("getFieldCount", &EditorInspector::getFieldCount);
    insp.addFunc("getFieldKind", &EditorInspector::getFieldKind);
    insp.addFunc("getFieldId", &EditorInspector::getFieldId);
    insp.addFunc("getFieldLabel", &EditorInspector::getFieldLabel);
    insp.addFunc("getFloat", &EditorInspector::getFloat);
    insp.addFunc("setFloat", &EditorInspector::setFloat);
    insp.addFunc("getFloatMin", &EditorInspector::getFloatMin);
    insp.addFunc("getFloatMax", &EditorInspector::getFloatMax);
    insp.addFunc("getFloatStep", &EditorInspector::getFloatStep);
    insp.addFunc("getFloat3X", &EditorInspector::getFloat3X);
    insp.addFunc("getFloat3Y", &EditorInspector::getFloat3Y);
    insp.addFunc("getFloat3Z", &EditorInspector::getFloat3Z);
    insp.addFunc("setFloat3", &EditorInspector::setFloat3);
    insp.addFunc("getBool", &EditorInspector::getBool);
    insp.addFunc("setBool", &EditorInspector::setBool);
    insp.addFunc("getString", &EditorInspector::getString);
    insp.addFunc("setString", &EditorInspector::setString);
    insp.addFunc("getChoice", &EditorInspector::getChoice);
    insp.addFunc("setChoice", &EditorInspector::setChoice);
    insp.addFunc("getChoicesCsv", &EditorInspector::getChoicesCsv);
    insp.addFunc("isDirty", &EditorInspector::isDirty);
    insp.addFunc("clearDirty", &EditorInspector::clearDirty);
    insp.addFunc("clearAllDirty", &EditorInspector::clearAllDirty);
    insp.addFunc("pollChangedId", &EditorInspector::pollChangedId);

    auto dock = table.addClass<EditorDock>(
        "EditorDock", std::function<EditorDock *()>([]() -> EditorDock * { return nullptr; }),
        true);
    dock.addFunc("setRegionSize", &EditorDock::setRegionSize);
    dock.addFunc("getRegionSize", &EditorDock::getRegionSize);
    dock.addFunc("layout", &EditorDock::layout);
    dock.addFunc("getRegionX", &EditorDock::getRegionX);
    dock.addFunc("getRegionY", &EditorDock::getRegionY);
    dock.addFunc("getRegionW", &EditorDock::getRegionW);
    dock.addFunc("getRegionH", &EditorDock::getRegionH);

    auto hist = table.addClass<EditorHistory>(
        "EditorHistory",
        std::function<EditorHistory *()>([]() -> EditorHistory * { return nullptr; }), true);
    hist.addFunc("clear", &EditorHistory::clear);
    hist.addFunc("push", &EditorHistory::push);
    hist.addFunc("beginGroup", &EditorHistory::beginGroup);
    hist.addFunc("recordTile", &EditorHistory::recordTile);
    hist.addFunc("endGroup", &EditorHistory::endGroup);
    hist.addFunc("isGrouping", &EditorHistory::isGrouping);
    hist.addFunc("canUndo", &EditorHistory::canUndo);
    hist.addFunc("canRedo", &EditorHistory::canRedo);
    hist.addFunc("getUndoCount", &EditorHistory::getUndoCount);
    hist.addFunc("getRedoCount", &EditorHistory::getRedoCount);
    hist.addFunc("undo", &EditorHistory::undo);
    hist.addFunc("redo", &EditorHistory::redo);
    hist.addFunc("applyLastToBuffer", &EditorHistory::applyLastToBuffer);
    hist.addFunc("getLastActionName", &EditorHistory::getLastActionName);
    hist.addFunc("getLastActionKind", &EditorHistory::getLastActionKind);
    hist.addFunc("getLastPayload", &EditorHistory::getLastPayload);
    hist.addFunc("getLastTileCount", &EditorHistory::getLastTileCount);
    hist.addFunc("getLastTileX", &EditorHistory::getLastTileX);
    hist.addFunc("getLastTileY", &EditorHistory::getLastTileY);
    hist.addFunc("getLastTileOldGid", &EditorHistory::getLastTileOldGid);
    hist.addFunc("getLastTileNewGid", &EditorHistory::getLastTileNewGid);
}

void Editor::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Editor::getName);
    cls.addFunc("newGizmo", &Editor::newGizmo);
    cls.addFunc("newGizmoManager", &Editor::newGizmoManager);
    cls.addFunc("newTileBuffer", &Editor::newTileBuffer);
    cls.addFunc("newBrush", &Editor::newBrush);
    cls.addFunc("newToolbar", &Editor::newToolbar);
    cls.addFunc("newInspector", &Editor::newInspector);
    cls.addFunc("newDock", &Editor::newDock);
    cls.addFunc("newHistory", &Editor::newHistory);
#ifdef EVENGINE_HAS_PROCGEN
    cls.addFunc("newHeightmapMesh", &Editor::newHeightmapMesh);
    cls.addFunc("updateHeightmapMesh", &Editor::updateHeightmapMesh);
    cls.addFunc("newHeightmapMeshSmooth", &Editor::newHeightmapMeshSmooth);
    cls.addFunc("updateHeightmapMeshSmooth", &Editor::updateHeightmapMeshSmooth);
#endif
}

}  // namespace eve::editor
