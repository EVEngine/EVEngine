#include "building_editor/EditorBuildingTarget.h"

#include "building/BuildingDef.h"
#include "building/PlacementWorld.h"
#include "editing/EditingAuthority.h"
#include "editor/EditorSession.h"

#include "zeroerr/unittest.h"

#include <cmath>
#include <span>

namespace {

using eve::editor::BuildingEdgeCurveTool;
using eve::editor::BuildingEdgeCurveToolSelection;
using eve::editor::BuildingPlacementTarget;
using eve::editor::BuildingViewportRay;
using eve::editor::EditorPointerEvent;
using eve::editor::IBuildingViewportAdapter;
using eve::editor::IEditorOverlay;
using eve::editor::OverlayPoint;
using eve::editor::OverlayStyle;

void registerToolFence() {
    eve::building::BuildingDefinition fence;
    fence.id = "editor-tool-fence";
    fence.displayName = "Tool Fence";
    fence.placementKind = "edge";
    fence.channel = "boundary";
    eve::building::BuildingRegistry::registerBuilding(fence);
}

class TestBuildingViewport final : public IBuildingViewportAdapter {
public:
    eve::editing::Result<BuildingViewportRay> pointerRay(
        const EditorPointerEvent& event) const override {
        BuildingViewportRay ray;
        ray.origin = {event.x, 30.0, event.y};
        ray.direction = {0.0, -1.0, 0.0};
        return eve::editing::applied<BuildingViewportRay>(ray);
    }

    eve::editing::Result<OverlayPoint> projectWorld(
        const std::array<double, 3>& world) const override {
        return eve::editing::applied<OverlayPoint>(
            {static_cast<float>(world[0]), static_cast<float>(world[2]),
             static_cast<float>(world[1])});
    }
};

class CountingAuthority final : public eve::editing::IEditAuthority {
public:
    explicit CountingAuthority(eve::editing::IDomainOperationTarget* target)
        : local_(target) {}

    eve::editing::Result<eve::editing::AuthorityPlan> preflight(
        const eve::editing::TransactionSpec& transaction,
        std::span<const eve::editing::DomainOperation> operations) override {
        ++preflights;
        return local_.preflight(transaction, operations);
    }

    eve::editing::Result<eve::editing::TransactionReceipt> commit(
        const eve::editing::AuthorityPlan& plan) override {
        ++commits;
        return local_.commit(plan);
    }

    eve::editing::Result<eve::editing::TransactionReceipt> compensate(
        const eve::editing::TransactionReceipt& receipt) override {
        return local_.compensate(receipt);
    }

    int preflights = 0;
    int commits = 0;

private:
    eve::editing::LocalWorldAuthority local_;
};

class CountingOverlay final : public IEditorOverlay {
public:
    void line(const OverlayPoint&, const OverlayPoint&, const OverlayStyle&) override {
        ++lines;
    }
    void circle(const OverlayPoint&, float, const OverlayStyle&) override { ++circles; }
    void rectangle(const OverlayPoint&, const OverlayPoint&, const OverlayStyle&) override {}
    void text(const OverlayPoint&, const std::string&, const OverlayStyle&) override {}
    int lines = 0;
    int circles = 0;
};

BuildingEdgeCurveToolSelection placedCurve(eve::building::PlacementWorld& world,
                                           BuildingPlacementTarget& target) {
    BuildingEdgeCurveToolSelection selection;
    selection.buildingId = "editor-tool-fence";
    selection.controlPoints = {{1.0, 1.0}, {1.0, 5.0}, {6.0, 5.0}, {6.0, 1.0}};
    selection.subdivisions = 20;
    auto operation = target.makeEdgeCubicBezier(selection.buildingId,
                                                 selection.controlPoints,
                                                 selection.subdivisions);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    selection.memberInstanceId = world.getBuildingInstanceAt(0);
    return selection;
}

EditorPointerEvent pointer(EditorPointerEvent::Phase phase, float x, float y,
                           int pointerId = 4) {
    EditorPointerEvent event;
    event.phase = phase;
    event.pointerId = pointerId;
    event.button = 0;
    event.x = x;
    event.y = y;
    return event;
}

}  // namespace

