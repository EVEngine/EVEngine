#include "editor/EditorAudioTarget.h"
#include "editor/EditorAuthority.h"
#include "editor/EditorTransactionService.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <tuple>

using namespace eve::editor;

namespace {

SelectionSnapshot sourceSelection(const AudioSourceTarget& target) {
    SelectionSnapshot selection;
    selection.channel = "scene";
    SelectionItem item;
    item.domain = SelectionDomain::Scene;
    item.target = TargetId(target.targetId());
    item.item = StableId(target.targetId());
    item.type = "audio.source";
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

template <class Target>
EditorResult<TransactionReceipt> commit(Target& target, LocalTransactionBackend& transactions,
                                        const DomainOperation& operation, const char* id) {
    TransactionSpec specification;
    specification.id = TransactionId(id);
    specification.label = "Edit audio";
    specification.target = TargetId(target.targetId());
    specification.baseRevision = target.revision();
    auto begun = transactions.begin(std::move(specification));
    if (!begun.isAccepted())
        return EditorResult<TransactionReceipt>::error(begun.status, RuleId("test.audio.begin"),
                                                       "Could not begin audio transaction");
    auto appended = transactions.append(operation);
    if (!appended.isAccepted()) {
        [[maybe_unused]] const auto rolledBack = transactions.rollback();
        return EditorResult<TransactionReceipt>::error(appended.status, RuleId("test.audio.append"),
                                                       "Could not append audio operation");
    }
    return transactions.commit();
}

}  // namespace

TEST_CASE("editor.audio.source_schema_covers_playback_spatial_attenuation_and_routing") {
    AudioSourceTarget target("ambient-river");
    const auto schema = target.schema(sourceSelection(target));
    CHECK_EQ(schema.typeId, std::string("audio.source"));
    CHECK(schema.find(PropertyPath("clip.asset")) != nullptr);
    CHECK(schema.find(PropertyPath("play.loop-start")) != nullptr);
    CHECK(schema.find(PropertyPath("spatial.position")) != nullptr);
    CHECK(schema.find(PropertyPath("spatial.maximum-distance")) != nullptr);
    CHECK(schema.find(PropertyPath("mixer.bus")) != nullptr);
    CHECK_EQ(target.validate().size(), static_cast<std::size_t>(1));
}

TEST_CASE("editor.audio.source_edits_are_reversible_and_cross_validated") {
    AudioSourceTarget target("ambient-river");
    LocalWorldAuthority authority(&target);
    LocalTransactionBackend transactions(&authority);
    const SelectionSnapshot selection = sourceSelection(target);
    for (const auto& [path, value, id] : {
             std::tuple{PropertyPath("clip.asset"), EditorValue("asset://audio/river.ogg"), "audio.clip"},
             {PropertyPath("play.loop"), EditorValue(true), "audio.loop"},
             {PropertyPath("play.loop-start"), EditorValue(10.0), "audio.loop-start"},
             {PropertyPath("play.loop-end"), EditorValue(5.0), "audio.loop-end"}}) {
        auto operation = target.makeSet(selection, path, value, PropertySetMode::Absolute);
        REQUIRE(operation.value);
        REQUIRE(commit(target, transactions, *operation.value, id).isAccepted());
    }
    CHECK_EQ(target.validate().size(), static_cast<std::size_t>(1));
    REQUIRE(transactions.undo().isAccepted());
    CHECK(target.validate().empty());
    REQUIRE(transactions.redo().isAccepted());
    CHECK_EQ(target.validate().size(), static_cast<std::size_t>(1));

    AudioSourceTarget restored("restored");
    REQUIRE(restored.loadSnapshot(target.snapshotValue()).isAccepted());
    CHECK(restored.read(sourceSelection(restored), PropertyPath("clip.asset")).value ==
          EditorValue("asset://audio/river.ogg"));

    EditorValue unknownVersion = target.snapshotValue();
    auto* unknownRoot = unknownVersion.getIf<EditorValue::Object>();
    REQUIRE(unknownRoot != nullptr);
    unknownRoot->at("schemaVersion") = int64_t{2};
    const Revision revisionBeforeRejectedMigration = restored.revision();
    CHECK_EQ(static_cast<int>(restored.loadSnapshot(unknownVersion).status),
             static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(restored.revision(), revisionBeforeRejectedMigration);
    CHECK(restored.read(sourceSelection(restored), PropertyPath("clip.asset")).value ==
          EditorValue("asset://audio/river.ogg"));
}

TEST_CASE("editor.audio.mixer_bus_hierarchy_is_cycle_safe_and_reversible") {
    AudioMixerTarget mixer("main-mixer");
    LocalWorldAuthority authority(&mixer);
    LocalTransactionBackend transactions(&authority);
    auto music = mixer.makeCreate({ObjectId("music"), ObjectId("master"), "Music", 0.8});
    REQUIRE(music.value);
    REQUIRE(commit(mixer, transactions, *music.value, "audio.bus.music").isAccepted());
    auto combat = mixer.makeCreate({ObjectId("combat"), ObjectId("music"), "Combat", 1.0});
    REQUIRE(combat.value);
    REQUIRE(commit(mixer, transactions, *combat.value, "audio.bus.combat").isAccepted());
    CHECK_EQ(mixer.children(ObjectId("music")).size(), static_cast<std::size_t>(1));
    CHECK_EQ(static_cast<int>(mixer.makeDelete(ObjectId("music")).status),
             static_cast<int>(EditorStatus::Rejected));

    AudioBusSnapshot changed = *mixer.bus(ObjectId("music")).value;
    changed.parent = ObjectId("combat");
    CHECK_EQ(static_cast<int>(mixer.makeReplace(changed).status),
             static_cast<int>(EditorStatus::Rejected));
    changed = *mixer.bus(ObjectId("combat")).value;
    changed.volume = 0.5;
    changed.mute = true;
    auto replace = mixer.makeReplace(changed);
    REQUIRE(replace.value);
    REQUIRE(commit(mixer, transactions, *replace.value, "audio.bus.settings").isAccepted());
    CHECK_EQ(mixer.bus(ObjectId("combat")).value->volume, 0.5);
    CHECK(mixer.bus(ObjectId("combat")).value->mute);
    REQUIRE(transactions.undo().isAccepted());
    CHECK_EQ(mixer.bus(ObjectId("combat")).value->volume, 1.0);
    CHECK(!mixer.bus(ObjectId("combat")).value->mute);
    CHECK_EQ(static_cast<int>(mixer.snapshotValue().type()),
             static_cast<int>(EditorValue::Type::Object));
    AudioMixerTarget restored("restored-mixer");
    REQUIRE(restored.loadSnapshot(mixer.snapshotValue()).isAccepted());
    CHECK_EQ(restored.children(ObjectId("music")).size(), static_cast<std::size_t>(1));
}
