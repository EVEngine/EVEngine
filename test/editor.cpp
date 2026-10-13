#include "editor/Editor.h"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include <stdexcept>
#include <string>
#include "common/Exception.h"
#include "common/Module.h"
#include "editor/BrushKernel.h"
#include "editor/EditCommand.h"
#include "editor/EditConstraint.h"
#include "editor/EditorCommandService.h"
#include "editor/EditorDock.h"
#include "editor/EditorInspector.h"
#include "editor/EditorPresentation.h"
#include "editor/EditorSession.h"
#include "editor/EditorTargetCoordinator.h"
#include "editor/EditorToolbar.h"
#include "editor/EditorTransactions.h"
#include "editor/EditorWorkspace.h"
#include "editor/FieldBrushTool.h"
#include "editor/GizmoManager.h"
#include "editor/TransformGizmo.h"
#include "map/level/editing/Brush.h"
#include "map/level/editing/EditorHistory.h"
#include "map/level/editing/FieldTargets.h"
#include "map/level/editing/TileBuffer.h"
#include "map/level/editor/LevelEditorModule.h"
#include "procgen/editing/HeightmapTarget.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap_target/HeightmapTargetModule.h"
#include "scene/editor/EditorSceneTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// editor.cpp
using namespace eve::editor;
using namespace eve::level_editing;

namespace {
class TestEditorTool final : public IEditorTool {
public:
    explicit TestEditorTool(std::string id) { desc.id = std::move(id); }
    const ToolDescriptor &descriptor() const override { return desc; }
    void                  activate(EditorContext &) override { ++activations; }
    void                  deactivate(EditorContext &) override { ++deactivations; }
    void                  cancel(EditorContext &) override { ++cancellations; }
    ToolResponse          pointerEvent(EditorContext &, const EditorPointerEvent &event) override {
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
    int            activations   = 0;
    int            deactivations = 0;
    int            cancellations = 0;
    int            pointerEvents = 0;
    int            keyEvents     = 0;
    int            updates       = 0;
    int            overlays      = 0;
    int            inspections   = 0;
    float          radius        = 2.f;
};

class ReentrantEditorTool final : public IEditorTool {
public:
    explicit ReentrantEditorTool(std::string id) { desc.id = std::move(id); }
    const ToolDescriptor &descriptor() const override { return desc; }
    ToolResponse          pointerEvent(EditorContext &context, const EditorPointerEvent &) override {
        if (activateDuringPointer) context.session()->activateTool("replacement");
        return ToolResponse::capture();
    }
    void deactivate(EditorContext &context) override {
        if (removeDuringDeactivate) context.session()->removeTool(desc.id);
        if (throwDuringDeactivate) throw std::runtime_error("deactivate failure");
    }
    void cancel(EditorContext &) override {
        if (throwDuringCancel) throw std::runtime_error("cancel failure");
    }

    ToolDescriptor desc;
    bool           activateDuringPointer  = false;
    bool           removeDuringDeactivate = false;
    bool           throwDuringCancel      = false;
    bool           throwDuringDeactivate  = false;
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
    EditorSession  session;
    TestEditorTool first("first");
    TestEditorTool second("second");
    CHECK(session.addTool(&first));
    CHECK(session.addTool(&second));
    CHECK(!session.addTool(&first));
    CHECK_EQ(session.getToolCount(), 2);
    CHECK(session.activateTool("first"));
    CHECK_EQ(first.activations, 1);

    EditorPointerEvent down;
    down.phase     = EditorPointerEvent::Phase::Down;
    down.pointerId = 7;
    CHECK(session.dispatchPointer(down).handled);
    CHECK(session.hasPointerCapture());
    CHECK_EQ(session.capturedPointerId(), 7);

    EditorPointerEvent other = down;
    other.phase              = EditorPointerEvent::Phase::Move;
    other.pointerId          = 8;
    CHECK(!session.dispatchPointer(other).handled);
    CHECK_EQ(first.pointerEvents, 1);

    EditorPointerEvent up = down;
    up.phase              = EditorPointerEvent::Phase::Up;
    CHECK(session.dispatchPointer(up).releasePointer);
    CHECK(!session.hasPointerCapture());

