#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/EditCommand.h"
#include "editor/EditorCommandService.h"
#include "editor/EditorHostProfile.h"
#include "editor/EditorIds.h"
#include "editor/EditorSession.h"
#include "editor/EditorValue.h"
#include "editor/FieldTargets.h"
#include "editor/TileBuffer.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

using namespace eve::editor;

static_assert(!std::is_convertible_v<CommandId, ToolId>);
static_assert(!std::is_convertible_v<TargetId, CommandId>);

TEST_CASE("editor.v2.strong_ids_and_values") {
    const CommandId command("scene.object.create");
    const CommandId same("scene.object.create");
    const CommandId other("scene.object.delete");
    CHECK(command == same);
    CHECK(command != other);
    CHECK_EQ(command.value(), std::string("scene.object.create"));

    EditorValue::Object root;
    root["name"]     = "Station";
    root["position"] = EditorValue::Array{1.0, 2.0, 3.0};
    root["visible"]  = true;
    const EditorValue value(std::move(root));
    CHECK_EQ(static_cast<int>(value.type()), static_cast<int>(EditorValue::Type::Object));
    CHECK(value.withinLimits(4, 16, 128));
    CHECK(!value.withinLimits(1, 16, 128));
    CHECK(!value.withinLimits(4, 2, 128));
    CHECK(!value.withinLimits(4, 16, 4));
}

TEST_CASE("editor.v2.idsUseCommonUuidAdapterWithLegacyParse") {
    const auto legacy = CommandId::parse("scene.object.create");
    REQUIRE(legacy.has_value());
    CHECK(!legacy->isCanonicalUuid());
    CHECK_EQ(legacy->value(), std::string("scene.object.create"));

    const auto projected = canonicalUuid(*legacy);
    const auto canonical = CommandId::fromUuid(projected);
    CHECK(canonical.isCanonicalUuid());
    CHECK_EQ(canonical.uuid(), projected);
    CHECK_EQ(canonical.canonicalFormat(), projected.format());

    const auto parsed = CommandId::parse("01020304-0506-0708-090a-0b0c0d0e0f10");
    REQUIRE(parsed.has_value());
    CHECK(parsed->isCanonicalUuid());
    CHECK_EQ(parsed->value(), std::string("01020304-0506-0708-090a-0b0c0d0e0f10"));
}

TEST_CASE("editor.v2.host_profile_is_an_execution_boundary") {
    const CommandId place("scene.asset.place");
    const CommandId importAsset("asset.import");
    HostProfile     runtime = HostProfile::runtimeBuilder();
    CHECK(!runtime.allowsCommand(place));
    runtime.allowCommand(place);
    CHECK(runtime.allowsCommand(place));
    CHECK(!runtime.allowsCommand(importAsset));
    CHECK(runtime.hasFeatures(HostFeature::RuntimeWorld));
    CHECK(!runtime.hasFeatures(HostFeature::SourceAssets));

    HostProfile developer = HostProfile::developer();
    CHECK(developer.allowsCommand(importAsset));
    CHECK(developer.hasFeatures(HostFeature::SourceAssets | HostFeature::BuildCook));
}

