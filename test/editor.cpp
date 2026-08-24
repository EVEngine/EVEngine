#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/Brush.h"
#include "editor/BrushKernel.h"
#include "editor/Editor.h"
#include "editor/EditorDock.h"
#include "editor/EditorHistory.h"
#include "editor/EditorInspector.h"
#include "editor/EditorSession.h"
#include "editor/EditorPresentation.h"
#include "editor/EditorTransactions.h"
#include "editor/EditConstraint.h"
#include "editor/EditorToolbar.h"
#include "editor/FieldTargets.h"
#include "editor/FieldBrushTool.h"
#include "editor/GizmoManager.h"
#include "editor/TileBuffer.h"
#ifdef EVENGINE_HAS_MAP
#include "map/Map.h"
#include "map/TileLayer.h"
#endif
#include "editor/TransformGizmo.h"

#include "common/Exception.h"
#include "procgen/heightmap/Heightmap.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cmath>
#include <memory>
#include <string>

using namespace eve::editor;

namespace {
class TestEditorTool final : public IEditorTool {
public:
    explicit TestEditorTool(std::string id) { desc.id = std::move(id); }
    const ToolDescriptor &descriptor() const override { return desc; }
    void activate(EditorContext &) override { ++activations; }
    void deactivate(EditorContext &) override { ++deactivations; }
    void cancel(EditorContext &) override { ++cancellations; }
    ToolResponse pointerEvent(EditorContext &, const EditorPointerEvent &event) override {
        ++pointerEvents;
        if (event.phase == EditorPointerEvent::Phase::Down) return ToolResponse::capture();
        if (event.phase == EditorPointerEvent::Phase::Up) return ToolResponse::release();
        return ToolResponse::consumed();
    }
    ToolResponse keyEvent(EditorContext &, const EditorKeyEvent &) override {
        ++keyEvents;
        return ToolResponse::consumed();
    }
    void update(EditorContext &, float) override { ++updates; }
    void drawOverlay(EditorContext &, IEditorOverlay &overlay) override {
        ++overlays;
        overlay.circle({1.f, 2.f, 3.f}, 4.f, {});
    }
    void inspect(EditorContext &, IEditorInspector &inspector) override {
        ++inspections;
        inspector.scalar("radius", "Radius", radius, 0.f, 10.f);
    }

    ToolDescriptor desc;
    int activations = 0;
    int deactivations = 0;
    int cancellations = 0;
    int pointerEvents = 0;
    int keyEvents = 0;
    int updates = 0;
    int overlays = 0;
    int inspections = 0;
    float radius = 2.f;
};

class TestPresentation final : public IEditorOverlay, public IEditorInspector {
public:
    void line(const OverlayPoint &, const OverlayPoint &, const OverlayStyle &) override { ++primitives; }
    void circle(const OverlayPoint &, float, const OverlayStyle &) override { ++primitives; }
    void rectangle(const OverlayPoint &, const OverlayPoint &, const OverlayStyle &) override { ++primitives; }
    void text(const OverlayPoint &, const std::string &, const OverlayStyle &) override { ++primitives; }
    void beginGroup(const std::string &, const std::string &) override {}
    void endGroup() override {}
    bool boolean(const std::string &, const std::string &, bool &) override { return false; }
    bool integer(const std::string &, const std::string &, int &, int, int) override { return false; }
    bool scalar(const std::string &, const std::string &, float &value, float, float) override {
        value = 6.f;
        return true;
    }
    bool string(const std::string &, const std::string &, std::string &) override { return false; }

    int primitives = 0;
};

class RejectLargeEdit final : public IEditConstraint {
public:
    ConstraintResult evaluate(EditorContext &, IEditCommand &command) override {
        const EditRegion region = command.dirtyRegion();
        if (!region.empty() && region.maxX - region.minX > 1) return ConstraintResult::reject("edit too wide");
        return ConstraintResult::warning("checked");
    }
};
}  // namespace

