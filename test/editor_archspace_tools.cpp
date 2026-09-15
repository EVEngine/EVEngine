#include "archspace/editor/EditorArchSpaceTools.h"

#include "archspace/ArchSpaceCatalog.h"
#include "editing/EditingAuthority.h"
#include "editor/EditorSession.h"

#include "zeroerr/unittest.h"

#include <algorithm>
#include <array>
#include <span>
#include <string>

namespace {

using eve::archspace::NodeKind;
using eve::archspace::OpeningKind;
using eve::archspace::Vec2;
using eve::archspace::Vec3;
using eve::archspace_editing::ArchSpaceDocumentTarget;
using eve::archspace_editing::EditorResult;
using eve::editing::DomainOperation;
using eve::editor::ArchSpaceItemPlaceTool;
using eve::editor::ArchSpaceOpeningPlaceTool;
using eve::editor::ArchSpaceViewportRay;
using eve::editor::ArchSpaceWallDrawTool;
using eve::editor::EditorPointerEvent;
using eve::editor::IArchSpaceViewportAdapter;
using eve::editor::IEditorOverlay;
using eve::editor::OverlayPoint;
using eve::editor::OverlayStyle;

void apply(ArchSpaceDocumentTarget& target, EditorResult<DomainOperation> operation) {
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
}

class TestArchSpaceViewport final : public IArchSpaceViewportAdapter {
public:
    eve::editing::Result<ArchSpaceViewportRay> pointerRay(const EditorPointerEvent& event) const override {
        ArchSpaceViewportRay ray;
        ray.origin    = {event.x, 30.0, event.y};
        ray.direction = {0.0, -1.0, 0.0};
        return eve::editing::applied<ArchSpaceViewportRay>(ray);
    }

    eve::editing::Result<OverlayPoint> projectWorld(const std::array<double, 3>& world) const override {
        return eve::editing::applied<OverlayPoint>(
            {static_cast<float>(world[0]), static_cast<float>(world[2]), static_cast<float>(world[1])});
    }
};

class CountingAuthority final : public eve::editing::IEditAuthority {
public:
    explicit CountingAuthority(eve::editing::IDomainOperationTarget* target) : local_(target) {}

    eve::editing::Result<eve::editing::AuthorityPlan> preflight(
        const eve::editing::TransactionSpec&           transaction,
        std::span<const eve::editing::DomainOperation> operations) override {
        ++preflights;
        return local_.preflight(transaction, operations);
    }

    eve::editing::Result<eve::editing::TransactionReceipt> commit(const eve::editing::AuthorityPlan& plan) override {
        ++commits;
        return local_.commit(plan);
    }

    eve::editing::Result<eve::editing::TransactionReceipt> compensate(
        const eve::editing::TransactionReceipt& receipt) override {
        return local_.compensate(receipt);
    }

    int preflights = 0;
    int commits    = 0;

private:
    eve::editing::LocalWorldAuthority local_;
};

class CountingOverlay final : public IEditorOverlay {
public:
    void line(const OverlayPoint&, const OverlayPoint&, const OverlayStyle&) override { ++lines; }
    void circle(const OverlayPoint&, float, const OverlayStyle&) override { ++circles; }
    void rectangle(const OverlayPoint&, const OverlayPoint&, const OverlayStyle&) override {}
    void text(const OverlayPoint&, const std::string&, const OverlayStyle&) override { ++texts; }
    int  lines   = 0;
    int  circles = 0;
    int  texts   = 0;
};

EditorPointerEvent pointer(EditorPointerEvent::Phase phase, float x, float y, int pointerId = 7) {
    EditorPointerEvent event;
    event.phase     = phase;
    event.pointerId = pointerId;
    event.button    = 0;
    event.x         = x;
    event.y         = y;
    return event;
}

bool hasPrimitive(const eve::archspace::MeshBake& bake, const std::string& needle) {
    return std::any_of(bake.primitiveIds.begin(), bake.primitiveIds.end(),
                       [&](const std::string& id) { return id.find(needle) != std::string::npos; });
}

}  // namespace