TEST_CASE("editor.v2.command_registry_filters_and_executes") {
    EditorCommandService service;
    CommandDescriptor    descriptor;
    descriptor.id               = CommandId("scene.rename");
    descriptor.ownerModule      = "scene";
    descriptor.displayName      = "Rename";
    descriptor.requiredFeatures = HostFeature::RuntimeWorld;

    int  calls      = 0;
    auto registered = service.registerCommand(descriptor, [&](const CommandContext&, const EditorValue& payload) {
        ++calls;
        return EditorResult<EditorValue>::applied(payload);
    });
    CHECK(registered.accepted());
    CHECK_EQ(service.revision(), static_cast<uint64_t>(1));

    auto duplicate = service.registerCommand(descriptor, [](const CommandContext&, const EditorValue&) {
        return EditorResult<EditorValue>::applied(EditorValue{});
    });
    CHECK_EQ(static_cast<int>(duplicate.status), static_cast<int>(EditorStatus::Rejected));

    HostProfile    runtime = HostProfile::runtimeBuilder();
    CommandContext context;
    context.profile = &runtime;
    auto denied     = service.execute(descriptor.id, context, EditorValue("New Name"));
    CHECK_EQ(static_cast<int>(denied.status), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(calls, 0);

    runtime.allowCommand(descriptor.id);
    auto executed = service.execute(descriptor.id, context, EditorValue("New Name"));
    CHECK(executed.accepted());
    CHECK_EQ(calls, 1);
    CHECK(executed.value.has_value());
    CHECK_EQ(*executed.value->getIf<std::string>(), std::string("New Name"));
    CHECK_EQ(service.commands(runtime).size(), static_cast<size_t>(1));

    CHECK_EQ(service.unregisterOwner("scene"), static_cast<size_t>(1));
    CHECK(service.find(descriptor.id) == nullptr);
}

TEST_CASE("editor.v2.command_registry_gates_automation_and_exceptions") {
    EditorCommandService service;
    CommandDescriptor    descriptor;
    descriptor.id                = CommandId("scene.dangerous");
    descriptor.ownerModule       = "scene";
    descriptor.automationAllowed = false;
    CHECK(service
              .registerCommand(descriptor,
                               [](const CommandContext&, const EditorValue&) {
                                   throw std::runtime_error("boom");
                                   return EditorResult<EditorValue>::applied(EditorValue{});
                               })
              .accepted());

    HostProfile    developer = HostProfile::developer();
    CommandContext context;
    context.profile = &developer;
    context.source  = CommandSource::Automation;
    auto automation = service.execute(descriptor.id, context, {});
    CHECK_EQ(static_cast<int>(automation.status), static_cast<int>(EditorStatus::Rejected));

    context.source = CommandSource::Api;
    auto failure   = service.execute(descriptor.id, context, {});
    CHECK_EQ(static_cast<int>(failure.status), static_cast<int>(EditorStatus::Failed));
    CHECK_EQ(failure.diagnostics.size(), static_cast<size_t>(1));
    CHECK(failure.diagnostics.front().message.find("boom") != std::string::npos);
}

TEST_CASE("editor.v2.session_wraps_command_in_legacy_transaction") {
    TileBuffer           buffer(2, 2);
    TileBufferTarget     target("tiles", &buffer);
    EditorCommandService service;

    CommandDescriptor descriptor;
    descriptor.id               = CommandId("map.paint-one");
    descriptor.ownerModule      = "map";
    descriptor.displayName      = "Paint One Tile";
    descriptor.requiredFeatures = HostFeature::RuntimeWorld;
    CHECK(service
              .registerCommand(
                  descriptor,
                  [](const CommandContext& context, const EditorValue& payload) {
                      auto* after = payload.getIf<int64_t>();
                      if (!context.session || !after)
                          return EditorResult<EditorValue>::error(EditorStatus::Rejected, RuleId("map.paint.invalid"),
                                                                  "Paint command requires an integer value");
                      auto command = std::make_unique<IntFieldEditCommand>("Paint One Tile", context.session->target());
                      if (!command->record(0, 0, static_cast<int>(*after)) ||
                          !context.session->execute(std::move(command)))
                          return EditorResult<EditorValue>::error(EditorStatus::Failed, RuleId("map.paint.failed"),
                                                                  "Tile edit could not be applied");
                      return EditorResult<EditorValue>::applied(payload);
                  })
              .accepted());

    EditorSession session;
    HostProfile   runtime = HostProfile::runtimeBuilder();
    runtime.allowCommand(descriptor.id);
    session.setHostProfile(std::move(runtime));
    session.setCommandService(&service);
    session.bindTarget(&target);

    auto result = session.executeCommand(descriptor.id, EditorValue(7));
    CHECK(result.accepted());
    CHECK_EQ(buffer.getGid(0, 0), 7);
    CHECK(session.transactions().canUndo());
    CHECK(session.transactions().undo());
    CHECK_EQ(buffer.getGid(0, 0), 0);

    session.setHostProfile(HostProfile::runtimeBuilder());
    auto denied = session.executeCommand(descriptor.id, EditorValue(9));
    CHECK_EQ(static_cast<int>(denied.status), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(buffer.getGid(0, 0), 0);
}
