#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/EditorExtension.h"

#include <stdexcept>
#include <string>

using namespace eve::editor;

namespace {

class ParkBuilderExtension final : public IGameEditorExtension {
public:
    std::string ownerModule() const override { return "park"; }

    void registerEditor(IEditorExtensionRegistry& registry) override {
        CommandDescriptor append;
        append.id               = CommandId("park.track.append-segment");
        append.ownerModule      = "spoofed-owner";
        append.displayName      = "Append Track Segment";
        append.category         = "park.track";
        append.requiredFeatures = HostFeature::RuntimeWorld;
        CHECK(registry.registerCommand(
            append,
            [&](const CommandContext&, const EditorValue& payload) {
                ++calls;
                return eve::editing::applied<EditorValue>(payload);
            },
            ExtensionAudience::Developer | ExtensionAudience::Player | ExtensionAudience::Automation)
                  .ok());

        ExtensionToolDescriptor tool;
        tool.id                 = ToolId("park.track.build");
        tool.labelKey           = "editor.park.track.build";
        tool.category           = "park.track";
        tool.targetRequirements = {CapabilityId("park.target.track-network")};
        tool.audiences          = ExtensionAudience::Developer | ExtensionAudience::Player;
        CHECK(registry.registerTool(std::move(tool)).ok());

        ExtensionPaletteDescriptor developer;
        developer.id        = StableId("park.level-design-palette");
        developer.titleKey  = "editor.palette.level.rides";
        developer.tools     = {ToolId("park.track.build")};
        developer.commands  = {CommandId("park.track.append-segment")};
        developer.audiences = ExtensionAudience::Developer;
        CHECK(registry.registerPalette(std::move(developer)).ok());

        ExtensionPaletteDescriptor player;
        player.id        = StableId("park.build-rides-palette");
        player.titleKey  = "game.build.rides";
        player.tools     = {ToolId("park.track.build")};
        player.commands  = {CommandId("park.track.append-segment")};
        player.audiences = ExtensionAudience::Player;
        CHECK(registry.registerPalette(std::move(player)).ok());

        ExtensionRuleDescriptor budget;
        budget.id        = RuleId("park.policy.build-cost");
        budget.audiences = ExtensionAudience::Player | ExtensionAudience::Automation;
        CHECK(registry.registerRule(std::move(budget)).ok());
    }

    int calls = 0;
};

class ThrowingExtension final : public IGameEditorExtension {
public:
    std::string ownerModule() const override { return "throwing"; }
    void        registerEditor(IEditorExtensionRegistry& registry) override {
        CommandDescriptor command;
        command.id = CommandId("throwing.partial");
        CHECK(registry.registerCommand(
            command,
            [](const CommandContext&, const EditorValue&) { return eve::editing::applied<EditorValue>(EditorValue{}); },
            ExtensionAudience::Developer)
                  .ok());
        throw std::runtime_error("extension load failed");
    }
};

}  // namespace

TEST_CASE("editor.v2.game_extension_injects_same_tool_into_editor_and_game") {
    EditorCommandService    commands;
    EditorExtensionRegistry extensions(&commands);
    ParkBuilderExtension    park;
    CHECK(extensions.load(park).ok());

    HostProfile developer = HostProfile::developer();
    HostProfile runtime   = HostProfile::runtimeBuilder();
    extensions.configureProfile(runtime);
    CHECK(runtime.allowsCommand(CommandId("park.track.append-segment")));

    auto developerTools = extensions.tools(developer);
    auto runtimeTools   = extensions.tools(runtime);
    CHECK_EQ(developerTools.size(), static_cast<std::size_t>(1));
    CHECK_EQ(runtimeTools.size(), static_cast<std::size_t>(1));
    CHECK(developerTools.front().id == runtimeTools.front().id);
    CHECK_EQ(extensions.palettes(developer).front().id.value(), std::string("park.level-design-palette"));
    CHECK_EQ(extensions.palettes(runtime).front().id.value(), std::string("park.build-rides-palette"));
    CHECK_EQ(extensions.rules(developer).size(), static_cast<std::size_t>(0));
    CHECK_EQ(extensions.rules(runtime).size(), static_cast<std::size_t>(1));

    const CommandDescriptor* registered = commands.find(CommandId("park.track.append-segment"));
    CHECK(registered != nullptr);
    CHECK_EQ(registered->ownerModule, std::string("park"));

    CommandContext context;
    context.profile = &runtime;
    auto result     = commands.execute(CommandId("park.track.append-segment"), context, EditorValue("segment"));
    CHECK(result.ok());
    CHECK_EQ(park.calls, 1);

    CHECK(extensions.unload("park") > 0);
    CHECK(commands.find(CommandId("park.track.append-segment")) == nullptr);
    CHECK(extensions.tools(developer).empty());
}

TEST_CASE("editor.v2.extension_load_exception_rolls_back_partial_registration") {
    EditorCommandService    commands;
    EditorExtensionRegistry extensions(&commands);
    ThrowingExtension       throwing;
    auto                    result = extensions.load(throwing);
    CHECK_EQ(static_cast<int>(result.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK(commands.find(CommandId("throwing.partial")) == nullptr);
}
