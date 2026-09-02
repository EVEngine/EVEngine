#include "PathBesideSource.h"

#include "action/ActionNotifyRegistry.h"
#include "animation/AnimClip.h"
#include "animation/AnimClipRegistry.h"
#include "animation/AnimImporter.h"
#include "animation/AnimSkeleton.h"
#include "action_editor/ActionPreviewController.h"
#include "action_editor/ActionTimelineEditor.h"
#include "action_editor/AnimationRootMotionPreviewSource.h"
#include "editor/EditorWorkspace.h"
#include "filesystem/Filesystem.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

eve::Duration seconds(double value) {
    auto converted = eve::Duration::fromSeconds(value);
    REQUIRE(converted.ok());
    return converted.value();
}

int animationIndex(const eve::model3d::ModelData& model, std::string_view name) {
    for (int index = 0; index < model.getAnimationCount(); ++index)
        if (model.getAnimationName(index) == name) return index;
    return -1;
}

int firstSkinnedMesh(const eve::model3d::ModelData& model) {
    for (int index = 0; index < model.getMeshCount(); ++index)
        if (model.hasBones(index)) return index;
    return -1;
}

std::unique_ptr<eve::animation::AnimClip> loadClip(eve::model3d::Model3D&              module,
                                                   const eve::animation::AnimSkeleton& skeleton,
                                                   const std::string& library, std::string_view clipName) {
    eve::model3d::ModelData* model = module.newModelDataFromFile(library);
    REQUIRE(model != nullptr);
    const int index = animationIndex(*model, clipName);
    REQUIRE(index >= 0);
    std::unique_ptr<eve::animation::AnimClip> clip(
        eve::animation::AnimImporter::loadClipFromModel(model, &skeleton, index));
    REQUIRE(clip.get() != nullptr);
    REQUIRE_EQ(clip->getName(), clipName);
    REQUIRE(clip->getDuration() > 0.0f);
    REQUIRE(clip->getTrackCount() >= skeleton.getBoneCount());
    return clip;
}

eve::action::ActionTimeline lightAttackTimeline(float clipDuration) {
    eve::action::ActionTimeline timeline;
    timeline.actionId     = id("combat:light-attack-kaykit");
    timeline.duration     = seconds(clipDuration);
    timeline.animationUri = "asset://kaykit/Rig_Medium_CombatMelee.glb#Melee_1H_Attack_Chop";
    timeline.metadata.emplace("character", eve::Value("asset://kaykit/Knight.glb"));
    timeline.metadata.emplace("weapon", eve::Value("asset://kaykit/sword_1handed.gltf"));
    timeline.metadata.emplace("sourceLicense", eve::Value("CC0-1.0"));

    eve::action::ActionTrack gameplay;
    gameplay.id    = id("kaykit-track:gameplay");
    gameplay.label = "Gameplay";
    gameplay.kind  = eve::action::ActionTrackKind::Gameplay;
    gameplay.states.push_back({id("kaykit-state:hitbox"),
                               id("combat:hitbox-window"),
                               seconds(clipDuration * 0.30),
                               seconds(clipDuration * 0.62),
                               {{"hitbox", eve::Value("weapon.main")}}});
    gameplay.states.push_back({id("kaykit-state:combo"),
                               id("input:combo-window"),
                               seconds(clipDuration * 0.55),
                               seconds(clipDuration * 0.82),
                               {{"input", eve::Value("Ability.Combat.Attack.Light")}}});
    gameplay.notifies.push_back({id("kaykit-notify:damage"),
                                 id("combat:damage"),
                                 seconds(clipDuration * 0.46),
                                 {{"damageType", eve::Value("Damage.Physical.Slash")}, {"amount", eve::Value(18)}}});

    eve::action::ActionTrack presentation;
    presentation.id    = id("kaykit-track:presentation");
    presentation.label = "Presentation";
    presentation.kind  = eve::action::ActionTrackKind::Effect;
    presentation.notifies.push_back({id("kaykit-notify:swing-audio"),
                                     id("presentation:audio"),
                                     seconds(clipDuration * 0.32),
                                     {{"uri", eve::Value("asset://audio/sword-whoosh")}}});
    presentation.notifies.push_back({id("kaykit-notify:swing-vfx"),
                                     id("presentation:vfx"),
                                     seconds(clipDuration * 0.34),
                                     {{"uri", eve::Value("asset://vfx/sword-arc")}}});
    presentation.notifies.push_back({id("kaykit-notify:impact-camera"),
                                     id("presentation:camera"),
                                     seconds(clipDuration * 0.46),
                                     {{"cue", eve::Value("combat.light-impact")}}});

    eve::action::ActionTrack movement;
    movement.id    = id("kaykit-track:movement");
    movement.label = "Root Motion";
    movement.kind  = eve::action::ActionTrackKind::Movement;
    movement.states.push_back({id("kaykit-state:root-motion"),
                               id("movement:root-motion-window"),
                               eve::Duration::zero(),
                               timeline.duration,
                               {{"mode", eve::Value("animation")}}});

    timeline.tracks.push_back(std::move(gameplay));
    timeline.tracks.push_back(std::move(presentation));
    timeline.tracks.push_back(std::move(movement));
    return timeline;
}

class RecordingPreviewSink final : public eve::action::IActionPreviewSink {
public:
    [[nodiscard]] eve::Result<void> prepare(const eve::action::ActionPreviewFrame& frame) override {
        prepared = frame;
        return eve::Result<void>::success();
    }