    EditorKeyEvent key;
    key.key     = "W";
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

TEST_CASE("editor.session_tool_reentrancy_cannot_apply_stale_pointer_capture") {
    EditorSession       session;
    ReentrantEditorTool first("first");
    ReentrantEditorTool replacement("replacement");
    first.activateDuringPointer  = true;
    first.removeDuringDeactivate = true;
    REQUIRE(session.addTool(&first));
    REQUIRE(session.addTool(&replacement));
    REQUIRE(session.activateTool("first"));

    EditorPointerEvent down;
    down.phase                  = EditorPointerEvent::Phase::Down;
    down.pointerId              = 17;
    const ToolResponse response = session.dispatchPointer(down);
    CHECK(response.handled);
    CHECK(!response.capturePointer);
    CHECK(!session.hasPointerCapture());
    CHECK_EQ(session.activeToolId(), std::string("replacement"));
    CHECK(session.findTool("first") == nullptr);
}

TEST_CASE("editor.session_destructor_contains_throwing_tool_callbacks") {
    ReentrantEditorTool tool("throwing");
    tool.throwDuringCancel     = true;
    tool.throwDuringDeactivate = true;
    {
        EditorSession session;
        REQUIRE(session.addTool(&tool));
        REQUIRE(session.activateTool("throwing"));
    }
    CHECK(true);
}

TEST_CASE("editor.presentation.is_host_and_renderer_independent") {
    EditorSession    session;
    TestEditorTool   tool("presentable");
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
    ConstantBrushFalloff   hard;
    CircleBrushKernel      kernel(&hard);
    PaintIntFieldOperation paint(7);
    FieldBrushTool         tool("paint", "Paint", &kernel, &paint);
    tool.setRadius(0.5f);

    TileBuffer       buffer(4, 4);
    TileBufferTarget tiles("tiles", &buffer);
    EditorSession    session;
    session.bindTarget(&tiles);
    CHECK(session.addTool(&tool));
    CHECK(session.activateTool("paint"));

    EditorPointerEvent event;
    event.phase = EditorPointerEvent::Phase::Down;
    event.x     = 1.f;
    event.y     = 2.f;
    CHECK(session.dispatchPointer(event).capturePointer);
    event.phase = EditorPointerEvent::Phase::Up;
    CHECK(session.dispatchPointer(event).releasePointer);
    CHECK_EQ(buffer.getGid(1, 2), 7);
    CHECK(session.transactions().undo());
    CHECK_EQ(buffer.getGid(1, 2), 0);

    eve::procgen::Heightmap               heightmap(4, 4);
    eve::procgen_editing::HeightmapTarget terrain("terrain", &heightmap);
    AddScalarFieldOperation               raise;
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
    eve::heightmap_target::HeightmapTargetModule heightmapTargets;
    eve::procgen::Heightmap                      heightmap(9, 9);
    CHECK_EQ(heightmapTargets.applyBrush(&heightmap, 4.f, 4.f, 2.f, 0.5f), 13);
    CHECK_EQ(heightmap.height(4, 4), 0.5f);
    CHECK(heightmap.height(5, 4) > heightmap.height(6, 4));
    CHECK_EQ(heightmap.height(0, 0), 0.f);

    CHECK(heightmapTargets.applyBrush(&heightmap, 4.f, 4.f, 2.f, -0.25f) > 0);
    CHECK_EQ(heightmap.height(4, 4), 0.25f);
    CHECK_EQ(heightmapTargets.applyBrush(nullptr, 4.f, 4.f, 2.f, 1.f), 0);
    CHECK_EQ(heightmapTargets.applyBrush(&heightmap, 4.f, 4.f, -1.f, 1.f), 0);
}

TEST_CASE("editor.heightmap_document.module_round_trips_without_partial_mutation") {
    eve::heightmap_target::HeightmapTargetModule heightmapTargets;
    eve::procgen::Heightmap                      heightmap(3, 2);
    heightmap.setHeight(1, 1, 42.5F);
    auto encoded = heightmapTargets.encodeDocument(&heightmap, 2.0F, 3.0F);
    REQUIRE(encoded.ok());
    heightmap.setHeight(1, 1, -1.0F);
    REQUIRE(heightmapTargets.decodeDocument(encoded.value(), &heightmap).ok());
    CHECK_EQ(heightmap.getWidth(), 3);
    CHECK_EQ(heightmap.getHeight(), 2);
    CHECK_EQ(heightmap.height(1, 1), 42.5F);

    const auto before  = heightmap.data();
    auto       invalid = heightmapTargets.decodeDocument("{\"schema\":\"wrong\"}", &heightmap);
    CHECK(!invalid.ok());
    CHECK_EQ(heightmap.data(), before);
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
    TileBuffer       buffer(4, 3);
    TileBufferTarget tiles("ground", &buffer);
    IEditableTarget *base = &tiles;
    auto            *ints = base->query<IIntFieldTarget>();
    CHECK(ints != nullptr);
    CHECK(base->query<IScalarFieldTarget>() == nullptr);
    CHECK_EQ(static_cast<int>(ints->writeInt(2, 1, 9)), static_cast<int>(FieldWriteStatus::Applied));
    CHECK_EQ(static_cast<int>(ints->writeInt(2, 1, 9)), static_cast<int>(FieldWriteStatus::Unchanged));
    CHECK_EQ(ints->readInt(2, 1), 9);
    CHECK_EQ(tiles.revision(), 1ULL);
    CHECK_EQ(tiles.dirtyRegion().minX, 2);
    tiles.clearDirtyRegion();
    CHECK(tiles.dirtyRegion().empty());

    eve::procgen::Heightmap               heightmap(3, 3);
    eve::procgen_editing::HeightmapTarget terrain("height", &heightmap);
    auto                                 *scalars = terrain.query<IScalarFieldTarget>();
    CHECK(scalars != nullptr);
    CHECK_EQ(static_cast<int>(scalars->writeScalar(1, 1, 0.75f)), static_cast<int>(FieldWriteStatus::Applied));
    CHECK_EQ(scalars->readScalar(1, 1), 0.75f);
    CHECK(scalars->sampleScalar(1.f, 1.f) == 0.75f);

    EditorSession session;
    session.bindTarget(&terrain);
    CHECK(session.context().targetCapability<IScalarFieldTarget>() == scalars);
    CHECK(session.context().targetCapability<IIntFieldTarget>() == nullptr);
}

TEST_CASE("editor.brush_kernel.separates_shape_and_falloff") {
    LinearBrushFalloff linear;
    CircleBrushKernel  circle(&linear);
    BrushSample        sample;
    sample.centerX = 2.f;
    sample.centerY = 2.f;
    sample.radius  = 2.f;
    BrushSampleBuffer points;
    circle.sample(sample, points);
    CHECK(points.size() > 1);
    bool foundCenter = false;
    bool foundEdge   = false;
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
    BoxBrushKernel       box(&hard);
    points.clear();
    sample.radius = 1.f;
    box.sample(sample, points);
    CHECK_EQ(points.size(), 9);
}

TEST_CASE("editor.transactions.undo_redo_and_rollback_any_field") {
    TileBuffer         buffer(3, 3);
    TileBufferTarget   target("tiles", &buffer);
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

TEST_CASE("editor.constraints.accept_warn_and_reject_without_core_types") {
    TileBuffer       buffer(4, 2);
    TileBufferTarget target("tiles", &buffer);
    EditorSession    session;
    session.bindTarget(&target);
    RejectLargeEdit constraint;
    CHECK(session.constraints().add(&constraint));
    CHECK(session.transactions().begin("constrained"));

    auto allowed = std::make_unique<IntFieldEditCommand>("small", &target);
    CHECK(allowed->record(0, 0, 1));
    auto allowedResult = session.context().executeChecked(std::move(allowed));
    CHECK(allowedResult.ok());
    REQUIRE(!allowedResult.diagnostics().empty());
    CHECK_EQ(static_cast<int>(allowedResult.diagnostics().front().severity()),
             static_cast<int>(DiagnosticSeverity::Warning));
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
    bool                            threw = false;
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
    tb->setIcon("move", "move");
    tb->setIcon("paint", "paint-brush");
    tb->setShortcut("move", "W");
    CHECK_EQ(tb->getActive(), std::string("move"));
    CHECK(tb->matchShortcut("W"));
    CHECK(tb->setActive("paint"));
    CHECK_EQ(tb->getActive(), std::string("paint"));
    CHECK_EQ(tb->getToolCount(), 2);
    CHECK_EQ(tb->getToolIcon(0), std::string("move"));
    CHECK_EQ(tb->getToolIcon(1), std::string("paint-brush"));

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
    Editor                              *ed = Editor::create();
    eve::level_editor::LevelEditorModule levelEditor;
    std::unique_ptr<TransformGizmo>      g         = ed->newGizmo();
    std::unique_ptr<GizmoManager>        m         = ed->newGizmoManager();
    std::unique_ptr<TileBuffer>          b         = levelEditor.newTileBuffer(2, 2);
    std::unique_ptr<Brush>               br        = levelEditor.newBrush();
    std::unique_ptr<EditorToolbar>       t         = ed->newToolbar();
    std::unique_ptr<EditorInspector>     i         = ed->newInspector();
    std::unique_ptr<EditorHistory>       h         = levelEditor.newHistory();
    std::unique_ptr<EditorDock>          d         = ed->newDock();
    std::unique_ptr<EditorSession>       s         = ed->newSession();
    auto                                 workspace = ed->newWorkspace("level", "Level");
    auto                                 rejected  = ed->newWorkspace("", "nope");
    // zeroerr CHECK copies the expression for printing — use raw pointers, not unique_ptr.
    CHECK(g.get() != nullptr);
    CHECK(m.get() != nullptr);
    CHECK_EQ(b->getWidth(), 2);
    CHECK(br.get() != nullptr);
    CHECK(t.get() != nullptr);
    CHECK(i.get() != nullptr);
    CHECK(d.get() != nullptr);
    CHECK(h.get() != nullptr);
    CHECK(s.get() != nullptr);
    CHECK(workspace.ok());
    CHECK_EQ(workspace.value()->getId(), std::string("level"));
    CHECK_NOT(rejected.ok());
    CHECK_NOT(rejected.hasValue());
}

// editor_action_timeline_script.cpp
TEST_CASE("editor.actionTimeline.scriptUsesCanonicalTransactionsAndWorkspace") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        workspace <- editor.newWorkspace("test.combat", "Test Combat Editor");
        timelineAsset <- {
            schema="eve.action.timeline", schemaVersion=1,
            actionId="test:light-attack", durationNs=1000000000,
            animationUri="asset://test/attack.glb#Attack",
            metadata={},
            tracks=[{
                id="test-track:gameplay", label="Gameplay", kind="gameplay",
                muted=false, locked=false, notifies=[],
                states=[{
                    id="test-state:hitbox", type="combat:hitbox-window",
                    startNs=200000000, endNs=500000000,
                    payload={hitbox="weapon.main"}
                }]
            }]
        };
        created <- eve.ActionEditorModule().create("test.asset.light-attack", timelineAsset);
        actionEditor <- created.value;
        externalDamage <- actionEditor.setRuntimeBlockExternallyHandled("combat:damage", true);
        externalUnknown <- actionEditor.setRuntimeBlockExternallyHandled("test:unknown", true);
        previewHostAvailable <- actionEditor.hasPreviewHost();
        previewRefresh <- actionEditor.refreshPreview();
        runtimeRateBeforeStart <- actionEditor.setRuntimeRate(1.5);
        sectionProgressBeforeStart <- actionEditor.evaluateRuntimeSectionProgress(0, 0.5);
        sectionSyncJumpBeforeStart <- actionEditor.syncRuntimeSectionAndJump(0, 0.25);
        clearSectionSyncBeforeStart <- actionEditor.clearRuntimeSectionSync(0);
        testAnimation <- eve.Animation();
        testSkeleton <- testAnimation.newSkeleton();
        testBoneMask <- testAnimation.newBoneMask(testSkeleton);
        testPose <- testAnimation.newPose(testSkeleton.getBoneCount());
        setBoneMaskBeforeStart <- actionEditor.setRuntimeBoneMask(testBoneMask);
        clearBoneMaskBeforeStart <- actionEditor.clearRuntimeBoneMask();
        setLayerWeightBeforeStart <- actionEditor.setRuntimeLayerWeight(0.5);
        setLayerAdditiveBeforeStart <- actionEditor.setRuntimeLayerAdditive(true);
        runtimeLayerWeightBeforeStart <- actionEditor.getRuntimeLayerWeight();
        runtimeLayerAdditiveBeforeStart <- actionEditor.getRuntimeLayerAdditive();
        runtimePoseOverBaseBeforeStart <- actionEditor.getRuntimePoseOverBase(testPose);
        runtimeStateBeforeStart <- actionEditor.getRuntimeState();
        runtimeElapsedBeforeStart <- actionEditor.getRuntimeElapsedSeconds();
        runtimeDurationBeforeStart <- actionEditor.getRuntimeDurationSeconds();
        runtimeLayerBeforeStart <- actionEditor.getRuntimeLayer();
        runtimeSlotBeforeStart <- actionEditor.getRuntimeSlot();
        configured <- actionEditor.configureWorkspace(workspace);
        workspacePanelCount <- workspace.getPanelCount();
        viewport <- actionEditor.setViewport(800.0, 36.0, 120.0);
        beforeStart <- actionEditor.getStateStart("test-state:hitbox");
        itemCenterX <- (actionEditor.getItemMinX(0) + actionEditor.getItemMaxX(0)) * 0.5;
        itemCenterY <- (actionEditor.getItemMinY(0) + actionEditor.getItemMaxY(0)) * 0.5;
        pointerDownResult <- actionEditor.pointerDown(itemCenterX, itemCenterY, false);
        pointerMoveResult <- actionEditor.pointerMove(itemCenterX + 80.0);
        pointerUpResult <- actionEditor.pointerUp(itemCenterX + 80.0);
        movedStart <- actionEditor.getStateStart("test-state:hitbox");
        revisionAfterDrag <- actionEditor.getRevision();
        undoResult <- actionEditor.undo();
        restoredStart <- actionEditor.getStateStart("test-state:hitbox");
        inspectorTiming <- actionEditor.setItemTiming("test-state:hitbox", 0.1, 0.6);
        inspectorStart <- actionEditor.getItemStart("test-state:hitbox");
        inspectorEnd <- actionEditor.getItemEnd("test-state:hitbox");
        invalidInspectorTiming <- actionEditor.setItemTiming("test-state:hitbox", 0.7, 0.2);
        detailsResult <- actionEditor.editItemDetails(
            "test-state:hitbox", "combat:hitbox-window", "{\"hitbox\":\"weapon.alt\"}");
        payloadAfterDetails <- actionEditor.getItemPayloadJson("test-state:hitbox");
        payloadTextPatch <- actionEditor.setItemPayloadText(
            "test-state:hitbox", "hitbox", "weapon.typed");
        typedHitbox <- actionEditor.getItemPayloadText(
            "test-state:hitbox", "hitbox", "missing");
        payloadVectorPatch <- actionEditor.setItemPayloadVector3(
            "test-state:hitbox", "positionOffset", 1.0, 2.0, 3.0);
        typedPositionY <- actionEditor.getItemPayloadVector(
            "test-state:hitbox", "positionOffset", 1, -1.0);
        payloadTextListPatch <- actionEditor.setItemPayloadTextList(
            "test-state:hitbox", "relatedAssets", "asset://audio/a.wav; asset://audio/b.wav");
        typedTextList <- actionEditor.getItemPayloadTextList("test-state:hitbox", "relatedAssets");
        invalidPayloadPatch <- actionEditor.setItemPayloadText(
            "test-state:hitbox", "hitbox", "");
        shapeMismatch <- actionEditor.editItemDetails(
            "test-state:hitbox", "combat:damage", "{\"damageType\":\"slash\",\"amount\":2}");
        invalidPayload <- actionEditor.editItemDetails(
            "test-state:hitbox", "combat:hitbox-window", "{}");
        disableResult <- actionEditor.setItemEnabled("test-state:hitbox", false);
        disabledValue <- actionEditor.getItemEnabled("test-state:hitbox");
        restoreEnabledResult <- actionEditor.undo();
        restoredEnabledValue <- actionEditor.getItemEnabled("test-state:hitbox");
        removeItemResult <- actionEditor.removeItem("test-state:hitbox");
        itemCountAfterRemove <- actionEditor.getItemCount();
        restoreRemovedResult <- actionEditor.undo();
        itemCountAfterRestore <- actionEditor.getItemCount();
        missResult <- actionEditor.pointerDown(700.0, 20.0, false);
        draggingAfterMiss <- actionEditor.isDragging();
        seekResult <- actionEditor.seekX(700.0);
        emptySpaceSeekTime <- actionEditor.getPreviewTime();
        actionEditor.seekSeconds(0.0);
        actionEditor.play();
        advanced <- actionEditor.update(1.0);
        previewEventCount <- actionEditor.getEventCount();
        snapshotResult <- actionEditor.snapshot();
        timelineAbilitySplitA <- actionEditor.addSectionSplit(0.2);
        timelineAbilitySplitB <- actionEditor.addSectionSplit(0.6);
        snapshotJsonResult <- actionEditor.snapshotJson();
        combatRuntime <- eve.Combat().newRuntime().value;
        timelineAbility <- combatRuntime.registerTimelineAbility(
            "combat-ability:authored-light", snapshotJsonResult.value, 0.25,
            "per-execution", "exclusive-replaceable", "Ability.Combat.Attack.Light");
        duplicateTimelineAbility <- combatRuntime.registerTimelineAbility(
            "combat-ability:authored-light", snapshotJsonResult.value, 0.25,
            "per-execution", "exclusive-replaceable", "Ability.Combat.Attack.Light");
        invalidTimelineAbility <- combatRuntime.registerTimelineAbility(
            "combat-ability:invalid-policy", snapshotJsonResult.value, 0.25,
            "per-execution", "invalid", "Ability.Combat.Attack.Light");
        invalidTimelineGrant <- combatRuntime.grantAbility("fighter:player", "combat-ability:invalid-policy");
        timelineGrant <- combatRuntime.grantAbility("fighter:player", "combat-ability:authored-light");
        timelineMatches <- combatRuntime.matchingAbilities("fighter:player", "Ability.Combat.Attack.Light");
        timelineActivation <- combatRuntime.activateAbility(timelineGrant.value.grantId, 1);
        actionEditor.setSectionSplit(0, 0.3);
        actionEditor.setSectionSplit(1, 0.8);
        replacementJson <- actionEditor.snapshotJson();
        replacedTimelineAbility <- combatRuntime.replaceTimelineAbility(
            "combat-ability:authored-light", replacementJson.value, 0.0,
            "per-execution", "exclusive-replaceable", "Ability.Combat.Attack.Light");
        timelineAbilityAdvance <- combatRuntime.advanceAbilities(2, 1.0);
        timelineActiveAfterAdvance <- timelineAbilityAdvance.value.activeCount;
        replacementActivation <- combatRuntime.activateAbility(timelineGrant.value.grantId, 3);
        replacementAdvance <- combatRuntime.advanceAbilities(4, 0.5);
        timelineAbilityAction <- timelineAbility.value.actionId;
        timelineAbilityDuration <- timelineAbility.value.durationSeconds;
        timelineAbilitySections <- timelineAbility.value.sectionCount;
        timelineMatchedGrant <- timelineMatches.value[0];
        timelineGrantedId <- timelineGrant.value.grantId;
        replacementAbilityAction <- replacedTimelineAbility.value.actionId;
        replacementStillActive <- replacementAdvance.value.activeCount;
        actionEditor.removeSectionSplit(1);
        actionEditor.removeSectionSplit(0);
        instantTypeCount <- actionEditor.getInsertableTypeCount(false);
        stateTypeCount <- actionEditor.getInsertableTypeCount(true);
        damageTypeIndex <- -1;
        invulnerabilityTypeIndex <- -1;
        for (local i = 0; i < instantTypeCount; ++i)
            if (actionEditor.getInsertableType(false, i) == "combat:damage") damageTypeIndex = i;
        for (local i = 0; i < stateTypeCount; ++i)
            if (actionEditor.getInsertableType(true, i) == "combat:invulnerability-window")
                invulnerabilityTypeIndex = i;
        damageTypeLabel <- actionEditor.getInsertableTypeLabel(false, damageTypeIndex);
        actionEditor.seekSeconds(0.7);
        insertedNotify <- actionEditor.addNotifyAtCursor(
            "test-track:gameplay", "combat:damage", "{\"damageType\":\"slash\",\"amount\":3}");
        insertedState <- actionEditor.addStateAtCursor(
            "test-track:gameplay", "combat:invulnerability-window", 0.1, "{}");
        invalidInsertedNotify <- actionEditor.addNotifyAtCursor(
            "test-track:gameplay", "combat:damage", "{}");
        itemCountAfterInsert <- actionEditor.getItemCount();
        initialTrackLocked <- actionEditor.getTrackLocked(0);
        lockTrack <- actionEditor.setTrackLocked("test-track:gameplay", true);
        lockedTrackValue <- actionEditor.getTrackLocked(0);
        unlockTrack <- actionEditor.setTrackLocked("test-track:gameplay", false);
        renameTrack <- actionEditor.renameTrack("test-track:gameplay", "Combat Events");
        renamedTrackLabel <- actionEditor.getTrackLabel(0);
        copyTrackResult <- actionEditor.copyTrack("test-track:gameplay");
        pasteTrackResult <- actionEditor.pasteTrack();
        trackCountAfterPaste <- actionEditor.getTrackCount();
        removePastedTrack <- actionEditor.removeTrack(pasteTrackResult.value);
        trackCountAfterTrackCleanup <- actionEditor.getTrackCount();
        addSectionResult <- actionEditor.addAnimationSection(
            "test-section:attack", "asset://test/attack.glb#Attack", 0.0, 0.4, 0.1);
        editSectionResult <- actionEditor.editAnimationSection(
            "test-section:attack", 0.05, 0.45, 0.08, "asset://test/attack.glb#AttackEdited");
        editSectionSourceResult <- actionEditor.editAnimationSectionSource(
            "test-section:attack", 0.1, 0.3, "linear");
        sectionUri <- actionEditor.getAnimationSectionUri(0);
        sectionStart <- actionEditor.getAnimationSectionStart(0);
        sectionBlend <- actionEditor.getAnimationSectionBlendIn(0);
        sectionSourceStart <- actionEditor.getAnimationSectionSourceStart(0);
        sectionCurve <- actionEditor.getAnimationSectionBlendCurve(0);
        removeSectionResult <- actionEditor.removeAnimationSection("test-section:attack");
        sectionCountAfterCleanup <- actionEditor.getAnimationSectionCount();
        montageSettingsResult <- actionEditor.setMontageSettings(
            1.5, true, true, 2, 0.12, 0.18, -0.03, true, false, false);
        montageRate <- actionEditor.getMontageBasePlayRate();
        montageLooping <- actionEditor.getMontageLooping();
        montageFootIk <- actionEditor.getMontageFootIk();
        montageLayer <- actionEditor.getMontageAnimationLayer();
        montageBlendOffset <- actionEditor.getMontageBlendOutOffset();
        montageRootRotation <- actionEditor.getMontageRootMotionRotation();
        invalidMontageSettings <- actionEditor.setMontageSettings(
            0.0, true, true, 2, 0.12, 0.18, -0.03, true, false, false);
        undoMontageSettings <- actionEditor.undo();
        restoredMontageRate <- actionEditor.getMontageBasePlayRate();
        splitCountBefore <- actionEditor.getSectionSplitCount();
        addSplit <- actionEditor.addSectionSplit(0.25);
        splitTimeAfterAdd <- actionEditor.getSectionSplitTime(0);
        moveSplit <- actionEditor.setSectionSplit(0, 0.35);
        splitTimeAfterMove <- actionEditor.getSectionSplitTime(0);
        duplicateSplit <- actionEditor.addSectionSplit(0.35);
        removeSplit <- actionEditor.removeSectionSplit(0);
        splitCountAfterRemove <- actionEditor.getSectionSplitCount();
        invalidResult <- eve.ActionEditorModule().create("test.asset.invalid", {
            schema="eve.action.timeline", schemaVersion=1,
            actionId="test:invalid", durationNs=-1,
            animationUri="", metadata={}, tracks=[]
        });
    )"));

    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK(vm.find("externalDamage").toTable().get<bool>("ok"));
    CHECK(!vm.find("externalUnknown").toTable().get<bool>("ok"));
    CHECK(!vm.find("previewHostAvailable").toBool());
    CHECK(vm.find("previewRefresh").toTable().get<bool>("ok"));
    CHECK(!vm.find("runtimeRateBeforeStart").toTable().get<bool>("ok"));
    CHECK(!vm.find("sectionProgressBeforeStart").toTable().get<bool>("ok"));
    CHECK(!vm.find("sectionSyncJumpBeforeStart").toTable().get<bool>("ok"));
    CHECK(!vm.find("clearSectionSyncBeforeStart").toTable().get<bool>("ok"));
    CHECK(!vm.find("setBoneMaskBeforeStart").toTable().get<bool>("ok"));
    CHECK(!vm.find("clearBoneMaskBeforeStart").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("runtimeStateBeforeStart").toString(), std::string("not-started"));
    CHECK_EQ(vm.find("runtimeElapsedBeforeStart").toFloat(), 0.0f);
    CHECK_EQ(vm.find("runtimeDurationBeforeStart").toFloat(), 0.0f);
    CHECK_EQ(vm.find("runtimeLayerBeforeStart").toInt(), -1);
    CHECK_EQ(vm.find("runtimeSlotBeforeStart").toInt(), -1);
    CHECK(vm.find("configured").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("workspacePanelCount").toInt(), 4);
    CHECK(vm.find("viewport").toTable().get<bool>("ok"));
    CHECK(vm.find("pointerDownResult").toTable().get<bool>("ok"));
    CHECK(vm.find("pointerMoveResult").toTable().get<bool>("ok"));
    CHECK(vm.find("pointerUpResult").toTable().get<bool>("ok"));
    CHECK(vm.find("movedStart").toFloat() > vm.find("beforeStart").toFloat());
    CHECK_EQ(vm.find("revisionAfterDrag").toInt(), 1);
    CHECK(vm.find("undoResult").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("restoredStart").toFloat() - vm.find("beforeStart").toFloat()), 0.0001F);
    CHECK(vm.find("inspectorTiming").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("inspectorStart").toFloat() - 0.1F), 0.0001F);
    CHECK_LT(std::abs(vm.find("inspectorEnd").toFloat() - 0.6F), 0.0001F);
    CHECK(!vm.find("invalidInspectorTiming").toTable().get<bool>("ok"));
    CHECK(vm.find("detailsResult").toTable().get<bool>("ok"));
    CHECK_NE(vm.find("payloadAfterDetails").toString().find("weapon.alt"), std::string::npos);
    CHECK(vm.find("payloadTextPatch").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("typedHitbox").toString(), std::string("weapon.typed"));
    CHECK(vm.find("payloadVectorPatch").toTable().get<bool>("ok"));
    CHECK(std::fabs(vm.find("typedPositionY").toFloat() - 2.0f) < 1e-5f);
    CHECK(vm.find("payloadTextListPatch").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("typedTextList").toString(), std::string("asset://audio/a.wav; asset://audio/b.wav"));
    CHECK(!vm.find("invalidPayloadPatch").toTable().get<bool>("ok"));
    CHECK(!vm.find("shapeMismatch").toTable().get<bool>("ok"));
    CHECK(!vm.find("invalidPayload").toTable().get<bool>("ok"));
    CHECK(vm.find("disableResult").toTable().get<bool>("ok"));
    CHECK(!vm.find("disabledValue").toBool());
    CHECK(vm.find("restoreEnabledResult").toTable().get<bool>("ok"));
    CHECK(vm.find("restoredEnabledValue").toBool());
    CHECK(vm.find("removeItemResult").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("itemCountAfterRemove").toInt(), 0);
    CHECK(vm.find("restoreRemovedResult").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("itemCountAfterRestore").toInt(), 1);
    CHECK(vm.find("missResult").toTable().get<bool>("ok"));
    CHECK(!vm.find("draggingAfterMiss").toBool());
    CHECK(vm.find("seekResult").toTable().get<bool>("ok"));
    CHECK(vm.find("emptySpaceSeekTime").toFloat() > 0.8F);
    CHECK(vm.find("advanced").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("previewEventCount").toInt(), 2);
    CHECK(vm.find("snapshotResult").toTable().get<bool>("ok"));
    CHECK(vm.find("snapshotJsonResult").toTable().get<bool>("ok"));
    CHECK(vm.find("timelineAbilitySplitA").toTable().get<bool>("ok"));
    CHECK(vm.find("timelineAbilitySplitB").toTable().get<bool>("ok"));
    CHECK(vm.find("timelineAbility").toTable().get<bool>("ok"));
    CHECK(!vm.find("duplicateTimelineAbility").toTable().get<bool>("ok"));
    CHECK(!vm.find("invalidTimelineAbility").toTable().get<bool>("ok"));
    CHECK(!vm.find("invalidTimelineGrant").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("timelineAbilityAction").toString(), std::string("test:light-attack"));
    CHECK_LT(std::abs(vm.find("timelineAbilityDuration").toFloat() - 1.0f), 1e-5f);
    CHECK_EQ(vm.find("timelineAbilitySections").toInt(), 3);
    CHECK_EQ(vm.find("timelineMatchedGrant").toInt(), vm.find("timelineGrantedId").toInt());
    CHECK(vm.find("timelineActivation").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("timelineActiveAfterAdvance").toInt(), 0);
    CHECK(vm.find("replacementJson").toTable().get<bool>("ok"));
    CHECK(vm.find("replacedTimelineAbility").toTable().get<bool>("ok"));
    CHECK(vm.find("replacementActivation").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("replacementAbilityAction").toString(), std::string("test:light-attack"));
    CHECK_EQ(vm.find("replacementStillActive").toInt(), 1);
    CHECK(vm.find("instantTypeCount").toInt() > 0);
    CHECK(vm.find("stateTypeCount").toInt() > 0);
    CHECK(vm.find("damageTypeIndex").toInt() >= 0);
    CHECK(vm.find("invulnerabilityTypeIndex").toInt() >= 0);
    CHECK_NE(vm.find("damageTypeLabel").toString().find("Apply Damage"), std::string::npos);
    CHECK(vm.find("insertedNotify").toTable().get<bool>("ok"));
    CHECK(vm.find("insertedState").toTable().get<bool>("ok"));
    CHECK(!vm.find("invalidInsertedNotify").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("itemCountAfterInsert").toInt(), 3);
    CHECK(!vm.find("initialTrackLocked").toBool());
    CHECK(vm.find("lockTrack").toTable().get<bool>("ok"));
    CHECK(vm.find("lockedTrackValue").toBool());
    CHECK(vm.find("unlockTrack").toTable().get<bool>("ok"));
    CHECK(vm.find("renameTrack").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("renamedTrackLabel").toString(), std::string("Combat Events"));
    CHECK(vm.find("copyTrackResult").toTable().get<bool>("ok"));
    CHECK(vm.find("pasteTrackResult").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("trackCountAfterPaste").toInt(), 2);
    CHECK(vm.find("removePastedTrack").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("trackCountAfterTrackCleanup").toInt(), 1);
    CHECK(vm.find("addSectionResult").toTable().get<bool>("ok"));
    CHECK(vm.find("editSectionResult").toTable().get<bool>("ok"));
    CHECK(vm.find("editSectionSourceResult").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("sectionUri").toString(), std::string("asset://test/attack.glb#AttackEdited"));
    CHECK(std::fabs(vm.find("sectionStart").toFloat() - 0.05f) < 1e-5f);
    CHECK(std::fabs(vm.find("sectionBlend").toFloat() - 0.08f) < 1e-5f);
    CHECK(std::fabs(vm.find("sectionSourceStart").toFloat() - 0.1f) < 1e-5f);
    CHECK_EQ(vm.find("sectionCurve").toString(), std::string("linear"));
    CHECK(vm.find("removeSectionResult").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("sectionCountAfterCleanup").toInt(), 0);
    CHECK(vm.find("montageSettingsResult").toTable().get<bool>("ok"));
    CHECK(std::fabs(vm.find("montageRate").toFloat() - 1.5f) < 1e-5f);
    CHECK(vm.find("montageLooping").toBool());
    CHECK(vm.find("montageFootIk").toBool());
    CHECK_EQ(vm.find("montageLayer").toInt(), 2);
    CHECK(std::fabs(vm.find("montageBlendOffset").toFloat() + 0.03f) < 1e-5f);
    CHECK(!vm.find("montageRootRotation").toBool());
    CHECK_EQ(vm.find("splitCountBefore").toInt(), 0);
    CHECK(vm.find("addSplit").toTable().get<bool>("ok"));
    CHECK(std::fabs(vm.find("splitTimeAfterAdd").toFloat() - 0.25f) < 1e-5f);
    CHECK(vm.find("moveSplit").toTable().get<bool>("ok"));
    CHECK(std::fabs(vm.find("splitTimeAfterMove").toFloat() - 0.35f) < 1e-5f);
    CHECK(!vm.find("duplicateSplit").toTable().get<bool>("ok"));
    CHECK(vm.find("removeSplit").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("splitCountAfterRemove").toInt(), 0);
    CHECK(!vm.find("invalidMontageSettings").toTable().get<bool>("ok"));
    CHECK(vm.find("undoMontageSettings").toTable().get<bool>("ok"));
    CHECK(std::fabs(vm.find("restoredMontageRate").toFloat() - 1.0f) < 1e-5f);
    CHECK(!vm.find("invalidResult").toTable().get<bool>("ok"));
}

