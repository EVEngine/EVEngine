#include "action/editor/ActionTimelineWidget.h"

#include "action/ActionAudioWaveform.h"
#include "action/ActionParameterCurve.h"
#include "common/Capability.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <map>
#include <string_view>

namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

eve::action::ActionTimeline timelineFixture() {
    eve::action::ActionTimeline timeline;
    timeline.actionId = id("combat:widget-test");
    timeline.duration = eve::Duration::fromNanoseconds(100);
    eve::action::ActionTrack track;
    track.id    = id("combat-track:gameplay");
    track.label = "Gameplay";
    track.kind  = eve::action::ActionTrackKind::Gameplay;
    track.notifies.push_back({id("combat-notify:damage"),
                              id("combat:damage"),
                              eve::Duration::fromNanoseconds(20),
                              {{"damageType", eve::Value("Damage.Physical")}, {"amount", eve::Value(10)}}});
    track.states.push_back({id("combat-state:hitbox"),
                            id("combat:hitbox-window"),
                            eve::Duration::fromNanoseconds(10),
                            eve::Duration::fromNanoseconds(30),
                            {{"hitbox", eve::Value("weapon.main")}}});
    timeline.tracks.push_back(std::move(track));
    return timeline;
}

class RecordingOverlay final : public eve::editor::IEditorOverlay {
public:
    void line(const eve::editor::OverlayPoint& from, const eve::editor::OverlayPoint& to,
              const eve::editor::OverlayStyle&) override {
        ++lines;
        (void)from;
        (void)to;
    }
    void circle(const eve::editor::OverlayPoint&, float, const eve::editor::OverlayStyle&) override { ++circles; }
    void rectangle(const eve::editor::OverlayPoint&, const eve::editor::OverlayPoint&,
                   const eve::editor::OverlayStyle&) override {
        ++rectangles;
    }
    void text(const eve::editor::OverlayPoint&, const std::string&, const eve::editor::OverlayStyle&) override {
        ++texts;
    }

    int lines      = 0;
    int circles    = 0;
    int rectangles = 0;
    int texts      = 0;
};

class TestWaveformProvider final : public eve::action::IActionAudioWaveformProvider {
public:
    eve::Result<eve::action::ActionAudioWaveform> waveform(
        const eve::action::ActionAudioWaveformRequest& request) override {
        ++calls;
        lastRequest = request;
        eve::action::ActionAudioWaveform result;
        result.clipDurationSeconds = 0.5;
        result.buckets.assign(request.bucketCount, {-0.5f, 0.75f});
        return eve::Result<eve::action::ActionAudioWaveform>::success(std::move(result));
    }

    int calls = 0;
    eve::action::ActionAudioWaveformRequest lastRequest;
};

class EditingInspector final : public eve::editor::IEditorInspector {
public:
    void beginGroup(const std::string&, const std::string&) override {}
    void endGroup() override {}
    bool boolean(const std::string&, const std::string&, bool&) override { return false; }
    bool integer(const std::string&, const std::string&, int&, int, int) override { return false; }
    bool scalar(const std::string& field, const std::string&, float& value, float, float) override {
        auto found = scalarReplacements.find(field);
        if (found == scalarReplacements.end()) return false;
        value = found->second;
        return true;
    }
    bool string(const std::string& field, const std::string&, std::string& value) override {
        auto found = replacements.find(field);
        if (found == replacements.end()) return false;
        value = found->second;
        return true;
    }

    std::map<std::string, std::string> replacements;
    std::map<std::string, float>       scalarReplacements;
};

class CurveEditingInspector final : public eve::editor::IEditorInspector {
public:
    void beginGroup(const std::string& id, const std::string&) override { groups.push_back(id); }
    void endGroup() override { groups.pop_back(); }
    bool boolean(const std::string&, const std::string&, bool&) override { return false; }
    bool integer(const std::string&, const std::string&, int&, int, int) override { return false; }
    bool scalar(const std::string& id, const std::string&, float& value, float, float) override {
        if (!groups.empty() && groups.back() == "action.timeline.parameter-key.1" && id == "value") {
            value = 0.25f;
            return true;
        }
        return false;
    }
    bool string(const std::string&, const std::string&, std::string&) override { return false; }

    std::vector<std::string> groups;
};

eve::editor::ActionTimelineWidget widget(eve::editor::ActionTimelineEditor& editor,
                                         eve::action::ActionNotifyRegistry& registry) {
    eve::editor::ActionTimelineWidget result(editor, registry);
    REQUIRE(result.setViewport(1120.0f, 24.0f, 120.0f).ok());
    return result;
}

}  // namespace

