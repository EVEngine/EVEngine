#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"

#include <string>

using namespace eve::ui;

TEST_CASE("ui.dragDrop.retainedPayloadSurvivesFlattenAndReconcile") {
    auto host = UIHost::resolve(UIHost::createHost("drag-drop-retained"));
    REQUIRE(host.has_value());

    WidgetDesc source = button("Asset", "asset")
                            .withDragSource("asset", "textures/stone.png");
    WidgetDesc target = button("Viewport", "viewport").withDropTarget("asset");
    host->get().setTree(window("DragDrop", {std::move(source), std::move(target)}, "root"));

    auto sourceNode = host->get().findById("asset");
    auto targetNode = host->get().findById("viewport");
    REQUIRE(sourceNode.has_value());
    REQUIRE(targetNode.has_value());
    CHECK(sourceNode->get().dragSource);
    CHECK_EQ(sourceNode->get().dragPayloadType, std::string("asset"));
    CHECK_EQ(sourceNode->get().dragPayloadText, std::string("textures/stone.png"));
    CHECK(targetNode->get().dropTarget);
    CHECK_EQ(targetNode->get().acceptedDropType, std::string("asset"));

    WidgetDesc updated = button("Asset", "asset").withDragSource("entity", "unit-42");
    CHECK(host->get().setTreeReconcile(
        window("DragDrop", {std::move(updated), button("Viewport", "viewport")}, "root")));
    sourceNode = host->get().findById("asset");
    targetNode = host->get().findById("viewport");
    REQUIRE(sourceNode.has_value());
    REQUIRE(targetNode.has_value());
    CHECK_EQ(sourceNode->get().dragPayloadType, std::string("entity"));
    CHECK_EQ(sourceNode->get().dragPayloadText, std::string("unit-42"));
    CHECK(!targetNode->get().dropTarget);
    CHECK(targetNode->get().acceptedDropType.empty());
}

TEST_CASE("ui.dragDrop.completedEventOwnsPayloadAfterSourceLifetime") {
    UISystem::dropQueue().clear();
    UISystem::dropQueue().push_back({DragDropOrigin::Internal, "assets", "stone",
                                     "scene", "viewport", "asset",
                                     "textures/stone.png"});

    auto drop = UISystem::consumeDrop();
    REQUIRE(drop.has_value());
    CHECK_EQ(drop->sourceHostName, std::string("assets"));
    CHECK_EQ(drop->sourceNodeId, std::string("stone"));
    CHECK_EQ(drop->targetHostName, std::string("scene"));
    CHECK_EQ(drop->targetNodeId, std::string("viewport"));
    CHECK_EQ(drop->payloadType, std::string("asset"));
    CHECK_EQ(drop->payloadText, std::string("textures/stone.png"));
    CHECK(!UISystem::consumeDrop().has_value());
}

TEST_CASE("ui.dragDrop.platformSupportIsExplicit") {
#if (defined(_WIN32) || defined(__APPLE__) || defined(__linux__)) && \
    !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    CHECK_EQ(static_cast<int>(UISystem::dragDropSupport()),
             static_cast<int>(DragDropSupport::Supported));
#else
    CHECK_EQ(static_cast<int>(UISystem::dragDropSupport()),
             static_cast<int>(DragDropSupport::UnsupportedPlatform));
#endif
}