TEST_CASE("editor.actionTimeline.scriptPersistsDocumentAndReportsDirtyState") {
    const auto root =
        std::filesystem::temp_directory_path() /
        ("eve_action_script_document_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);

    {
        ssq::VM vm(2048, ssq::Libs::ALL);
        eve::ModuleManager::expose(vm);
        const std::string source = std::string(R"(
            timelineAsset <- {
                schema="eve.action.timeline", schemaVersion=1,
                actionId="test:persistent", durationNs=1000000000,
                animationUri="asset://test/attack.glb#Attack", metadata={},
                tracks=[{
                    id="test-track:gameplay", label="Gameplay", kind="gameplay",
                    muted=false, locked=false, notifies=[], states=[]
                }]
            };
            editor <- eve.Editor();
            module <- eve.ActionEditorModule();
            opened <- module.openDocument(")") +
                                   root.generic_string() + R"(", "test.asset.persistent", "Persistent Action",
                "content://Actions/Persistent.action", timelineAsset);
            actionEditor <- opened.value;
            backed <- actionEditor.isDocumentBacked();
            initiallyDirty <- actionEditor.isDirty();
            renameResult <- actionEditor.renameTrack("test-track:gameplay", "Gameplay Saved");
            dirtyAfterEdit <- actionEditor.isDirty();
            saveResult <- actionEditor.saveDocument();
            dirtyAfterSave <- actionEditor.isDirty();
            reopened <- module.openDocument(")" +
                                   root.generic_string() + R"(", "test.asset.reopened", "Reopened Action",
                "content://Actions/Persistent.action", timelineAsset);
            reopenedLabel <- reopened.value.getTrackLabel(0);
        )";
        vm.run(vm.compileSource(source.c_str()));

        CHECK(vm.find("opened").toTable().get<bool>("ok"));
        CHECK(vm.find("backed").toBool());
        CHECK(!vm.find("initiallyDirty").toBool());
        CHECK(vm.find("renameResult").toTable().get<bool>("ok"));
        CHECK(vm.find("dirtyAfterEdit").toBool());
        CHECK(vm.find("saveResult").toTable().get<bool>("ok"));
        CHECK(!vm.find("dirtyAfterSave").toBool());
        CHECK(vm.find("reopened").toTable().get<bool>("ok"));
        CHECK_EQ(vm.find("reopenedLabel").toString(), std::string("Gameplay Saved"));

        const std::string catalogSource = std::string(R"(
            catalogCreated <- module.createAssetCatalog(")") +
                                          root.generic_string() + R"(");
            catalog <- catalogCreated.value;
            catalogRegister <- catalog.registerDocument(
                "test.asset.persistent", "content://Actions/Persistent.action");
            catalogRefresh <- catalog.refresh("persistent");
            catalogCount <- catalog.getAssetCount();
            catalogGuid <- catalog.getAssetGuid(0);
            catalogUri <- catalog.getAssetUri(0);
            catalogTitle <- catalog.getAssetTitle(0);
            catalogGeneration <- catalog.getGeneration();
            catalogEmptySearch <- catalog.refresh("missing-query");
            catalogEmptyCount <- catalog.getAssetCount();
        )";
        vm.run(vm.compileSource(catalogSource.c_str()));
        CHECK(vm.find("catalogCreated").toTable().get<bool>("ok"));
        CHECK(vm.find("catalogRegister").toTable().get<bool>("ok"));
        CHECK(vm.find("catalogRefresh").toTable().get<bool>("ok"));
        CHECK_EQ(vm.find("catalogCount").toInt(), 1);
        CHECK_EQ(vm.find("catalogGuid").toString(), std::string("test.asset.persistent"));
        CHECK_EQ(vm.find("catalogUri").toString(), std::string("content://Actions/Persistent.action"));
        CHECK_EQ(vm.find("catalogTitle").toString(), std::string("Persistent"));
        CHECK(vm.find("catalogGeneration").toInt() > 0);
        CHECK(vm.find("catalogEmptySearch").toTable().get<bool>("ok"));
        CHECK_EQ(vm.find("catalogEmptyCount").toInt(), 0);
    }

    CHECK(std::filesystem::is_regular_file(root / "Content" / "Actions" / "Persistent.action"));
    std::filesystem::remove_all(root, cleanupError);
}