TEST_CASE("actionTimelineWidget.projectsDrawsAndHitTestsSemanticItems") {
    eve::editor::ActionTimelineEditor editor("asset.combat.widget", timelineFixture());
    auto                              registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    auto view   = widget(editor, registry.value());
    auto layout = view.layout();
    REQUIRE_EQ(layout.items.size(), 2u);
    CHECK_EQ(layout.width, 1120.0f);
    CHECK_EQ(layout.height, 24.0f);

    auto notify = view.hitTest(320.0f, 12.0f);
    REQUIRE(notify.has_value());
    CHECK_EQ(notify->itemId, id("combat-notify:damage"));
    auto stateStart = view.hitTest(220.0f, 12.0f);
    REQUIRE(stateStart.has_value());
    CHECK(static_cast<int>(stateStart->part) == static_cast<int>(eve::editor::TimelineHitPart::StartHandle));

    RecordingOverlay overlay;
    view.draw(overlay);
    CHECK(overlay.rectangles >= 3);
    CHECK(overlay.lines >= 4);
    CHECK_EQ(overlay.texts, 1);
}

TEST_CASE("actionTimelineWidget.projectsBoundedAudioWaveformWithoutOwningAudioState") {
    auto timeline = timelineFixture();
    timeline.tracks[0].kind = eve::action::ActionTrackKind::Audio;
    timeline.tracks[0].notifies.clear();
    timeline.tracks[0].states.clear();
    timeline.tracks[0].states.push_back(
        {id("audio-state:loop"), id("presentation:audio-state"), eve::Duration::fromNanoseconds(10),
         eve::Duration::fromNanoseconds(90),
         {{"uri", "audio/hit.wav"}, {"pitch", 1.5}, {"looping", true}}});
    eve::editor::ActionTimelineEditor editor("asset.combat.widget-waveform", std::move(timeline));
    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    auto view = widget(editor, registry.value());
    CHECK(!view.layout().audioWaveformsAvailable);

    TestWaveformProvider provider;
    eve::cap::provide<eve::action::IActionAudioWaveformProvider>(&provider);
    CHECK(view.layout().audioWaveformsAvailable);
    RecordingOverlay overlay;
    view.draw(overlay);
    eve::cap::revoke<eve::action::IActionAudioWaveformProvider>(&provider);

    REQUIRE_EQ(provider.calls, 1);
    CHECK_EQ(provider.lastRequest.binding.uri, std::string("audio/hit.wav"));
    CHECK_EQ(provider.lastRequest.binding.pitch, 1.5);
    CHECK(provider.lastRequest.binding.looping);
    CHECK_EQ(provider.lastRequest.blockDuration, eve::Duration::fromNanoseconds(80));
    CHECK(provider.lastRequest.bucketCount > 0);
    CHECK(provider.lastRequest.bucketCount <= 512);
    CHECK(overlay.lines >= static_cast<int>(provider.lastRequest.bucketCount));
}