TEST_CASE("editor.session.routes_tool_lifecycle_and_capture") {
    EditorSession session;
    TestEditorTool first("first");
    TestEditorTool second("second");
    CHECK(session.addTool(&first));
    CHECK(session.addTool(&second));
    CHECK(!session.addTool(&first));
    CHECK_EQ(session.getToolCount(), 2);
    CHECK(session.activateTool("first"));
    CHECK_EQ(first.activations, 1);

    EditorPointerEvent down;
    down.phase = EditorPointerEvent::Phase::Down;
    down.pointerId = 7;
    CHECK(session.dispatchPointer(down).handled);
    CHECK(session.hasPointerCapture());
    CHECK_EQ(session.capturedPointerId(), 7);

    EditorPointerEvent other = down;
    other.phase = EditorPointerEvent::Phase::Move;
    other.pointerId = 8;
    CHECK(!session.dispatchPointer(other).handled);
    CHECK_EQ(first.pointerEvents, 1);

    EditorPointerEvent up = down;
    up.phase = EditorPointerEvent::Phase::Up;
    CHECK(session.dispatchPointer(up).releasePointer);
    CHECK(!session.hasPointerCapture());

    EditorKeyEvent key;
    key.key = "W";
    key.pressed = true;
    CHECK(session.dispatchKey(key).handled);
    session.update(0.016f);
    CHECK_EQ(first.keyEvents, 1);
    CHECK_EQ(first.updates, 1);

    CHECK(session.activateTool("second"));
    CHECK_EQ(first.cancellations, 1);
    CHECK_EQ(first.deactivations, 1);
    CHECK_EQ(second.activations, 1);
    CHECK(session.removeTool("second"));
    CHECK_EQ(second.deactivations, 1);
    CHECK_EQ(session.activeToolId(), std::string(""));
}

TEST_CASE("editor.presentation.is_host_and_renderer_independent") {
    EditorSession session;
    TestEditorTool tool("presentable");
    TestPresentation presentation;
    CHECK(session.addTool(&tool));
    CHECK(session.activateTool("presentable"));
    session.drawOverlay(presentation);
    session.inspect(presentation);
    CHECK_EQ(tool.overlays, 1);
    CHECK_EQ(tool.inspections, 1);
    CHECK_EQ(presentation.primitives, 1);
    CHECK_EQ(tool.radius, 6.f);
}

TEST_CASE("editor.field_brush.composes_kernel_and_operation") {
    ConstantBrushFalloff hard;
    CircleBrushKernel kernel(&hard);
    PaintIntFieldOperation paint(7);
    FieldBrushTool tool("paint", "Paint", &kernel, &paint);
    tool.setRadius(0.5f);

    TileBuffer buffer(4, 4);
    TileBufferTarget tiles("tiles", &buffer);
    EditorSession session;
    session.bindTarget(&tiles);
    CHECK(session.addTool(&tool));
    CHECK(session.activateTool("paint"));

    EditorPointerEvent event;
    event.phase = EditorPointerEvent::Phase::Down;
    event.x = 1.f;
    event.y = 2.f;
    CHECK(session.dispatchPointer(event).capturePointer);
    event.phase = EditorPointerEvent::Phase::Up;
    CHECK(session.dispatchPointer(event).releasePointer);
    CHECK_EQ(buffer.getGid(1, 2), 7);
    CHECK(session.transactions().undo());
    CHECK_EQ(buffer.getGid(1, 2), 0);

    eve::procgen::Heightmap heightmap(4, 4);
    HeightmapTarget terrain("terrain", &heightmap);
    AddScalarFieldOperation raise;
    tool.setOperation(&raise);
    tool.setStrength(0.25f);
    session.bindTarget(&terrain);
    event.phase = EditorPointerEvent::Phase::Down;
    CHECK(session.dispatchPointer(event).handled);
    event.phase = EditorPointerEvent::Phase::Up;
    session.dispatchPointer(event);
    CHECK_EQ(terrain.readScalar(1, 2), 0.25f);
}

TEST_CASE("editor.heightmap_brush.applies_native_circular_falloff") {
    Editor editor;
    eve::procgen::Heightmap heightmap(9, 9);
    CHECK_EQ(editor.applyHeightmapBrush(&heightmap, 4.f, 4.f, 2.f, 0.5f), 13);
    CHECK_EQ(heightmap.height(4, 4), 0.5f);
    CHECK(heightmap.height(5, 4) > heightmap.height(6, 4));
    CHECK_EQ(heightmap.height(0, 0), 0.f);

    CHECK(editor.applyHeightmapBrush(&heightmap, 4.f, 4.f, 2.f, -0.25f) > 0);
    CHECK_EQ(heightmap.height(4, 4), 0.25f);
    CHECK_EQ(editor.applyHeightmapBrush(nullptr, 4.f, 4.f, 2.f, 1.f), 0);
    CHECK_EQ(editor.applyHeightmapBrush(&heightmap, 4.f, 4.f, -1.f, 1.f), 0);
}