// editor_animation_clip_script.cpp
TEST_CASE("editor.animation.clip_editor_script_composes_workspace_and_transactions") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        workspace <- editor.newWorkspace("test.animation", "Test Animation Editor");
        created <- eve.AnimationEditorModule().create("test.asset.walk");
        clipEditor <- created.value;
        configured <- clipEditor.configureWorkspace(workspace);
        workspacePanelCount <- workspace.getPanelCount();
        panel0 <- workspace.getPanelId(0);
        cap0 <- workspace.getPanelCapability(0);
        ctx1 <- workspace.getPanelContext(1);
        viewport <- clipEditor.setViewport(640.0, 36.0, 120.0);
        seekResult <- clipEditor.seekSeconds(1.0);
        hipsAtMid <- 0.0;
        for (local i = 0; i < clipEditor.getPrimitiveCount(); ++i) {
            if (clipEditor.getPrimitiveKind(i) == "joint") {
                hipsAtMid = clipEditor.getPrimitiveX(i);
                break;
            }
        }
        beforeMask <- clipEditor.getSelectedMaskWeight();
        maskResult <- clipEditor.setMaskWeight(0.2);
        afterMask <- clipEditor.getSelectedMaskWeight();
        undoResult <- clipEditor.undo();
        restoredMask <- clipEditor.getSelectedMaskWeight();
        clipEditor.play();
        advanced <- clipEditor.update(0.25);
        playhead <- clipEditor.getPlayhead();
        tracks <- clipEditor.getTrackCount();
        keys <- clipEditor.getKeyCount();
        primitives <- clipEditor.getPrimitiveCount();
    )"));

    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK(vm.find("configured").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("workspacePanelCount").toInt(), 4);
    CHECK_EQ(vm.find("panel0").toString(), std::string("animation.skeleton"));
    CHECK_EQ(vm.find("cap0").toString(), std::string("animation.clip"));
    CHECK_EQ(vm.find("ctx1").toString(), std::string("preview"));
    CHECK(vm.find("viewport").toTable().get<bool>("ok"));
    CHECK(vm.find("seekResult").toTable().get<bool>("ok"));
    CHECK(vm.find("hipsAtMid").toFloat() > 0.4F);
    CHECK(vm.find("maskResult").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("afterMask").toFloat() - 0.2F), 0.0001F);
    CHECK(vm.find("undoResult").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("restoredMask").toFloat() - vm.find("beforeMask").toFloat()), 0.0001F);
    CHECK(vm.find("advanced").toTable().get<bool>("ok"));
    CHECK(vm.find("playhead").toFloat() > 0.0F);
    CHECK_EQ(vm.find("tracks").toInt(), 2);
    CHECK(vm.find("keys").toInt() >= 4);
    CHECK(vm.find("primitives").toInt() > 0);
}

