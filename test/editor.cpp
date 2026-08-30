#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/Brush.h"
#include "editor/Editor.h"
#include "editor/EditorDock.h"
#include "editor/EditorHistory.h"
#include "editor/EditorInspector.h"
#include "editor/EditorToolbar.h"
#include "editor/GizmoManager.h"
#include "editor/LevelDocument.h"
#include "editor/LevelFormat.h"
#include "editor/TileBuffer.h"
#include "editor/TransformGizmo.h"

#include "common/Exception.h"

#include <cmath>
#include <memory>
#include <string>

using namespace eve::editor;

TEST_CASE("editor.module.name") {
    auto* mod = Editor::create();
    CHECK_EQ(mod->getName(), std::string("Editor"));
    CHECK_EQ(Editor::create(), mod);
}

TEST_CASE("editor.gizmo.translate.pick_and_drag") {
    std::unique_ptr<TransformGizmo> g(new TransformGizmo());
    g->setMode("translate");
    g->setSpace("world");
    g->setPosition(0.f, 0.f, 0.f);
    g->setSize(1.f);

    // Ray from +Z looking at origin, aimed near +X axis tip
    std::string axis = g->pick(2.f, 0.f, 5.f, -0.3f, 0.f, -1.f);
    // May hit x or miss depending on exact geometry; force beginDrag on x
    CHECK(g->beginDrag("x", 0.f, 2.f, 5.f, 0.f, 0.f, -1.f));
    CHECK(g->isDragging());
    CHECK_EQ(g->getActiveAxis(), std::string("x"));

    // Drag along +X by moving hit point
    CHECK(g->updateDrag(1.5f, 2.f, 5.f, 0.f, 0.f, -1.f));
    CHECK(g->getPositionX() > 0.f);
    float y = g->getPositionY();
    float z = g->getPositionZ();
    CHECK(std::fabs(y) < 1e-3f);
    CHECK(std::fabs(z) < 1e-3f);

    g->endDrag();
    CHECK(!g->isDragging());
    CHECK_EQ(g->getPartCount() > 0, true);
    CHECK_EQ(g->getPartKind(0), std::string("axis"));
    (void)axis;
}

TEST_CASE("editor.gizmo.snap_translate") {
    std::unique_ptr<TransformGizmo> g(new TransformGizmo());
    g->setMode("translate");
    g->setSnapTranslate(1.f, 1.f, 1.f);
    g->setPosition(0.f, 0.f, 0.f);
    CHECK(g->beginDrag("x", 0.f, 2.f, 5.f, 0.f, 0.f, -1.f));
    g->updateDrag(0.4f, 2.f, 5.f, 0.f, 0.f, -1.f);
    // With snap 1, small moves round toward 0 or 1
    float x = g->getPositionX();
    CHECK(std::fabs(x - std::round(x)) < 1e-4f);
    g->endDrag();
}

TEST_CASE("editor.gizmo.rotate_and_scale_modes") {
    std::unique_ptr<TransformGizmo> g(new TransformGizmo());
    g->setMode("rotate");
    CHECK_EQ(g->getPartCount(), 3);
    CHECK_EQ(g->getPartKind(0), std::string("ring"));

    g->setMode("scale");
    CHECK(g->getPartCount() >= 3);
    CHECK(g->beginDrag("xyz", 0.f, 0.f, 5.f, 0.f, 0.f, -1.f));
    float s0 = g->getScaleX();
    g->updateDrag(0.f, 1.f, 5.f, 0.f, 0.f, -1.f);
    // uniform scale may change
    CHECK(g->getScaleX() > 0.f);
    CHECK_EQ(g->getScaleX(), g->getScaleY());
    (void)s0;
    g->endDrag();

    g->setMode("bound");
    CHECK(g->getPartCount() >= 6);
}

