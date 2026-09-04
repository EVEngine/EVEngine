#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "audio_editor/AudioEditorModule.h"
#include "audio_editor/EditorAudioTarget.h"
#include "common/Capability.h"
#include "common/EditorAutomation.h"
#include "editor/Editor.h"

using namespace eve::editor;

TEST_CASE("editor.audio.adapter_registers_commands_and_owns_headless_targets") {
    Editor editor;
    eve::audio_editor::AudioEditorModule audioAdapter;
    auto* automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    const std::string commands = automation->invoke("commands", "{}");
    CHECK(commands.find("audio.source.property.set.v1") != std::string::npos);

    const std::string created =
        automation->invoke("target-create", R"({"target":"agent.audio","type":"audio-source"})");
    REQUIRE(created.find("\"status\":\"applied\"") != std::string::npos);

    const std::string changed = automation->invoke(
        "execute",
        R"({"target":"agent.audio","command":"audio.source.property.set.v1","payload":{"path":"play.volume","value":0.25}})");
    REQUIRE(changed.find("\"status\":\"applied\"") != std::string::npos);

    const std::string inspected = automation->invoke("inspect", R"({"target":"agent.audio"})");
    CHECK(inspected.find("\"play.volume\"") != std::string::npos);
    CHECK(inspected.find("0.25") != std::string::npos);

    const std::string undone = automation->invoke("undo", R"({"target":"agent.audio"})");
    REQUIRE(undone.find("\"status\":\"applied\"") != std::string::npos);
    const std::string restored = automation->invoke("inspect", R"({"target":"agent.audio"})");
    CHECK(restored.find("0.25") == std::string::npos);

    const std::string mixer =
        automation->invoke("target-create", R"({"target":"agent.mixer","type":"audio-mixer"})");
    REQUIRE(mixer.find("\"status\":\"applied\"") != std::string::npos);
    REQUIRE(automation->invoke("target-close", R"({"target":"agent.mixer"})")
                .find("\"status\":\"applied\"") != std::string::npos);
    REQUIRE(automation->invoke("target-close", R"({"target":"agent.audio"})")
                .find("\"status\":\"applied\"") != std::string::npos);
}

TEST_CASE("editor.audio.property_command_keeps_bound_document_histories_independent") {
    AudioSourceTarget river("level.river");
    AudioSourceTarget wind("level.wind");
    Editor editor;
    eve::audio_editor::AudioEditorModule audioAdapter;
    REQUIRE(editor.registerEditingTarget(river).ok());
    REQUIRE(editor.registerEditingTarget(wind).ok());
    auto* automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    REQUIRE(automation
                ->invoke("execute",
                         R"({"target":"level.river","command":"audio.source.property.set.v1","payload":{"path":"play.volume","value":0.4}})")
                .find("\"status\":\"applied\"") != std::string::npos);
    REQUIRE(automation
                ->invoke("execute",
                         R"({"target":"level.wind","command":"audio.source.property.set.v1","payload":{"path":"play.volume","value":0.8}})")
                .find("\"status\":\"applied\"") != std::string::npos);

    SelectionSnapshot riverSelection;
    riverSelection.channel = "audio";
    SelectionItem item;
    item.domain = SelectionDomain::Asset;
    item.target = TargetId(river.targetId());
    item.item   = StableId(river.targetId().value());
    item.type   = "audio.source";
    riverSelection.items.push_back(item);
    riverSelection.primary = item;
    SelectionSnapshot windSelection = riverSelection;
    windSelection.items.front().target = TargetId(wind.targetId());
    windSelection.items.front().item   = StableId(wind.targetId().value());
    windSelection.primary              = windSelection.items.front();

    CHECK_EQ(river.read(riverSelection, PropertyPath("play.volume")).value, EditorValue(0.4));
    CHECK_EQ(wind.read(windSelection, PropertyPath("play.volume")).value, EditorValue(0.8));

    REQUIRE(automation->invoke("undo", R"({"target":"level.river"})")
                .find("\"status\":\"applied\"") != std::string::npos);
    CHECK_EQ(river.read(riverSelection, PropertyPath("play.volume")).value, EditorValue(1.0));
    CHECK_EQ(wind.read(windSelection, PropertyPath("play.volume")).value, EditorValue(0.8));

    CHECK(editor.unregisterEditingTarget(TargetId("level.river")).ok());
    CHECK(editor.unregisterEditingTarget(TargetId("level.wind")).ok());
}