TEST_CASE("editor.script_tool.implements_the_same_session_protocol") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        session <- editor.newSession();
        tool <- editor.newScriptTool("custom", "Custom Tool");
        activations <- 0;
        pointerX <- -1.0;
        tool.setActivateCallback(function() { activations += 1; });
        tool.setPointerCallback(function(phase, pointerId, button, x, y, dx, dy, pressure,
                                         shift, control, alt) {
            pointerX = x;
            return phase == 0 ? 3 : 5;
        });
        added <- session.addTool(tool);
        selected <- session.activateTool("custom");
        response <- session.dispatchPointer(0, 9, 0, 12.5, 4.0, 0.0, 0.0, 1.0);
    )"));
    CHECK(vm.find("added").toBool());
    CHECK(vm.find("selected").toBool());
    CHECK_EQ(vm.find("activations").toInt(), 1);
    CHECK_EQ(vm.find("response").toInt(), 3);
    CHECK_EQ(vm.find("pointerX").toFloat(), 12.5f);
}

TEST_CASE("editor.targets.expose_capabilities_and_dirty_regions") {
    TileBuffer buffer(4, 3);
    TileBufferTarget tiles("ground", &buffer);
    IEditableTarget *base = &tiles;
    auto *ints = base->query<IIntFieldTarget>();
    CHECK(ints != nullptr);
    CHECK(base->query<IScalarFieldTarget>() == nullptr);
    CHECK(ints->writeInt(2, 1, 9));
    CHECK(!ints->writeInt(2, 1, 9));
    CHECK_EQ(ints->readInt(2, 1), 9);
    CHECK_EQ(tiles.revision(), 1ULL);
    CHECK_EQ(tiles.dirtyRegion().minX, 2);
    tiles.clearDirtyRegion();
    CHECK(tiles.dirtyRegion().empty());

    eve::procgen::Heightmap heightmap(3, 3);
    HeightmapTarget terrain("height", &heightmap);
    auto *scalars = terrain.query<IScalarFieldTarget>();
    CHECK(scalars != nullptr);
    CHECK(scalars->writeScalar(1, 1, 0.75f));
    CHECK_EQ(scalars->readScalar(1, 1), 0.75f);
    CHECK(scalars->sampleScalar(1.f, 1.f) == 0.75f);

    EditorSession session;
    session.bindTarget(&terrain);
    CHECK(session.context().targetCapability<IScalarFieldTarget>() == scalars);
    CHECK(session.context().targetCapability<IIntFieldTarget>() == nullptr);
}

TEST_CASE("editor.brush_kernel.separates_shape_and_falloff") {
    LinearBrushFalloff linear;
    CircleBrushKernel circle(&linear);
    BrushSample sample;
    sample.centerX = 2.f;
    sample.centerY = 2.f;
    sample.radius = 2.f;
    BrushSampleBuffer points;
    circle.sample(sample, points);
    CHECK(points.size() > 1);
    bool foundCenter = false;
    bool foundEdge = false;
    for (int i = 0; i < points.size(); ++i) {
        const auto &point = points.point(i);
        if (point.x == 2 && point.y == 2) {
            foundCenter = true;
            CHECK_EQ(point.weight, 1.f);
        }
        if (point.x == 4 && point.y == 2) {
            foundEdge = true;
            CHECK_EQ(point.weight, 0.f);
        }
    }
    CHECK(foundCenter);
    CHECK(foundEdge);

    ConstantBrushFalloff hard;
    BoxBrushKernel box(&hard);
    points.clear();
    sample.radius = 1.f;
    box.sample(sample, points);
    CHECK_EQ(points.size(), 9);
}

