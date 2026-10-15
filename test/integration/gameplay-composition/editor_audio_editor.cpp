#include "editor/Editor.h"
#include <cmath>
#include <limits>
#include "audio/editor/AudioEditorModule.h"
#include "audio/editor/AudioSourceEditor.h"
#include "audio/editor/EditorAudioEffects.h"
#include "audio/editor/EditorAudioImportDiagnostics.h"
#include "audio/editor/EditorAudioTarget.h"
#include "common/Capability.h"
#include "common/EditorAutomation.h"
#include "editor/EditorWorkspace.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// editor_audio_editor.cpp
using namespace eve::editor;

TEST_CASE("editor.audio.adapter_registers_commands_and_owns_headless_targets") {
    Editor                               editor;
    eve::audio_editor::AudioEditorModule audioAdapter;
    auto*                                automation = eve::cap::query<eve::IEditorAutomation>();
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

    const std::string mixer = automation->invoke("target-create", R"({"target":"agent.mixer","type":"audio-mixer"})");
    REQUIRE(mixer.find("\"status\":\"applied\"") != std::string::npos);
    REQUIRE(automation->invoke("target-close", R"({"target":"agent.mixer"})").find("\"status\":\"applied\"") !=
            std::string::npos);
    REQUIRE(automation->invoke("target-close", R"({"target":"agent.audio"})").find("\"status\":\"applied\"") !=
            std::string::npos);
}