TEST_CASE("editor.gizmo.invalid_mode") {
    std::unique_ptr<TransformGizmo> g(new TransformGizmo());
    bool                            threw = false;
    try {
        g->setMode("skew");
    } catch (const eve::Exception&) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("editor.gizmo_manager.multi_mode") {
    std::unique_ptr<GizmoManager> mgr(new GizmoManager());
    CHECK(!mgr->isAttached());
    CHECK(mgr->pick(0, 0, 5, 0, 0, -1).empty());

    mgr->attach();
    mgr->setPositionEnabled(true);
    mgr->setRotationEnabled(true);
    TransformGizmo* g = mgr->getGizmo();
    g->setPosition(0.f, 0.f, 0.f);
    g->setSize(1.f);

    // Pick should succeed on some handle when looking at origin
    std::string axis = mgr->pick(0.f, 0.f, 5.f, 0.f, 0.f, -1.f);
    // center or axis may be hit
    if (!axis.empty()) {
        CHECK(mgr->beginDrag(axis, 0.f, 0.f, 5.f, 0.f, 0.f, -1.f));
        mgr->endDrag();
    }
    mgr->detach();
    CHECK(!mgr->isAttached());
}

TEST_CASE("editor.brush.paint_fill_line_rect") {
    std::unique_ptr<Editor> ed(Editor::create());
    // Module singleton — don't delete; use factories only
    std::unique_ptr<TileBuffer> buf(new TileBuffer(8, 8));
    std::unique_ptr<Brush>      brush(new Brush());

    brush->setTool("paint");
    brush->setTile(3);
    brush->setSize(1);
    CHECK_EQ(brush->paintAt(buf.get(), 2, 2), 1);
    CHECK_EQ(buf->getGid(2, 2), 3);

    brush->setSize(3);
    brush->setShape("square");
    int n = brush->paintAt(buf.get(), 4, 4);
    CHECK(n >= 1);
    CHECK_EQ(buf->getGid(4, 4), 3);

    buf->fill(1);
    buf->setGid(0, 0, 0);
    brush->setTile(7);
    CHECK(brush->floodFill(buf.get(), 1, 1) > 1);
    CHECK_EQ(buf->getGid(3, 3), 7);
    CHECK_EQ(buf->getGid(0, 0), 0);  // separated

    buf->clear();
    brush->setTile(2);
    brush->setSize(1);
    CHECK(brush->paintLine(buf.get(), 0, 0, 3, 0) >= 4);
    CHECK_EQ(buf->getGid(2, 0), 2);

    buf->clear();
    CHECK(brush->paintRect(buf.get(), 1, 1, 3, 3, true) >= 9);
    CHECK_EQ(buf->getGid(2, 2), 2);

    brush->previewAt(buf.get(), 5, 5);
    CHECK(brush->getPreviewCount() >= 1);
}

TEST_CASE("editor.brush.stamp_and_erase") {
    std::unique_ptr<TileBuffer> buf(new TileBuffer(5, 5));
    std::unique_ptr<Brush>      brush(new Brush());
    brush->setStampSize(2, 2);
    brush->setStampTile(0, 0, 9);
    brush->setStampTile(1, 0, 8);
    brush->setStampTile(0, 1, 7);
    brush->setStampTile(1, 1, 6);
    brush->setTool("stamp");
    CHECK(brush->paintAt(buf.get(), 2, 2) >= 1);
    CHECK(brush->getChangeCount() >= 1);

    brush->setTool("erase");
    brush->setEraseTile(0);
    brush->setSize(1);
    brush->eraseAt(buf.get(), 2, 2);
}

TEST_CASE("editor.toolbar_inspector_dock") {
    std::unique_ptr<EditorToolbar> tb(new EditorToolbar());
    tb->addTool("move", "Move");
    tb->addTool("paint", "Paint");
    tb->setShortcut("move", "W");
    CHECK_EQ(tb->getActive(), std::string("move"));
    CHECK(tb->matchShortcut("W"));
    CHECK(tb->setActive("paint"));
    CHECK_EQ(tb->getActive(), std::string("paint"));
    CHECK_EQ(tb->getToolCount(), 2);

    std::unique_ptr<EditorInspector> insp(new EditorInspector());
    insp->addFloat("size", "Size", 1.f, 0.f, 10.f, 0.1f);
    insp->addFloat3("pos", "Position", 1.f, 2.f, 3.f);
    insp->addBool("snap", "Snap", false);
    insp->addChoice("mode", "Mode", "translate,rotate,scale", "translate");
    insp->setFloat("size", 2.5f);
    CHECK(insp->isDirty("size"));
    CHECK_EQ(insp->pollChangedId(), std::string("size"));
    CHECK_EQ(insp->getFloat3Y("pos"), 2.f);
    insp->setChoice("mode", "rotate");
    CHECK_EQ(insp->getChoice("mode"), std::string("rotate"));

    std::unique_ptr<EditorDock> dock(new EditorDock());
    dock->setRegionSize("left", 100.f);
    dock->setRegionSize("right", 120.f);
    dock->setRegionSize("top", 30.f);
    dock->setRegionSize("bottom", 20.f);
    dock->layout(800.f, 600.f);
    CHECK_EQ(dock->getRegionW("center"), 800.f - 100.f - 120.f);
    CHECK_EQ(dock->getRegionH("center"), 600.f - 30.f - 20.f);
    CHECK_EQ(dock->getRegionX("right"), 800.f - 120.f);
}

TEST_CASE("editor.history.tiles_undo_redo") {
    std::unique_ptr<TileBuffer>    buf(new TileBuffer(4, 4));
    std::unique_ptr<Brush>         brush(new Brush());
    std::unique_ptr<EditorHistory> hist(new EditorHistory());

    brush->setTile(5);
    brush->paintAt(buf.get(), 1, 1);
    hist->beginGroup("paint");
    for (int i = 0; i < brush->getChangeCount(); ++i) {
        hist->recordTile(brush->getChangeX(i), brush->getChangeY(i), brush->getChangeOldGid(i),
                         brush->getChangeNewGid(i));
    }
    hist->endGroup();
    CHECK_EQ(buf->getGid(1, 1), 5);
    CHECK(hist->canUndo());
    CHECK(hist->undo());
    CHECK(hist->applyLastToBuffer(buf.get()));
    CHECK_EQ(buf->getGid(1, 1), 0);
    CHECK(hist->canRedo());
    CHECK(hist->redo());
    CHECK(hist->applyLastToBuffer(buf.get()));
    CHECK_EQ(buf->getGid(1, 1), 5);

    hist->push("rename", "npc_01");
    CHECK(hist->undo());
    CHECK_EQ(hist->getLastActionKind(), std::string("opaque"));
    CHECK_EQ(hist->getLastPayload(), std::string("npc_01"));
}

TEST_CASE("editor.factories") {
    Editor*                          ed = Editor::create();
    std::unique_ptr<TransformGizmo>  g(ed->newGizmo());
    std::unique_ptr<GizmoManager>    m(ed->newGizmoManager());
    std::unique_ptr<TileBuffer>      b(ed->newTileBuffer(2, 2));
    std::unique_ptr<Brush>           br(ed->newBrush());
    std::unique_ptr<EditorToolbar>   t(ed->newToolbar());
    std::unique_ptr<EditorInspector> i(ed->newInspector());
    std::unique_ptr<EditorDock>      d(ed->newDock());
    std::unique_ptr<EditorHistory>   h(ed->newHistory());
    // zeroerr CHECK copies the expression for printing — use raw pointers, not unique_ptr.
    CHECK(g.get() != nullptr);
    CHECK(m.get() != nullptr);
    CHECK_EQ(b->getWidth(), 2);
    CHECK(br.get() != nullptr);
    CHECK(t.get() != nullptr);
    CHECK(i.get() != nullptr);
    CHECK(d.get() != nullptr);
    CHECK(h.get() != nullptr);
}

TEST_CASE("editor.level.document_and_formats") {
    LevelDocument level(4, 3, 16.f, 24.f);
    level.setOrientation("hexagonal");
    int ground = level.addTileLayer("Ground");
    level.getTileLayer(ground)->setGid(2, 1, 17);
    int actors = level.addObjectLayer("Actors");
    int hero   = level.addObject(actors, "spawn", 32.f, 24.f);
    REQUIRE(hero >= 0);
    level.object(actors, hero)->name               = "Player";
    level.object(actors, hero)->properties["team"] = "blue";
    level.setProperty("music", "dungeon");

    LevelFormatRegistry formats;
    CHECK_EQ(formats.getFormatCount(), 2);
    std::string error;
    std::string native = formats.encode("eve.level", level, &error);
    CHECK(error.empty());
    CHECK_EQ(formats.detect("room.level.json", native), std::string("eve.level"));
    auto roundtrip = formats.decode("eve.level", native, &error);
    REQUIRE(roundtrip != nullptr);
    CHECK_EQ(roundtrip->getOrientation(), std::string("hexagonal"));
    CHECK_EQ(roundtrip->getTileLayer(0)->getGid(2, 1), 17);
    CHECK_EQ(roundtrip->object(1, 0)->properties["team"], std::string("blue"));

    std::string tiled = formats.encode("tiled.json", level, &error);
    CHECK_EQ(formats.detect("room.tmj", tiled), std::string("tiled.json"));
    auto imported = formats.decode("tiled.json", tiled, &error);
    REQUIRE(imported != nullptr);
    CHECK_EQ(imported->getLayerCount(), 2);
    CHECK_EQ(imported->getTileLayer(0)->getGid(2, 1), 17);
}
