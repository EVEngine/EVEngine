#include "action/ActionBlockRuntime.h"
#include "action/ActionNotifyRegistry.h"
#include "action/ActionPrefabInstances.h"
#include "action/ActionPreview.h"
#include "common/Capability.h"
#include "filesystem/Filesystem.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem3D.h"
#include "model3d/Model3D.h"
#include "scene/loader/SceneLoader.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <filesystem>
#include <string_view>

namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return *parsed;
}

std::size_t renderableCount(bool visibleOnly) {
    if (!ecs::current()->getManager<eve::graphics::Renderable3D>()) return 0;
    std::size_t count = 0;
    auto view = ecs::View<eve::graphics::Renderable3D,
                          eve::graphics::Renderable3D::MeshRenderer>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [renderer] = *it;
        if (!visibleOnly || renderer->visible) ++count;
    }
    return count;
}

eve::action::ActionTimelineEvent boundary(eve::action::ActionTimelineEventKind kind,
                                           std::string_view item,
                                           eve::Value::Object payload) {
    return {kind, id("prefab-track:gameplay"), id(item), id("gameplay:prefab-spawn"),
            eve::Duration::zero(), std::move(payload)};
}

eve::action::ActionActiveBlock active(std::string_view item, eve::Value::Object payload) {
    return {id("prefab-track:gameplay"), id(item), id("gameplay:prefab-spawn"),
            eve::Duration::zero(), eve::Duration::fromNanoseconds(1000000000),
            std::move(payload)};
}

}  // namespace