// editor_api_contracts.cpp
using namespace eve::editor;
using namespace eve::level_editing;

TEST_CASE("editor.api.transactions_expose_checked_results") {
    EditorTransactions transactions;

    auto begun = transactions.beginTransaction("checked transaction");
    REQUIRE(begun.ok());

    auto duplicate = transactions.beginTransaction("nested transaction");
    CHECK(!duplicate.ok());

    auto rolledBack = transactions.rollbackTransaction();
    CHECK(rolledBack.ok());

    auto noActiveTransaction = transactions.rollbackTransaction();
    CHECK(!noActiveTransaction.ok());
}

TEST_CASE("editor.api.session_and_history_preserve_failure_status") {
    EditorSession session;
    auto          nullCommand = session.executeChecked(nullptr);
    CHECK_EQ(static_cast<int>(nullCommand.code()), static_cast<int>(EditorStatus::Rejected));
    REQUIRE(!nullCommand.diagnostics().empty());

    EditorHistory history;
    auto          emptyUndo = history.undoAction();
    CHECK_EQ(static_cast<int>(emptyUndo.code()), static_cast<int>(EditorStatus::NoOp));
    REQUIRE(!emptyUndo.diagnostics().empty());
    CHECK(!history.undo());

    history.push("rename", "payload");
    auto undone = history.undoAction();
    REQUIRE(undone.ok());
    REQUIRE(undone.ok());
    CHECK_EQ(undone.value(), std::string("rename"));
}