TEST_CASE("actionTimelineWidget.drawsAndTransactionallyInspectsParameterCurveKeys") {
    auto timeline = timelineFixture();
    timeline.tracks[0].states.clear();
    timeline.tracks[0].notifies.clear();
    timeline.tracks[0].states.push_back(
        {id("parameter-curve:fade"), id("presentation:parameter-curve"),
         eve::Duration::fromNanoseconds(10), eve::Duration::fromNanoseconds(90),
         {{"target", "audio:master-volume"}, {"operation", "multiply"},
          {"keys", eve::Value::Array{
                       eve::Value::Object{{"time", 0.0}, {"value", 1.0}, {"interpolation", "linear"}},
                       eve::Value::Object{{"time", 0.5}, {"value", 0.5}, {"interpolation", "cubic"}},
                       eve::Value::Object{{"time", 1.0}, {"value", 0.0}, {"interpolation", "linear"}}}}}});
    eve::editor::ActionTimelineEditor editor("asset.combat.widget-curve", std::move(timeline));
    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    auto view = widget(editor, registry.value());
    RecordingOverlay overlay;
    view.draw(overlay);
    CHECK(overlay.lines >= 128);
    CHECK(overlay.circles >= 3);

    REQUIRE(editor.selectItem(id("parameter-curve:fade")).ok());
    CurveEditingInspector inspector;
    REQUIRE(view.inspectSelection(inspector).ok());
    auto binding = eve::action::ActionParameterCurveBinding::fromPayload(
        editor.target().timeline().tracks[0].states[0].payload);
    REQUIRE(binding.ok());
    CHECK_EQ(binding.value().keys[1].value, 0.25);
    REQUIRE(editor.undo().ok());
    binding = eve::action::ActionParameterCurveBinding::fromPayload(
        editor.target().timeline().tracks[0].states[0].payload);
    REQUIRE(binding.ok());
    CHECK_EQ(binding.value().keys[1].value, 0.5);

    REQUIRE(editor.addParameterKey(id("parameter-curve:fade"),
                                   {0.25, 0.75, 0.0, 0.0,
                                    eve::action::ActionParameterInterpolation::Linear})
                .ok());
    binding = eve::action::ActionParameterCurveBinding::fromPayload(
        editor.target().timeline().tracks[0].states[0].payload);
    REQUIRE(binding.ok());
    REQUIRE_EQ(binding.value().keys.size(), 4u);
    CHECK_EQ(binding.value().keys[1].time, 0.25);
    CHECK(!editor.addParameterKey(id("parameter-curve:fade"), binding.value().keys[1]).ok());
    REQUIRE(editor.editParameterKey(id("parameter-curve:fade"), 1,
                                    {0.2, 0.8, 0.0, 0.0,
                                     eve::action::ActionParameterInterpolation::Step})
                .ok());
    REQUIRE(editor.removeParameterKey(id("parameter-curve:fade"), 1).ok());
    CHECK(!editor.removeParameterKey(id("parameter-curve:fade"), 0).ok());
    REQUIRE(editor.undo().ok());
    REQUIRE(editor.redo().ok());
    REQUIRE(editor.setTrackLocked(id("combat-track:gameplay"), true).ok());
    CHECK(!editor.addParameterKey(id("parameter-curve:fade"),
                                  {0.25, 0.75, 0.0, 0.0,
                                   eve::action::ActionParameterInterpolation::Linear})
               .ok());
}

TEST_CASE("actionTimelineWidget.dragPreviewCommitsOnceAndUndoRestores") {
    eve::editor::ActionTimelineEditor editor("asset.combat.widget", timelineFixture());
    auto                              registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    auto view = widget(editor, registry.value());

    REQUIRE(view.pointerDown(320.0f, 12.0f).ok());
    REQUIRE(view.pointerMove(520.0f).ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies[0].time, eve::Duration::fromNanoseconds(20));
    REQUIRE(view.pointerUp(520.0f).ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies[0].time, eve::Duration::fromNanoseconds(40));
    REQUIRE(editor.undo().ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies[0].time, eve::Duration::fromNanoseconds(20));

    REQUIRE(view.pointerDown(220.0f, 12.0f).ok());
    REQUIRE(view.pointerUp(270.0f).ok());
    CHECK_EQ(editor.target().timeline().tracks[0].states[0].start, eve::Duration::fromNanoseconds(15));
    CHECK_EQ(editor.target().timeline().tracks[0].states[0].end, eve::Duration::fromNanoseconds(30));
}

TEST_CASE("actionTimelineWidget.draggingSelectedBodyMovesWholeSelectionOnce") {
    eve::editor::ActionTimelineEditor editor("asset.combat.widget-selection", timelineFixture());
    auto                              registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    auto view = widget(editor, registry.value());
    REQUIRE(editor.selectItem(id("combat-notify:damage")).ok());
    REQUIRE(editor.selectItem(id("combat-state:hitbox"), true).ok());

    REQUIRE(view.pointerDown(320.0f, 12.0f).ok());
    REQUIRE(view.pointerUp(420.0f).ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies[0].time, eve::Duration::fromNanoseconds(30));
    CHECK_EQ(editor.target().timeline().tracks[0].states[0].start, eve::Duration::fromNanoseconds(20));
    CHECK_EQ(editor.target().timeline().tracks[0].states[0].end, eve::Duration::fromNanoseconds(40));
    REQUIRE(editor.undo().ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies[0].time, eve::Duration::fromNanoseconds(20));
    CHECK_EQ(editor.target().timeline().tracks[0].states[0].start, eve::Duration::fromNanoseconds(10));
}

TEST_CASE("actionTimelineWidget.multiSelectionInspectorScalesCanonicalItems") {
    eve::editor::ActionTimelineEditor editor("asset.combat.widget-selection-inspector", timelineFixture());
    auto                              registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    auto view = widget(editor, registry.value());
    REQUIRE(editor.selectItem(id("combat-notify:damage")).ok());
    REQUIRE(editor.selectItem(id("combat-state:hitbox"), true).ok());
    EditingInspector inspector;
    inspector.scalarReplacements["start"] = 0.00000002f;
    inspector.scalarReplacements["end"]   = 0.00000006f;
    REQUIRE(view.inspectSelection(inspector).ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies[0].time, eve::Duration::fromNanoseconds(40));
    CHECK_EQ(editor.target().timeline().tracks[0].states[0].start, eve::Duration::fromNanoseconds(20));
    CHECK_EQ(editor.target().timeline().tracks[0].states[0].end, eve::Duration::fromNanoseconds(60));
}