TEST_CASE("actionPrefabRuntime.spawnsRealRenderablePoolsAndAppliesThreeLifecycles") {
    auto* filesystem = eve::filesystem::Filesystem::create();
    const auto testDirectory = std::filesystem::path(__FILE__).parent_path();
    filesystem->allowMountingForPath(testDirectory.string());
    REQUIRE(filesystem->mount(testDirectory.string(), "", false));
    const std::string prefabUri = "fixtures/model3d/uv-origin.gltf";

    auto* graphics = eve::graphics::Graphics::create();
    graphics->initHeadless(64, 64);
    REQUIRE(eve::model3d::Model3D::create() != nullptr);
    REQUIRE(eve::sceneloader::SceneLoader::create() != nullptr);

    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    eve::action::ActionBlockRuntime runtime(registry.value());

    eve::Value::Object recycledPayload{{"uri", prefabUri},
                                       {"lifecycle", "recycle_on_block_exit"},
                                       {"positionOffset", eve::Value::Array{2.0, 3.0, 4.0}}};
    eve::action::ActionAdvance enter;
    enter.id = eve::action::ActionExecutionId{101};
    enter.timelineEvents.push_back(boundary(eve::action::ActionTimelineEventKind::StateEnter,
                                            "prefab-item:recycled", recycledPayload));
    enter.activeBlocks.push_back(active("prefab-item:recycled", recycledPayload));
    eve::action::ActionNotifyContext context;
    context.executionId = enter.id;
    REQUIRE(runtime.apply(enter, context).ok());
    CHECK_EQ(renderableCount(true), 1u);
    auto transformed = ecs::View<eve::graphics::Renderable3D,
                                 eve::graphics::Renderable3D::Transform3D,
                                 eve::graphics::Renderable3D::MeshRenderer>();
    for (auto it = transformed.begin(); it != transformed.end(); ++it) {
        auto [transform, renderer] = *it;
        if (!renderer->visible) continue;
        CHECK_EQ(transform->x, 2.f);
        CHECK_EQ(transform->y, 3.f);
        CHECK_EQ(transform->z, 4.f);
    }
    const auto firstTotal = renderableCount(false);
    REQUIRE_EQ(firstTotal, 1u);

    eve::action::ActionAdvance leave;
    leave.id = enter.id;
    leave.timelineEvents.push_back(boundary(eve::action::ActionTimelineEventKind::StateExit,
                                            "prefab-item:recycled", recycledPayload));
    REQUIRE(runtime.apply(leave, context).ok());
    CHECK_EQ(renderableCount(true), 0u);

    enter.id = eve::action::ActionExecutionId{102};
    context.executionId = enter.id;
    REQUIRE(runtime.apply(enter, context).ok());
    CHECK_EQ(renderableCount(true), 1u);
    CHECK_EQ(renderableCount(false), firstTotal);
    leave.id = enter.id;
    context.executionId = leave.id;
    REQUIRE(runtime.apply(leave, context).ok());

    eve::Value::Object timedPayload{{"uri", prefabUri},
                                    {"lifecycle", "custom_duration"},
                                    {"customDurationSeconds", 0.1}};
    eve::action::ActionAdvance timedEnter;
    timedEnter.id = eve::action::ActionExecutionId{103};
    timedEnter.timelineEvents.push_back(boundary(eve::action::ActionTimelineEventKind::StateEnter,
                                                 "prefab-item:timed", timedPayload));
    timedEnter.activeBlocks.push_back(active("prefab-item:timed", timedPayload));
    context.executionId = timedEnter.id;
    REQUIRE(runtime.apply(timedEnter, context).ok());
    CHECK_EQ(renderableCount(true), 1u);
    eve::action::ActionAdvance timedExit;
    timedExit.id = timedEnter.id;
    timedExit.totalElapsed = eve::Duration::fromNanoseconds(50000000);
    timedExit.timelineEvents.push_back(boundary(eve::action::ActionTimelineEventKind::StateExit,
                                                "prefab-item:timed", timedPayload));
    REQUIRE(runtime.apply(timedExit, context).ok());
    CHECK_EQ(renderableCount(true), 1u);
    eve::action::ActionAdvance timedMaintenance;
    timedMaintenance.id = timedEnter.id;
    timedMaintenance.totalElapsed = eve::Duration::fromNanoseconds(200000000);
    REQUIRE(runtime.apply(timedMaintenance, context).ok());
    CHECK_EQ(renderableCount(true), 0u);

    eve::Value::Object independentPayload{{"uri", prefabUri},
                                          {"lifecycle", "independent"}};
    eve::action::ActionAdvance independentEnter;
    independentEnter.id = eve::action::ActionExecutionId{104};
    independentEnter.timelineEvents.push_back(boundary(eve::action::ActionTimelineEventKind::StateEnter,
                                                       "prefab-item:independent", independentPayload));
    independentEnter.activeBlocks.push_back(active("prefab-item:independent", independentPayload));
    context.executionId = independentEnter.id;
    REQUIRE(runtime.apply(independentEnter, context).ok());
    eve::action::ActionAdvance independentExit;
    independentExit.id = independentEnter.id;
    independentExit.timelineEvents.push_back(boundary(eve::action::ActionTimelineEventKind::StateExit,
                                                      "prefab-item:independent", independentPayload));
    REQUIRE(runtime.apply(independentExit, context).ok());
    CHECK_EQ(renderableCount(true), 1u);
    auto* instances = eve::cap::query<eve::action::IActionPrefabInstances>();
    REQUIRE(instances != nullptr);
    const auto independent = instances->independentInstances();
    REQUIRE_EQ(independent.size(), 1u);
    CHECK_EQ(independent.front().uri, prefabUri);
    REQUIRE(instances->recycleIndependent(independent.front().handle).ok());
    CHECK_EQ(renderableCount(true), 0u);
    auto stale = instances->recycleIndependent(independent.front().handle);
    CHECK(!stale.ok());
    CHECK_EQ(stale.status().code(), eve::StatusCode::NotFound);

    REQUIRE_EQ(eve::cap::listenerCount<eve::action::IActionPreviewSinkProvider>(), 1u);
    auto* previewProvider = eve::cap::listenerAt<eve::action::IActionPreviewSinkProvider>(0);
    REQUIRE(previewProvider != nullptr);
    auto preview = previewProvider->createActionPreviewSink();
    REQUIRE(preview.ok());
    eve::action::ActionPreviewFrame frame;
    eve::Value::Object previewPayload{{"uri", prefabUri},
                                      {"lifecycle", "recycle_on_block_exit"},
                                      {"positionOffset", eve::Value::Array{6.0, 7.0, 8.0}}};
    frame.activeBlocks.push_back(active("prefab-item:preview", previewPayload));
    REQUIRE(preview.value()->prepare(frame).ok());
    CHECK_EQ(renderableCount(true), 0u);
    preview.value()->present(frame);
    CHECK_EQ(renderableCount(true), 1u);
    for (auto it = transformed.begin(); it != transformed.end(); ++it) {
        auto [transform, renderer] = *it;
        if (!renderer->visible) continue;
        CHECK_EQ(transform->x, 6.f);
        CHECK_EQ(transform->y, 7.f);
        CHECK_EQ(transform->z, 8.f);
    }

    frame.activeBlocks.front().payload["positionOffset"] = eve::Value::Array{9.0, 10.0, 11.0};
    REQUIRE(preview.value()->prepare(frame).ok());
    CHECK_EQ(renderableCount(true), 1u);
    preview.value()->present(frame);
    CHECK_EQ(renderableCount(true), 1u);
    auto invalidFrame = frame;
    invalidFrame.activeBlocks.front().payload.erase("uri");
    CHECK(!preview.value()->prepare(invalidFrame).ok());
    CHECK_EQ(renderableCount(true), 1u);

    auto timedPreview = frame;
    timedPreview.activeBlocks.front().payload["lifecycle"] = "custom_duration";
    timedPreview.activeBlocks.front().payload["customDurationSeconds"] = 0.1;
    timedPreview.activeBlocks.front().localTime = eve::Duration::fromNanoseconds(200000000);
    REQUIRE(preview.value()->prepare(timedPreview).ok());
    preview.value()->present(timedPreview);
    CHECK_EQ(renderableCount(true), 0u);
    eve::action::ActionPreviewFrame emptyFrame;
    REQUIRE(preview.value()->prepare(emptyFrame).ok());
    preview.value()->present(emptyFrame);
    CHECK_EQ(renderableCount(true), 0u);
}