TEST_CASE("editor.api.workspace_checked_operations_preserve_diagnostics") {
    EditorWorkspace workspace("checked", "Checked workspace");

    WorkspacePanelDescriptor panel;
    panel.id        = "scene";
    panel.title     = "Scene";
    panel.region    = "center";
    auto registered = workspace.registerPanel(panel);
    REQUIRE(registered.ok());
    REQUIRE(registered.ok());

    auto duplicate = workspace.registerPanel(panel);
    CHECK_EQ(static_cast<int>(duplicate.code()), static_cast<int>(EditorStatus::Conflict));
    REQUIRE(!duplicate.diagnostics().empty());

    auto moved = workspace.movePanel(StableId("scene"), WorkspaceRegion::Left, 10);
    REQUIRE(moved.ok());
    REQUIRE(moved.ok());
    CHECK_EQ(moved.value().region, std::string("left"));

    auto unchanged = workspace.movePanel(StableId("scene"), WorkspaceRegion::Left, 10);
    CHECK_EQ(static_cast<int>(unchanged.code()), static_cast<int>(EditorStatus::NoOp));
    CHECK(unchanged.ok());

    auto missing = workspace.panelAt(9);
    CHECK_EQ(static_cast<int>(missing.code()), static_cast<int>(EditorStatus::NotFound));
    REQUIRE(!missing.diagnostics().empty());

    auto activated = workspace.activatePanel(StableId("scene"));
    CHECK(activated.ok());
    auto removed = workspace.removePanel(StableId("scene"));
    REQUIRE(removed.ok());
    auto removedAgain = workspace.removePanel(StableId("scene"));
    CHECK_EQ(static_cast<int>(removedAgain.code()), static_cast<int>(EditorStatus::NotFound));
}