TEST_CASE("editor.building.curve_tool_routes_drag_as_one_authority_transaction") {
    registerToolFence();
    eve::building::PlacementWorld world(12, 12, 10.f);
    world.setGridPlane("xz");
    BuildingPlacementTarget target("building-curve-tool-world", &world);
    BuildingEdgeCurveToolSelection selection = placedCurve(world, target);
    const auto groupBefore = world.edgeCurveGroupForInstance(selection.memberInstanceId);
    REQUIRE(groupBefore.ok());
    const auto revisionBefore = target.revision();

    TestBuildingViewport viewport;
    CountingAuthority authority(&target);
    BuildingEdgeCurveTool tool(&viewport, &authority);
    REQUIRE(tool.setSelection(selection).ok());
    eve::editor::EditorSession session;
    session.bindTarget(target);
    REQUIRE(session.addTool(&tool));
    REQUIRE(session.activateTool(tool.descriptor().id));

    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Down, 10.f, 10.f))
              .capturePointer);
    CHECK(session.hasPointerCapture());
    CHECK(tool.isDragging());
    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Move, 20.f, 10.f))
              .handled);
    CHECK_EQ(authority.preflights, 0);
    CHECK_EQ(authority.commits, 0);
    CHECK_EQ(target.revision(), revisionBefore);

    CountingOverlay overlay;
    session.drawOverlay(overlay);
    CHECK_EQ(overlay.circles, 4);
    CHECK(overlay.lines > 4);

    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Up, 20.f, 10.f))
              .releasePointer);
    CHECK(!session.hasPointerCapture());
    CHECK(!tool.isDragging());
    CHECK_EQ(authority.preflights, 1);
    CHECK_EQ(authority.commits, 1);
    CHECK_EQ(target.revision(), revisionBefore + 1);
    REQUIRE(tool.lastReceipt().has_value());
    const eve::editing::TransactionReceipt firstReceipt = *tool.lastReceipt();
    auto groupAfter = world.edgeCurveGroup(groupBefore.value().id);
    REQUIRE(groupAfter.ok());
    CHECK(std::abs(groupAfter.value().controlPoints[0].x - 2.f) < 0.0001f);
    CHECK_EQ(groupAfter.value().controlPoints[0].y, groupBefore.value().controlPoints[0].y);

    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Down, 20.f, 10.f))
              .capturePointer);
    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Move, 30.f, 10.f))
              .handled);
    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Up, 30.f, 10.f))
              .releasePointer);
    CHECK_EQ(authority.preflights, 2);
    CHECK_EQ(authority.commits, 2);
    REQUIRE(tool.lastReceipt().has_value());
    const eve::editing::TransactionReceipt secondReceipt = *tool.lastReceipt();
    auto groupAfterSecondDrag = world.edgeCurveGroup(groupBefore.value().id);
    REQUIRE(groupAfterSecondDrag.ok());
    CHECK(std::abs(groupAfterSecondDrag.value().controlPoints[0].x - 3.f) < 0.0001f);

    REQUIRE(authority.compensate(secondReceipt).ok());
    REQUIRE(authority.compensate(firstReceipt).ok());
    auto undone = world.edgeCurveGroup(groupBefore.value().id);
    REQUIRE(undone.ok());
    CHECK_EQ(undone.value(), groupBefore.value());
    eve::building::BuildingRegistry::clear();
}

TEST_CASE("editor.building.curve_tool_cancel_and_miss_do_not_mutate") {
    registerToolFence();
    eve::building::PlacementWorld world(12, 12, 10.f);
    world.setGridPlane("xz");
    BuildingPlacementTarget target("building-curve-tool-cancel", &world);
    BuildingEdgeCurveToolSelection selection = placedCurve(world, target);
    const auto before = world.edgeCurveGroupForInstance(selection.memberInstanceId);
    REQUIRE(before.ok());
    const auto revisionBefore = target.revision();

    TestBuildingViewport viewport;
    CountingAuthority authority(&target);
    BuildingEdgeCurveTool tool(&viewport, &authority);
    REQUIRE(tool.setSelection(selection).ok());
    eve::editor::EditorSession session;
    session.bindTarget(target);
    REQUIRE(session.addTool(&tool));
    REQUIRE(session.activateTool(tool.descriptor().id));

    CHECK(!session.dispatchPointer(pointer(EditorPointerEvent::Phase::Down, 200.f, 200.f))
               .handled);
    CHECK(!session.hasPointerCapture());
    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Down, 10.f, 10.f))
              .capturePointer);
    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Move, 30.f, 10.f))
              .handled);
    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Cancel, 30.f, 10.f))
              .releasePointer);
    CHECK_EQ(authority.preflights, 0);
    CHECK_EQ(authority.commits, 0);
    CHECK_EQ(target.revision(), revisionBefore);
    auto after = world.edgeCurveGroup(before.value().id);
    REQUIRE(after.ok());
    CHECK_EQ(after.value(), before.value());
    eve::building::BuildingRegistry::clear();
}