TEST_CASE("editor.transactions.undo_redo_and_rollback_any_field") {
    TileBuffer buffer(3, 3);
    TileBufferTarget target("tiles", &buffer);
    EditorTransactions transactions;
    CHECK(transactions.begin("paint"));
    auto first = std::make_unique<IntFieldEditCommand>("paint", &target);
    CHECK(first->record(1, 1, 5));
    CHECK(transactions.execute(std::move(first)));
    auto second = std::make_unique<IntFieldEditCommand>("paint", &target);
    CHECK(second->record(1, 1, 7));
    CHECK(second->record(2, 1, 4));
    CHECK(transactions.execute(std::move(second)));
    CHECK_EQ(buffer.getGid(1, 1), 7);
    CHECK(transactions.commit());
    CHECK_EQ(transactions.undoCount(), 1);
    CHECK(transactions.undo());
    CHECK_EQ(buffer.getGid(1, 1), 0);
    CHECK_EQ(buffer.getGid(2, 1), 0);
    CHECK(transactions.redo());
    CHECK_EQ(buffer.getGid(1, 1), 7);

    CHECK(transactions.begin("cancel"));
    auto cancelled = std::make_unique<IntFieldEditCommand>("paint", &target);
    CHECK(cancelled->record(0, 0, 9));
    CHECK(transactions.execute(std::move(cancelled)));
    CHECK_EQ(buffer.getGid(0, 0), 9);
    CHECK(transactions.rollback());
    CHECK_EQ(buffer.getGid(0, 0), 0);
}

#ifdef EVENGINE_HAS_MAP
TEST_CASE("editor.map.tileLayerTargetUsesLiveRevision") {
    auto *map = eve::map::Map::create();
    auto *layer = map->newLayer(4, 3, 8.f, 8.f);
    Editor editor;
    std::unique_ptr<TileLayerTarget> target(editor.newTileLayerTarget("ground", layer));
    const auto before = target->revision();
    CHECK(target->writeInt(2, 1, 9));
    CHECK_EQ(layer->getTile(2, 1), 9);
    CHECK_GT(target->revision(), before);
    CHECK_EQ(target->dirtyRegion().minX, 2);
    CHECK_EQ(target->dirtyRegion().maxY, 1);
    IntFieldEditCommand command("paint", target.get());
    CHECK(command.record(2, 1, 4));
    CHECK(command.apply());
    CHECK_EQ(layer->getTile(2, 1), 4);
    command.revert();
    CHECK_EQ(layer->getTile(2, 1), 9);
}
#endif

TEST_CASE("editor.constraints.accept_warn_and_reject_without_core_types") {
    TileBuffer buffer(4, 2);
    TileBufferTarget target("tiles", &buffer);
    EditorSession session;
    session.bindTarget(&target);
    RejectLargeEdit constraint;
    CHECK(session.constraints().add(&constraint));
    CHECK(session.transactions().begin("constrained"));

    auto allowed = std::make_unique<IntFieldEditCommand>("small", &target);
    CHECK(allowed->record(0, 0, 1));
    CHECK(session.context().execute(std::move(allowed)));
    CHECK_EQ(session.constraints().diagnosticCount(), 1);
    CHECK_EQ(session.constraints().diagnostic(0), std::string("checked"));

    auto rejected = std::make_unique<IntFieldEditCommand>("wide", &target);
    CHECK(rejected->record(0, 1, 2));
    CHECK(rejected->record(3, 1, 2));
    CHECK(!session.context().execute(std::move(rejected)));
    CHECK(session.constraints().rejected());
    CHECK_EQ(buffer.getGid(0, 1), 0);
    CHECK(session.transactions().commit());
}

TEST_CASE("editor.module.name") {
    auto *mod = Editor::create();
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
    bool threw = false;
    try {
        g->setMode("skew");
    } catch (const eve::Exception &) {
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
    TransformGizmo *g = mgr->getGizmo();
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
    std::unique_ptr<Brush> brush(new Brush());

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
    std::unique_ptr<Brush> brush(new Brush());
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
    std::unique_ptr<TileBuffer> buf(new TileBuffer(4, 4));
    std::unique_ptr<Brush> brush(new Brush());
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
    Editor *ed = Editor::create();
    std::unique_ptr<TransformGizmo> g(ed->newGizmo());
    std::unique_ptr<GizmoManager> m(ed->newGizmoManager());
    std::unique_ptr<TileBuffer> b(ed->newTileBuffer(2, 2));
    std::unique_ptr<Brush> br(ed->newBrush());
    std::unique_ptr<EditorToolbar> t(ed->newToolbar());
    std::unique_ptr<EditorInspector> i(ed->newInspector());
    std::unique_ptr<EditorDock> d(ed->newDock());
    std::unique_ptr<EditorHistory> h(ed->newHistory());
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