TEST_CASE("editor.api.coordinator_invalidates_bound_target_when_unregistered") {
    SceneDocumentTarget target("scene.lifecycle");
    Editor              editor;
    EditorSession       session;

    REQUIRE(editor.registerEditingTarget(target).ok());
    CHECK(target.capability<eve::editing::IEditingSnapshotProvider>().has_value());
    REQUIRE(editor.bindEditingTarget(session, TargetId("scene.lifecycle")).ok());
    REQUIRE(session.target() == &target);
    REQUIRE(session.context().target() == &target);
    REQUIRE(session.transactions().beginTransaction("pending target gesture").ok());
    REQUIRE(session.transactions().isActive());

    REQUIRE(editor.unregisterEditingTarget(TargetId("scene.lifecycle")).ok());
    CHECK(session.target() == nullptr);
    CHECK(session.context().target() == nullptr);
    CHECK(session.contextSnapshot().target.empty());
    CHECK(!session.transactions().isActive());
    CHECK(!session.transactions().canUndo());
}

TEST_CASE("editor.api.coordinator_unregisters_captured_command_callbacks_before_destruction") {
    EditorCommandService commands;
    {
        EditorTargetCoordinator                coordinator(commands);
        eve::editing::EditingCommandDescriptor descriptor;
        descriptor.id          = CommandId("test.coordinator.lifecycle");
        descriptor.ownerModule = "test.coordinator";
        auto registered = coordinator.registerPlannedCommand(descriptor, [](IEditableTarget &, const CommandRequest &) {
            return eve::editing::applied<CommandPlan>(CommandPlan{});
        });
        REQUIRE(registered.ok());
        CHECK(commands.find(descriptor.id) != nullptr);
    }
    CHECK(commands.find(CommandId("test.coordinator.lifecycle")) == nullptr);
}