TEST_CASE("editor.archspace.wall_draw_tool_commits_one_authority_transaction") {
    ArchSpaceDocumentTarget target("archspace-wall-tool");
    apply(target, target.makeBootstrap("site", "building", "level0"));
    const auto revisionBefore = target.revision();

    TestArchSpaceViewport viewport;
    CountingAuthority     authority(&target);
    ArchSpaceWallDrawTool tool(&viewport, &authority);
    tool.setLevelId("level0");
    tool.setIdPrefix("drawn.wall");

    eve::editor::EditorSession session;
    session.bindTarget(target);
    REQUIRE(session.addTool(&tool));
    REQUIRE(session.activateTool(tool.descriptor().id));

    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Down, 0.f, 0.f)).capturePointer);
    CHECK(session.hasPointerCapture());
    CHECK(tool.isDrawing());
    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Move, 4.f, 0.f)).handled);
    CHECK_EQ(authority.preflights, 0);
    CHECK_EQ(authority.commits, 0);
    CHECK_EQ(target.revision(), revisionBefore);

    CountingOverlay overlay;
    session.drawOverlay(overlay);
    CHECK_EQ(overlay.lines, 1);
    CHECK_EQ(overlay.circles, 2);

    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Up, 4.f, 0.f)).releasePointer);
    CHECK(!session.hasPointerCapture());
    CHECK(!tool.isDrawing());
    CHECK_EQ(authority.preflights, 1);
    CHECK_EQ(authority.commits, 1);
    CHECK_EQ(target.revision(), revisionBefore + 1);
    REQUIRE(tool.lastReceipt().has_value());
    REQUIRE(target.document().find(tool.lastWallId()) != nullptr);
    CHECK_EQ(target.document().find(tool.lastWallId())->kind, NodeKind::Wall);
}

TEST_CASE("editor.archspace.opening_and_item_tools_commit_through_authority") {
    ArchSpaceDocumentTarget target("archspace-place-tools");
    apply(target, target.makeBootstrap("site", "building", "level0"));
    apply(target, target.makeCreateWall("level0", "wall.a", "Wall A", Vec2{0, 0}, Vec2{6, 0}, 3.0, 0.2));

    TestArchSpaceViewport viewport;
    CountingAuthority     authority(&target);

    ArchSpaceOpeningPlaceTool openingTool(&viewport, &authority);
    openingTool.setOpeningKind(OpeningKind::Door);
    openingTool.setWidth(1.0);
    openingTool.setHeight(2.1);
    openingTool.setIdPrefix("drawn.door");

    eve::editor::EditorSession session;
    session.bindTarget(target);
    REQUIRE(session.addTool(&openingTool));
    REQUIRE(session.activateTool(openingTool.descriptor().id));

    const auto revisionBeforeOpening = target.revision();
    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Down, 3.f, 0.f)).capturePointer);
    CHECK_EQ(authority.commits, 0);
    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Up, 3.f, 0.f)).releasePointer);
    CHECK_EQ(authority.preflights, 1);
    CHECK_EQ(authority.commits, 1);
    CHECK_EQ(target.revision(), revisionBeforeOpening + 1);
    REQUIRE(target.document().find(openingTool.lastOpeningId()) != nullptr);
    CHECK_EQ(target.document().find(openingTool.lastOpeningId())->kind, NodeKind::Opening);

    ArchSpaceItemPlaceTool itemTool(&viewport, &authority);
    itemTool.setLevelId("level0");
    itemTool.setCatalogId("furniture.desk");
    itemTool.setYawDegrees(90.0);
    itemTool.setIdPrefix("drawn.desk");
    REQUIRE(session.addTool(&itemTool));
    REQUIRE(session.activateTool(itemTool.descriptor().id));

    const auto revisionBeforeItem = target.revision();
    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Down, 2.f, 2.f)).capturePointer);
    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Up, 2.f, 2.f)).releasePointer);
    CHECK_EQ(authority.commits, 2);
    CHECK_EQ(target.revision(), revisionBeforeItem + 1);
    REQUIRE(target.document().find(itemTool.lastItemId()) != nullptr);
    CHECK_EQ(target.document().find(itemTool.lastItemId())->catalogId, "furniture.desk");

    const eve::archspace::MeshBake bake = target.bakeMesh();
    CHECK(hasPrimitive(bake, ".reveal."));
    CHECK(hasPrimitive(bake, itemTool.lastItemId()));
    CHECK(eve::archspace::lookupCatalog("furniture.desk").has_value());
    CHECK(eve::archspace::listCatalogIds().size() >= 6);
}

TEST_CASE("editor.archspace.wall_draw_cancel_does_not_mutate") {
    ArchSpaceDocumentTarget target("archspace-wall-cancel");
    apply(target, target.makeBootstrap("site", "building", "level0"));
    const auto revisionBefore = target.revision();
    const auto nodesBefore    = target.document().nodeCount();

    TestArchSpaceViewport viewport;
    CountingAuthority     authority(&target);
    ArchSpaceWallDrawTool tool(&viewport, &authority);
    tool.setLevelId("level0");

    eve::editor::EditorSession session;
    session.bindTarget(target);
    REQUIRE(session.addTool(&tool));
    REQUIRE(session.activateTool(tool.descriptor().id));

    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Down, 1.f, 1.f)).capturePointer);
    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Move, 3.f, 1.f)).handled);
    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Cancel, 3.f, 1.f)).releasePointer);
    CHECK_EQ(authority.preflights, 0);
    CHECK_EQ(authority.commits, 0);
    CHECK_EQ(target.revision(), revisionBefore);
    CHECK_EQ(target.document().nodeCount(), nodesBefore);
    CHECK(!tool.isDrawing());
}
