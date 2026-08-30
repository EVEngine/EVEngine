#include "editor/EditorAudioTransport.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

namespace {
class FakeAudioBackend final : public IAudioTransportBackend {
public:
    void play() override { isPlaying = true; ++plays; }
    void pause() override { isPlaying = false; }
    void stop() override { isPlaying = false; position = 0.0; ++stops; }
    EditorResult<void> seek(double seconds) override { if (!seekable) return EditorResult<void>::error(EditorStatus::Failed, RuleId("test.audio.seek"), "seek rejected"); position = seconds; return EditorResult<void>::applied(); }
    double tell() const override { return position; }
    double duration() const override { return length; }
    bool playing() const override { return isPlaying; }
    void setNativeLooping(bool enabled) override { nativeLoop = enabled; }
    double position = 0.0;
    double length = 10.0;
    bool isPlaying = false;
    bool seekable = true;
    bool nativeLoop = true;
    int plays = 0;
    int stops = 0;
};
}

TEST_CASE("editor.audio.transport_wraps_custom_loop_and_rejects_stale_revision") {
    FakeAudioBackend backend;
    AudioAuditionTransport transport;
    CHECK(transport.bind(StableId("voice"), 7, &backend).isAccepted());
    CHECK(!backend.nativeLoop);
    CHECK(transport.setLoop(7, true, 2.0, 4.0).isAccepted());
    CHECK(transport.play(7).isAccepted());
    CHECK_EQ(backend.position, 2.0);
    backend.position = 4.1;
    auto wrapped = transport.update(7);
    REQUIRE(wrapped.value);
    CHECK_EQ(wrapped.value->position, 2.0);
    CHECK_EQ(backend.plays, 2);
    CHECK_EQ(static_cast<int>(transport.update(8).status), static_cast<int>(EditorStatus::Conflict));
    CHECK_EQ(backend.stops, 1);
}

TEST_CASE("editor.audio.transport_validates_seek_loop_and_rebinding") {
    FakeAudioBackend first, second;
    AudioAuditionTransport transport;
    CHECK(transport.bind(StableId("first"), 1, &first).isAccepted());
    CHECK_EQ(static_cast<int>(transport.setLoop(1, true, 5.0, 2.0).status),
             static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(static_cast<int>(transport.seek(1, 11.0).status), static_cast<int>(EditorStatus::Rejected));
    CHECK(transport.play(1).isAccepted());
    CHECK(transport.pause(1).isAccepted());
    CHECK(transport.bind(StableId("second"), 2, &second).isAccepted());
    CHECK_EQ(first.stops, 1);
    auto snapshot = transport.snapshot(2);
    REQUIRE(snapshot.value);
    CHECK_EQ(snapshot.value->asset.value(), std::string("second"));
}