    void present(const eve::action::ActionPreviewFrame& frame) noexcept override { presented = frame; }
    void discardPrepared() noexcept override { discarded = true; }

    std::optional<eve::action::ActionPreviewFrame> prepared;
    std::optional<eve::action::ActionPreviewFrame> presented;
    bool                                           discarded = false;
};

}  // namespace

TEST_CASE("kaykitCombatAssets.importRigClipsAndDriveActionEditorPreview") {
    const std::string assetDir =
        eve_test_path::pathBesideTestDir(__FILE__, "../examples/combat-action-editor/assets/kaykit");
    auto* filesystem = eve::filesystem::Filesystem::create();
    REQUIRE(filesystem != nullptr);
    REQUIRE(filesystem->setIdentity("ev_ut_kaykit_combat", true));
    REQUIRE(filesystem->setupWriteDirectory());
    filesystem->allowMountingForPath(assetDir);
    REQUIRE(filesystem->mount(assetDir, "", false));

    auto* modelModule = eve::model3d::Model3D::create();
    REQUIRE(modelModule != nullptr);
    eve::model3d::ModelData* knight = modelModule->newModelDataFromFile("Knight.glb");
    REQUIRE(knight != nullptr);
    REQUIRE(knight->getMeshCount() > 0);
    const int skinnedMesh = firstSkinnedMesh(*knight);
    REQUIRE(skinnedMesh >= 0);
    REQUIRE(knight->getBoneCount(skinnedMesh) >= 20);

    eve::model3d::ModelData* sword = modelModule->newModelDataFromFile("sword_1handed.gltf");
    REQUIRE(sword != nullptr);
    REQUIRE(sword->getMeshCount() > 0);

    std::unique_ptr<eve::animation::AnimSkeleton> skeleton(eve::animation::AnimImporter::loadSkeletonFromModel(knight));
    REQUIRE(skeleton.get() != nullptr);
    REQUIRE(skeleton->getBoneCount() >= knight->getBoneCount(skinnedMesh));

    struct RequiredClip {
        const char* library;
        const char* name;
    };
    const std::array<RequiredClip, 10> coverage = {{
        {"Rig_Medium_General.glb", "Idle_A"},
        {"Rig_Medium_General.glb", "Hit_A"},
        {"Rig_Medium_General.glb", "Death_A"},
        {"Rig_Medium_MovementBasic.glb", "Running_A"},
        {"Rig_Medium_MovementBasic.glb", "Jump_Full_Short"},
        {"Rig_Medium_MovementAdvanced.glb", "Dodge_Forward"},
        {"Rig_Medium_CombatMelee.glb", "Melee_1H_Attack_Chop"},
        {"Rig_Medium_CombatMelee.glb", "Melee_Block"},
        {"Rig_Medium_CombatRanged.glb", "Ranged_Bow_Release"},
        {"Rig_Medium_CombatRanged.glb", "Ranged_Magic_Shoot"},
    }};
    for (const auto& required : coverage) {
        auto clip = loadClip(*modelModule, *skeleton, required.library, required.name);
        CHECK(clip->getDuration() > 0.0f);
    }

    auto attack   = loadClip(*modelModule, *skeleton, "Rig_Medium_CombatMelee.glb", "Melee_1H_Attack_Chop");
    auto timeline = lightAttackTimeline(attack->getDuration());
    REQUIRE(timeline.validate().ok());

    auto notifyRegistry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(notifyRegistry.ok());
    auto events = timeline.sample(eve::Duration::zero(), timeline.duration, true);
    REQUIRE(events.ok());
    REQUIRE_EQ(events.value().size(), 10u);
    for (const auto& event : events.value()) REQUIRE(notifyRegistry.value().validate(event).ok());

    eve::editor::ActionTimelineEditor editor("asset.kaykit.light-attack", timeline);
    eve::editor::EditorWorkspace      workspace("kaykit.combat", "KayKit Combat Action Editor");
    REQUIRE(editor.configureWorkspace(workspace).ok());
    CHECK_EQ(workspace.getPanelCount(), 4);
    CHECK_EQ(workspace.getActivePanel(), "action.timeline");

    const auto originalHitStart = timeline.tracks[0].states[0].start;
    REQUIRE(editor
                .resizeState(id("kaykit-state:hitbox"), seconds(attack->getDuration() * 0.28),
                             seconds(attack->getDuration() * 0.64))
                .ok());
    REQUIRE(editor.undo().ok());
    CHECK_EQ(editor.target().timeline().tracks[0].states[0].start, originalHitStart);

    eve::animation::AnimClipRegistry::registerPath(timeline.animationUri, attack.get());
    eve::editor::AnimationRootMotionPreviewSource rootMotion;
    RecordingPreviewSink                          sink;
    eve::editor::ActionPreviewController          preview(editor, sink, &rootMotion);
    REQUIRE(preview.setRootMotionSampleCount(16).ok());
    REQUIRE(preview.refresh().ok());
    REQUIRE(sink.presented.has_value());
    CHECK(static_cast<int>(sink.presented->rootMotionState) ==
          static_cast<int>(eve::action::RootMotionPreviewState::Available));
    REQUIRE_EQ(sink.presented->rootMotionPath.size(), 16u);

    editor.play();
    auto advanced = preview.update(timeline.duration);
    REQUIRE(advanced.ok());
    REQUIRE_EQ(advanced.value(), 10u);
    REQUIRE(sink.presented.has_value());
    REQUIRE_EQ(sink.presented->cues.size(), 10u);
    CHECK(!sink.discarded);

    CHECK(filesystem->unmount(assetDir));
}