TEST_CASE("editor.api.session_execute_checked_forwards_transaction_diagnostics") {
    TileBuffer       buffer(1, 1);
    TileBufferTarget target("tiles", &buffer);
    EditorSession    session;
    session.bindTarget(&target);

    auto command = std::make_unique<IntFieldEditCommand>("set", &target);
    REQUIRE(command->record(0, 0, 3));
    auto missingTransaction = session.executeChecked(std::move(command));
    CHECK_EQ(static_cast<int>(missingTransaction.code()), static_cast<int>(EditorStatus::Rejected));
    REQUIRE(!missingTransaction.diagnostics().empty());
    CHECK_EQ(eve::editing::diagnosticRule(missingTransaction.diagnostics().front()).value(),
             std::string("editor.transaction"));
    CHECK_EQ(buffer.getGid(0, 0), 0);
}

TEST_CASE("editor.api.session_execute_command_reports_empty_transaction_commit") {
    EditorCommandService service;
    CommandDescriptor    descriptor;
    descriptor.id          = CommandId("test.noop");
    descriptor.ownerModule = "test";
    descriptor.displayName = "Noop";
    REQUIRE(service
                .registerCommand(descriptor,
                                 [](const CommandContext &, const EditorValue &payload) {
                                     return eve::editing::applied<EditorValue>(payload);
                                 })
                .ok());

    EditorSession session;
    HostProfile   profile = HostProfile::developer();
    profile.allowCommand(descriptor.id);
    session.setHostProfile(std::move(profile));
    session.setCommandService(&service);

    auto result = session.executeCommand(descriptor.id, EditorValue(1));
    CHECK(!result.ok());
    CHECK_EQ(static_cast<int>(result.code()), static_cast<int>(EditorStatus::Rejected));
    REQUIRE(!result.diagnostics().empty());
    CHECK(!session.transactions().isActive());
}

// editor_avatar_document_script.cpp
TEST_CASE("editor.avatar.document_editor_script_composes_workspace_and_rejects_referenced_delete") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        workspace <- editor.newWorkspace("test.avatar", "Test Avatar Editor");
        created <- eve.AvatarEditorModule().create("test.asset.face");
        avatarEditor <- created.value;
        configured <- avatarEditor.configureWorkspace(workspace);
        workspacePanelCount <- workspace.getPanelCount();
        panel0 <- workspace.getPanelId(0);
        cap0 <- workspace.getPanelCapability(0);
        ctx4 <- workspace.getPanelContext(4);
        selectParam <- avatarEditor.selectParameter("smile");
        beforeY <- avatarEditor.getPreviewY(1);
        deleted <- avatarEditor.deleteSelectedParameter();
        afterCount <- avatarEditor.getParameterCount();
        afterY <- avatarEditor.getPreviewY(1);
        valueResult <- avatarEditor.setParameterValue(0.8);
        movedY <- avatarEditor.getPreviewY(1);
        expressions <- avatarEditor.getExpressionCount();
        channels <- avatarEditor.getExpressionChannelCount(0);
    )"));

    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK(vm.find("configured").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("workspacePanelCount").toInt(), 5);
    CHECK_EQ(vm.find("panel0").toString(), std::string("avatar.layers"));
    CHECK_EQ(vm.find("cap0").toString(), std::string("avatar.document"));
    CHECK_EQ(vm.find("ctx4").toString(), std::string("expressions"));
    CHECK(vm.find("selectParam").toTable().get<bool>("ok"));
    CHECK(!vm.find("deleted").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("afterCount").toInt(), 1);
    CHECK_EQ(vm.find("afterY").toFloat(), vm.find("beforeY").toFloat());
    CHECK(vm.find("valueResult").toTable().get<bool>("ok"));
    CHECK(vm.find("movedY").toFloat() > vm.find("beforeY").toFloat());
    CHECK_EQ(vm.find("expressions").toInt(), 1);
    CHECK(vm.find("channels").toInt() >= 1);
}

// editor_biome_rules_script.cpp
TEST_CASE("editor.biome.rules_editor_script_composes_workspace_and_exclusions") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        workspace <- editor.newWorkspace("test.biome", "Test Biome Editor");
        created <- eve.BiomeEditorModule().create("test.asset.forest");
        biomeEditor <- created.value;
        configured <- biomeEditor.configureWorkspace(workspace);
        workspacePanelCount <- workspace.getPanelCount();
        panel0 <- workspace.getPanelId(0);
        cap0 <- workspace.getPanelCapability(0);
        ctx3 <- workspace.getPanelContext(3);
        beforePoints <- biomeEditor.getPointCount();
        excluded <- biomeEditor.addExclusion("asset://preview/clearing.spatial");
        afterPoints <- biomeEditor.getPointCount();
        exclusions <- biomeEditor.getExclusionCount();
        undoResult <- biomeEditor.undo();
        restored <- biomeEditor.getPointCount();
        densityReject <- biomeEditor.setLayerDensity(2.0);
    )"));

    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK(vm.find("configured").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("workspacePanelCount").toInt(), 4);
    CHECK_EQ(vm.find("panel0").toString(), std::string("biome.layers"));
    CHECK_EQ(vm.find("cap0").toString(), std::string("biome.rules"));
    CHECK_EQ(vm.find("ctx3").toString(), std::string("assets"));
    CHECK(vm.find("beforePoints").toInt() > 0);
    CHECK(vm.find("excluded").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("exclusions").toInt(), 1);
    CHECK(vm.find("afterPoints").toInt() < vm.find("beforePoints").toInt());
    CHECK(vm.find("undoResult").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("restored").toInt(), vm.find("beforePoints").toInt());
    CHECK(!vm.find("densityReject").toTable().get<bool>("ok"));
}
