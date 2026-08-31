#include "audio_editing/AudioTarget.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::audio_editing;

namespace {

SelectionSnapshot select(const AudioSourceTarget& target) {
    eve::editing::SelectionItem item;
    item.domain = eve::editing::SelectionDomain::Asset;
    item.target = TargetId(target.targetId());
    item.item   = StableId(target.targetId());
    item.type   = "audio.source";

    SelectionSnapshot selection;
    selection.channel = "audio";
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

class RecordingSink final : public IAudioSourceRuntimeSink {
public:
    EditorResult<void> publish(const AudioSourceTarget& candidate) override {
        ++calls;
        observedRevision = candidate.revision();
        if (reject)
            return EditorResult<void>::error(EditorStatus::Failed, RuleId("test.audio.publish"),
                                             "injected publication failure");
        return EditorResult<void>::applied();
    }

    bool     reject           = false;
    int      calls            = 0;
    Revision observedRevision = 0;
};

DomainOperation setClip(const AudioSourceTarget& target, std::string uri) {
    auto operation = target.makeSet(select(target), PropertyPath("clip.asset"), EditorValue(std::move(uri)),
                                    PropertySetMode::Absolute);
    REQUIRE(operation.value);
    return *operation.value;
}

}  // namespace

TEST_CASE("audio_editing.provider_absence_is_explicit_and_preserves_authoring_state") {
    AudioSourcePublishingTarget target("voice", nullptr);
    const Revision              before = target.revision();
    auto applied = target.applyDomainOperation(setClip(target.authoringTarget(), "asset://audio/voice.ogg"));
    CHECK(!applied.isAccepted());
    CHECK_EQ(static_cast<int>(applied.status), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.revision(), before);
}

TEST_CASE("audio_editing.publication_failure_is_atomic_and_success_commits_candidate") {
    RecordingSink               sink;
    AudioSourcePublishingTarget target("voice", &sink);
    const Revision              before = target.revision();
    const DomainOperation operation = setClip(target.authoringTarget(), "asset://audio/voice.ogg");

    sink.reject = true;
    auto rejected = target.applyDomainOperation(operation);
    CHECK(!rejected.isAccepted());
    CHECK_EQ(target.revision(), before);
    CHECK_EQ(sink.calls, 1);

    sink.reject = false;
    auto committed = target.applyDomainOperation(operation);
    CHECK(committed.isAccepted());
    CHECK_EQ(target.revision(), before + 1);
    CHECK_EQ(sink.calls, 2);
    CHECK_EQ(sink.observedRevision, before + 1);
}