TEST_CASE("editor.audio.property_command_keeps_bound_document_histories_independent") {
    AudioSourceTarget                    river("level.river");
    AudioSourceTarget                    wind("level.wind");
    Editor                               editor;
    eve::audio_editor::AudioEditorModule audioAdapter;
    REQUIRE(editor.registerEditingTarget(river).ok());
    REQUIRE(editor.registerEditingTarget(wind).ok());
    auto* automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    REQUIRE(
        automation
            ->invoke(
                "execute",
                R"({"target":"level.river","command":"audio.source.property.set.v1","payload":{"path":"play.volume","value":0.4}})")
            .find("\"status\":\"applied\"") != std::string::npos);
    REQUIRE(
        automation
            ->invoke(
                "execute",
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
    riverSelection.primary             = item;
    SelectionSnapshot windSelection    = riverSelection;
    windSelection.items.front().target = TargetId(wind.targetId());
    windSelection.items.front().item   = StableId(wind.targetId().value());
    windSelection.primary              = windSelection.items.front();

    CHECK_EQ(river.read(riverSelection, PropertyPath("play.volume")).value, EditorValue(0.4));
    CHECK_EQ(wind.read(windSelection, PropertyPath("play.volume")).value, EditorValue(0.8));

    REQUIRE(automation->invoke("undo", R"({"target":"level.river"})").find("\"status\":\"applied\"") !=
            std::string::npos);
    CHECK_EQ(river.read(riverSelection, PropertyPath("play.volume")).value, EditorValue(1.0));
    CHECK_EQ(wind.read(windSelection, PropertyPath("play.volume")).value, EditorValue(0.8));

    CHECK(editor.unregisterEditingTarget(TargetId("level.river")).ok());
    CHECK(editor.unregisterEditingTarget(TargetId("level.wind")).ok());
}

// editor_audio_effects.cpp
using namespace eve::editor;

namespace {
AudioEffectRecord gain(const char* id, double decibels) {
    return {StableId(id), "gain", false, 1.0, EditorValue::Object{{"decibels", decibels}}};
}
class Sink final : public IAudioEffectChainSink {
public:
    Result<void> publish(const std::string& id, Revision sourceRevision,
                         const std::vector<AudioEffectRecord>& value) override {
        chain    = id;
        revision = sourceRevision;
        effects  = value;
        return eve::editing::applied<void>();
    }
    std::string                    chain;
    Revision                       revision = 0;
    std::vector<AudioEffectRecord> effects;
};
}  // namespace

TEST_CASE("editor.audio.effects_are_typed_reversible_ordered_and_assignable") {
    AudioEffectChainTarget chain("music-fx");
    auto                   first  = chain.makeSet(gain("pre", -3.0));
    auto                   second = chain.makeSet(gain("post", 2.0));
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK(chain.applyDomainOperation(first.value()).ok());
    CHECK(chain.applyDomainOperation(second.value()).ok());
    auto reorder = chain.makeReorder({StableId("post"), StableId("pre")});
    REQUIRE(reorder.ok());
    CHECK(chain.applyDomainOperation(reorder.value()).ok());
    CHECK_EQ(chain.effects().front().id.value(), std::string("post"));
    DomainOperation undo = reorder.value();
    undo.type            = reorder.value().inverseType;
    undo.payload         = reorder.value().inverse;
    CHECK(chain.applyDomainOperation(undo).ok());
    CHECK_EQ(chain.effects().front().id.value(), std::string("pre"));

    AudioMixerTarget mixer("mixer");
    auto             bus = mixer.makeCreate({ObjectId("music"), ObjectId("master"), "Music"});
    REQUIRE(bus.ok());
    CHECK(mixer.applyDomainOperation(bus.value()).ok());
    auto assign = chain.makeAssignToBus(mixer, ObjectId("music"));
    REQUIRE(assign.ok());
    CHECK(mixer.applyDomainOperation(assign.value()).ok());
    CHECK_EQ(mixer.bus(ObjectId("music")).value().effects.getIf<EditorValue::Array>()->size(), 2U);
}

TEST_CASE("editor.audio.effect_publisher_rejects_stale_revision_and_snapshot_is_atomic") {
    AudioEffectChainTarget chain("voice-fx");
    auto                   set = chain.makeSet(gain("gain", -2.0));
    REQUIRE(set.ok());
    CHECK(chain.applyDomainOperation(set.value()).ok());
    Sink                      sink;
    AudioEffectChainPublisher publisher;
    CHECK_EQ(static_cast<int>(publisher.publish(chain, chain.revision() - 1, sink).code()),
             static_cast<int>(EditorStatus::Conflict));
    CHECK(publisher.publish(chain, chain.revision(), sink).ok());
    CHECK_EQ(sink.effects.size(), 1U);
    EditorValue invalid = chain.snapshotValue();
    auto*       root    = invalid.getIf<EditorValue::Object>();
    REQUIRE(root);
    auto* effects = root->at("effects").getIf<EditorValue::Array>();
    REQUIRE(effects);
    effects->push_back(effects->front());
    CHECK_EQ(static_cast<int>(chain.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(chain.effects().size(), 1U);
    CHECK_EQ(static_cast<int>(chain.makeSet(gain("bad", 99.0)).code()), static_cast<int>(EditorStatus::Rejected));
}

// editor_audio_import_diagnostics.cpp
using namespace eve::editor;

TEST_CASE("editor.audio.import_diagnostics_detect_clip_dc_and_streaming_budget") {
    EditorAudioPcm pcm;
    pcm.sampleRate = 10;
    pcm.channels   = 2;
    pcm.samples    = {1.0F, 0.2F, 1.0F, 0.2F, 0.5F, 0.2F, 0.5F, 0.2F};
    AudioImportInspectionRequest request;
    request.asset              = "voice";
    request.sourceRevision     = 3;
    request.codec              = "wav";
    request.staticMemoryBudget = 8;
    AudioImportDiagnosticsService service;
    auto                          result = service.inspect(request, pcm);
    CHECK_EQ(static_cast<int>(result.status), static_cast<int>(EditorStatus::Rejected));

    pcm.sampleRate = 8000;
    result         = service.inspect(request, pcm);
    CHECK_EQ(static_cast<int>(result.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(result.channels.size(), 2U);
    CHECK_EQ(result.channels[0].clippedSamples, 2U);
    CHECK(result.channels[1].dcOffset > 0.19);
    CHECK(result.recommendStreaming);
    CHECK(result.diagnostics.size() >= 3U);
}

TEST_CASE("editor.audio.import_diagnostics_rejects_nonfinite_and_reports_silence") {
    AudioImportInspectionRequest request;
    request.asset          = "quiet";
    request.sourceRevision = 1;
    request.codec          = "ogg";
    EditorAudioPcm pcm;
    pcm.sampleRate = 48000;
    pcm.channels   = 1;
    pcm.samples.assign(480, 0.0F);
    AudioImportDiagnosticsService service;
    auto                          quiet = service.inspect(request, pcm);
    CHECK_EQ(static_cast<int>(quiet.status), static_cast<int>(EditorStatus::Applied));
    CHECK(!quiet.diagnostics.empty());
    pcm.samples[4] = std::numeric_limits<float>::infinity();
    CHECK_EQ(static_cast<int>(service.inspect(request, pcm).status), static_cast<int>(EditorStatus::Rejected));
}

// editor_audio_source_editor.cpp
using eve::audio_editor::AudioSourceEditor;
using eve::editor::EditorWorkspace;

TEST_CASE("editor.audio.source_editor_installs_workspace_and_reverses_volume") {
    AudioSourceEditor editor("preview.tone");
    EditorWorkspace   workspace("audio.preview", "Audio Source Editor");
    REQUIRE(editor.configureWorkspace(workspace).ok());
    CHECK_EQ(workspace.getPanelCount(), 4);
    CHECK_EQ(workspace.getPanelId(0), std::string("audio.sources"));
    CHECK_EQ(workspace.getPanelCapability(0), std::string("audio.source"));
    CHECK_EQ(workspace.getPanelContext(0), std::string("list"));
    CHECK_EQ(workspace.getPanelContext(1), std::string("preview"));
    CHECK_EQ(workspace.getPanelContext(2), std::string("inspector"));
    CHECK_EQ(workspace.getPanelContext(3), std::string("transport"));

    REQUIRE(editor.setViewportWidth(320.0f).ok());
    CHECK(editor.bucketCount() > 8);
    CHECK(editor.duration() > 1.9);

    const double before =
        editor.read("play.volume").value.getIf<double>() ? *editor.read("play.volume").value.getIf<double>() : 0.0;
    REQUIRE(editor.setProperty("play.volume", eve::audio_editing::EditorValue(0.25)).ok());
    CHECK_EQ(*editor.read("play.volume").value.getIf<double>(), 0.25);
    REQUIRE(editor.undo().ok());
    CHECK_EQ(*editor.read("play.volume").value.getIf<double>(), before);

    REQUIRE(editor.play().ok());
    REQUIRE(editor.update(0.2).ok());
    CHECK(editor.playhead() > 0.0);
    REQUIRE(editor.seekSeconds(0.75).ok());
    CHECK(std::abs(editor.playhead() - 0.75) < 0.02);
    REQUIRE(editor.seekX(editor.layoutWidth() * 0.5f).ok());
    CHECK(std::abs(editor.playhead() - editor.duration() * 0.5) < 0.05);
}

TEST_CASE("editor.audio.source_editor_property_edits_do_not_stale_audition") {
    AudioSourceEditor editor("preview.tone");
    REQUIRE(editor.play().ok());
    const auto pcm = editor.pcmRevision();
    REQUIRE(editor.setProperty("play.volume", eve::audio_editing::EditorValue(0.4)).ok());
    CHECK_EQ(editor.pcmRevision(), pcm);
    auto advanced = editor.update(0.05);
    REQUIRE(advanced.ok());
    CHECK_EQ(static_cast<int>(advanced.value().state),
             static_cast<int>(eve::audio_editing::AudioTransportState::Playing));
}