TEST_CASE("actionTimelineWidget.exposesUndoableSelectionAlignmentCommands") {
    eve::editor::ActionTimelineEditor editor("asset.combat.widget-selection-align", timelineFixture());
    auto                              registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    auto view = widget(editor, registry.value());
    REQUIRE(editor.selectItem(id("combat-notify:damage")).ok());
    REQUIRE(editor.selectItem(id("combat-state:hitbox"), true).ok());
    const auto commands = view.commands();
    REQUIRE_EQ(commands.size(), 8u);
    CHECK(commands[3].enabled);
    CHECK(commands[4].enabled);

    REQUIRE(view.handleShortcut("Shift+[").ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies[0].time, eve::Duration::fromNanoseconds(10));
    REQUIRE(editor.undo().ok());
    REQUIRE(view.handleShortcut("Shift+]").ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies[0].time, eve::Duration::fromNanoseconds(30));
}

TEST_CASE("actionTimelineWidget.inspectorUsesRegistryAndCanonicalPayload") {
    eve::editor::ActionTimelineEditor editor("asset.combat.widget", timelineFixture());
    auto                              registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    auto view = widget(editor, registry.value());
    REQUIRE(editor.selectItem(id("combat-notify:damage")).ok());

    EditingInspector inspector;
    inspector.replacements["type"]    = "gameplay:event";
    inspector.replacements["payload"] = R"({"tag":"Combat.Action.Hit"})";
    REQUIRE(view.inspectSelection(inspector).ok());
    const auto& notify = editor.target().timeline().tracks[0].notifies[0];
    CHECK_EQ(notify.type, id("gameplay:event"));
    CHECK(notify.payload.contains("tag"));

    inspector.replacements["type"]        = "combat:hitbox-window";
    inspector.scalarReplacements["start"] = 0.00000005f;
    CHECK(!view.inspectSelection(inspector).ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies[0].type, id("gameplay:event"));
    CHECK_EQ(editor.target().timeline().tracks[0].notifies[0].time, eve::Duration::fromNanoseconds(20));
}

TEST_CASE("actionTimelineWidget.commandsAndShortcutsAreUndoable") {
    eve::editor::ActionTimelineEditor editor("asset.combat.widget", timelineFixture());
    auto                              registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    auto view = widget(editor, registry.value());
    REQUIRE(editor.selectItem(id("combat-notify:damage")).ok());
    REQUIRE(view.handleShortcut("Ctrl+C").ok());
    REQUIRE(view.seek(720.0f).ok());
    REQUIRE(view.handleShortcut("Ctrl+V").ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies.size(), 2u);
    REQUIRE(view.handleShortcut("Delete").ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies.size(), 1u);
    REQUIRE(view.handleShortcut("Ctrl+Z").ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies.size(), 2u);
    CHECK(!view.handleShortcut("Ctrl+Unknown").ok());
}

TEST_CASE("actionTimelineWidget.insertsRegisteredNotifyShapesAtCursor") {
    eve::editor::ActionTimelineEditor editor("asset.combat.widget", timelineFixture());
    auto                              registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    auto view = widget(editor, registry.value());
    REQUIRE(view.seek(620.0f).ok());
    const auto track = id("combat-track:gameplay");
    REQUIRE(view.addNotifyAtCursor(track, "presentation:vfx",
                                   {{"uri", eve::Value("asset://vfx/slash")},
                                    {"lifetimeSeconds", eve::Value(0.5)}})
                .ok());
    REQUIRE(view.addStateAtCursor(track, "input:combo-window", eve::Duration::fromNanoseconds(10),
                                  {{"input", eve::Value("Attack.Heavy")}})
                .ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies.size(), 2u);
    CHECK_EQ(editor.target().timeline().tracks[0].states.size(), 2u);
    CHECK_EQ(view.insertableTypes(eve::action::ActionNotifyShape::Instant).size(), 5u);
    CHECK_EQ(view.insertableTypes(eve::action::ActionNotifyShape::State).size(), 7u);
    CHECK(!view.addNotifyAtCursor(track, "combat:hitbox-window", {{"hitbox", eve::Value("weapon")}}).ok());
}
