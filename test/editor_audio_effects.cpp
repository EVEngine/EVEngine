#include "audio_editor/EditorAudioEffects.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

namespace {
AudioEffectRecord gain(const char* id, double decibels) {
    return {StableId(id), "gain", false, 1.0, EditorValue::Object{{"decibels", decibels}}};
}
class Sink final : public IAudioEffectChainSink {
public:
    EditorResult<void> publish(const std::string& id, Revision sourceRevision,
                               const std::vector<AudioEffectRecord>& value) override {
        chain=id;revision=sourceRevision;effects=value;return eve::editing::applied<void>();
    }
    std::string chain;Revision revision=0;std::vector<AudioEffectRecord>effects;
};
}

TEST_CASE("editor.audio.effects_are_typed_reversible_ordered_and_assignable") {
    AudioEffectChainTarget chain("music-fx");
    auto first=chain.makeSet(gain("pre",-3.0));auto second=chain.makeSet(gain("post",2.0));
    REQUIRE(first.ok());REQUIRE(second.ok());CHECK(chain.applyDomainOperation(first.value()).ok());
    CHECK(chain.applyDomainOperation(second.value()).ok());
    auto reorder=chain.makeReorder({StableId("post"),StableId("pre")});REQUIRE(reorder.ok());
    CHECK(chain.applyDomainOperation(reorder.value()).ok());CHECK_EQ(chain.effects().front().id.value(),std::string("post"));
    DomainOperation undo=reorder.value();undo.type=reorder.value().inverseType;undo.payload=reorder.value().inverse;
    CHECK(chain.applyDomainOperation(undo).ok());CHECK_EQ(chain.effects().front().id.value(),std::string("pre"));

    AudioMixerTarget mixer("mixer");auto bus=mixer.makeCreate({ObjectId("music"),ObjectId("master"),"Music"});
    REQUIRE(bus.ok());CHECK(mixer.applyDomainOperation(bus.value()).ok());
    auto assign=chain.makeAssignToBus(mixer,ObjectId("music"));REQUIRE(assign.ok());
    CHECK(mixer.applyDomainOperation(assign.value()).ok());
    CHECK_EQ(mixer.bus(ObjectId("music")).value().effects.getIf<EditorValue::Array>()->size(),2U);
}

TEST_CASE("editor.audio.effect_publisher_rejects_stale_revision_and_snapshot_is_atomic") {
    AudioEffectChainTarget chain("voice-fx");auto set=chain.makeSet(gain("gain",-2.0));REQUIRE(set.ok());
    CHECK(chain.applyDomainOperation(set.value()).ok());Sink sink;AudioEffectChainPublisher publisher;
    CHECK_EQ(static_cast<int>(publisher.publish(chain,chain.revision()-1,sink).code()),static_cast<int>(EditorStatus::Conflict));
    CHECK(publisher.publish(chain,chain.revision(),sink).ok());CHECK_EQ(sink.effects.size(),1U);
    EditorValue invalid=chain.snapshotValue();auto*root=invalid.getIf<EditorValue::Object>();REQUIRE(root);
    auto*effects=root->at("effects").getIf<EditorValue::Array>();REQUIRE(effects);effects->push_back(effects->front());
    CHECK_EQ(static_cast<int>(chain.loadSnapshot(invalid).code()),static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(chain.effects().size(),1U);
    CHECK_EQ(static_cast<int>(chain.makeSet(gain("bad",99.0)).code()),static_cast<int>(EditorStatus::Rejected));
}
