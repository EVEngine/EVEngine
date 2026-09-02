#include "action_editor/ActionTimelineWidget.h"

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
    void line(const eve::editor::OverlayPoint&, const eve::editor::OverlayPoint&,
              const eve::editor::OverlayStyle&) override {
        ++lines;
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
    REQUIRE(view.addNotifyAtCursor(track, "presentation:vfx", {{"uri", eve::Value("asset://vfx/slash")}}).ok());
    REQUIRE(view.addStateAtCursor(track, "input:combo-window", eve::Duration::fromNanoseconds(10),
                                  {{"input", eve::Value("Attack.Heavy")}})
                .ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies.size(), 2u);
    CHECK_EQ(editor.target().timeline().tracks[0].states.size(), 2u);
    CHECK_EQ(view.insertableTypes(eve::action::ActionNotifyShape::Instant).size(), 5u);
    CHECK_EQ(view.insertableTypes(eve::action::ActionNotifyShape::State).size(), 5u);
    CHECK(!view.addNotifyAtCursor(track, "combat:hitbox-window", {{"hitbox", eve::Value("weapon")}}).ok());
}
