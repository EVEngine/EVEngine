#include "animation/editor/AnimationClipEditor.h"
#include "animation/editor/AnimationClipPreview.h"
#include "audio/editor/AudioSourceEditor.h"
#include "audio/editor/AudioSourcePreview.h"
#include "editor/presentation/AsyncPreview.h"
#include "editor_preview_test_support.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <condition_variable>
#include <future>
#include <stdexcept>

namespace {
// Bounded even if a REQUIRE exits early. A provider deliberately ignores cancellation
// until released so tests exercise late completion, rather than timing luck.
struct Gate {
    std::mutex              mutex;
    std::condition_variable cv;
    bool                    entered   = false;
    bool                    released  = false;
    std::atomic_bool        cancelled = false;
    void                    block(const eve::editing::TaskContext& context) {
        std::unique_lock lock(mutex);
        entered = true;
        cv.notify_all();
        cv.wait_for(lock, std::chrono::seconds(3), [&] { return released; });
        if (context.isCancellationRequested()) cancelled = true;
    }
    bool waitEntered() {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, std::chrono::seconds(3), [&] { return entered; });
    }
    void release() {
        std::lock_guard lock(mutex);
        released = true;
        cv.notify_all();
    }
    ~Gate() { release(); }
};
class DelayedAudio final : public eve::audio_editor::IAudioSourcePreviewProvider {
public:
    std::shared_ptr<Gate>                                                     gate = std::make_shared<Gate>();
    eve::audio_editing::EditorResult<eve::audio_editing::AudioWaveformResult> generate(
        const eve::audio_editing::AudioWaveformRequest& request, const eve::audio_editing::EditorAudioPcm& pcm,
        const eve::editing::TaskContext& context) const override {
        auto result = eve::audio_editing::AudioWaveformService{}.generate(request, pcm);
        gate->block(context);
        return eve::editing::applied(std::move(result));
    }
};
class DelayedAnimation final : public eve::animation_editor::IAnimationClipPreviewProvider {
public:
    std::shared_ptr<Gate> gate = std::make_shared<Gate>();
    eve::animation_editing::EditorResult<eve::animation_editor::AnimationPreviewPublication> generate(
        const eve::animation_editing::AnimationClipDocumentTarget& document, double time,
        const std::vector<eve::animation_editor::AnimationPreviewBone>& skeleton, const std::string& selected,
        const eve::editing::TaskContext& context) const override {
        auto result = eve::animation_editor::makeAnimationClipPreviewProvider()->generate(document, time, skeleton,
                                                                                          selected, context);
        gate->block(context);
        return result;
    }
};
using eve::editor::PreviewPhase;
}  // namespace

TEST_CASE("editor.presentation.async_audio_cancels_superseded_request_and_provider_generation") {
    eve::audio_editor::AudioSourceEditor editor("async.audio");
    REQUIRE(preview_test::finish(editor).ok());
    const auto count   = editor.bucketCount();
    auto       delayed = std::make_shared<DelayedAudio>();
    REQUIRE(editor.setPreviewProvider(delayed).ok());
    const auto generation = editor.previewState().providerGeneration;
    REQUIRE(editor.setViewportWidth(123.0f).ok());
    REQUIRE(delayed->gate->waitEntered());
    const auto request = editor.previewState().request;
    REQUIRE(editor.setViewportWidth(321.0f).ok());
    CHECK(editor.previewState().request > request);
    CHECK_EQ(editor.bucketCount(), count);
    REQUIRE(editor.setPreviewProvider(eve::audio_editor::makeAudioSourcePreviewProvider()).ok());
    CHECK_EQ(editor.previewState().providerGeneration, generation + 1);
    REQUIRE(editor.requestPreview().ok());
    delayed->gate->release();
    REQUIRE(preview_test::finish(editor).ok());
    CHECK(delayed->gate->cancelled.load());
    CHECK_EQ(editor.bucketCount(), 321);
    CHECK_EQ(editor.layoutWidth(), 321.0f);
    CHECK(!editor.isPlaying());
    const auto published = editor.previewState().request;
    REQUIRE(editor.pollPreview().ok());
    CHECK_EQ(editor.previewState().request, published);
}

TEST_CASE("editor.presentation.async_animation_same_revision_seek_selection_and_provider_rebind") {
    eve::animation_editor::AnimationClipEditor editor("async.animation");
    REQUIRE(preview_test::finish(editor).ok());
    auto delayed = std::make_shared<DelayedAnimation>();
    REQUIRE(editor.setPreviewProvider(delayed).ok());
    const auto revision = editor.revision();
    REQUIRE(editor.seekSeconds(0.25).ok());
    REQUIRE(delayed->gate->waitEntered());
    const auto generation = editor.previewState().providerGeneration;
    REQUIRE(editor.seekSeconds(1.0).ok());
    REQUIRE(editor.selectBone("Spine").ok());
    REQUIRE(editor.setPreviewProvider(delayed).ok());
    CHECK_EQ(editor.previewState().providerGeneration, generation + 1);
    REQUIRE(editor.requestPreview().ok());
    delayed->gate->release();
    REQUIRE(preview_test::finish(editor).ok());
    CHECK_EQ(editor.revision(), revision);
    CHECK_EQ(editor.preview().time, 1.0);
    CHECK_EQ(editor.preview().documentRevision, revision);
    CHECK_EQ(editor.selectedBone(), std::string("Spine"));
    CHECK(editor.primitiveCount() > 0);
    CHECK(delayed->gate->cancelled.load());
}

namespace {
template <class Editor, class Delayed>
void checkCancellation(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    Editor editor("async.cancel");
    REQUIRE(preview_test::finish(editor).ok());
    auto delayed = std::make_shared<Delayed>();
    REQUIRE(editor.setPreviewProvider(delayed).ok());
    REQUIRE(editor.requestPreview().ok());
    REQUIRE(delayed->gate->waitEntered());
    REQUIRE(editor.cancelPreview().ok());
    CHECK(editor.previewState().phase == PreviewPhase::Cancelled);
    const auto cancelledRequest = editor.previewState().request;
    REQUIRE(editor.update(0.0).ok());
    CHECK_EQ(editor.previewState().request, cancelledRequest);
    REQUIRE(editor.setPreviewProvider(nullptr).ok());
    CHECK(editor.previewState().phase == PreviewPhase::Unavailable);
    CHECK(!editor.requestPreview().ok());
    const auto revision = editor.revision();
    if constexpr (std::is_same_v<Editor, eve::audio_editor::AudioSourceEditor>) {
        REQUIRE(editor.setProperty("play.volume", 0.3).ok());
    } else {
        REQUIRE(editor.setMaskWeight(0.3).ok());
    }
    CHECK(editor.revision() > revision);
    CHECK(editor.previewState().phase == PreviewPhase::Unavailable);
    delayed->gate->release();
    REQUIRE(editor.pollPreview().ok());
    REQUIRE(editor.setPreviewProvider(delayed).ok());
    REQUIRE(editor.requestPreview().ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK(editor.previewState().phase == PreviewPhase::Ready);
    editor.closePreview();
    editor.closePreview();
    CHECK(editor.previewState().phase == PreviewPhase::Closed);
    CHECK(!editor.requestPreview().ok());
    CHECK(!editor.setPreviewProvider(delayed).ok());
    REQUIRE(editor.pollPreview().ok());
}
}  // namespace
TEST_CASE("editor.presentation.async_audio_cancel_absent_restore_close_contract") {
    checkCancellation<eve::audio_editor::AudioSourceEditor, DelayedAudio>(_ZEROERR_TEST_CONTEXT);
}
TEST_CASE("editor.presentation.async_animation_cancel_absent_restore_close_contract") {
    checkCancellation<eve::animation_editor::AnimationClipEditor, DelayedAnimation>(_ZEROERR_TEST_CONTEXT);
}

TEST_CASE("editor.presentation.async_provider_exception_preserves_publication_and_recovers") {
    eve::editor::AsyncPreview<int> preview;
    REQUIRE(preview
                .request([](const eve::editing::TaskContext&) -> eve::editing::Result<int> {
                    throw std::runtime_error("injected provider failure");
                })
                .ok());
    const auto                               deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    std::optional<eve::editing::Result<int>> outcome;
    while (!(outcome = preview.poll()) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(outcome.has_value());
    CHECK(!outcome->ok());
    CHECK(preview.state().phase == PreviewPhase::Failed);
    REQUIRE(preview.request([](const eve::editing::TaskContext&) { return eve::editing::applied(42); }).ok());
    outcome.reset();
    while (!(outcome = preview.poll()) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(outcome.has_value());
    REQUIRE(outcome->ok());
    CHECK_EQ(outcome->value(), 42);
}

TEST_CASE("editor.presentation.async_task_discard_releases_queued_capture_and_waits_for_running") {
    eve::editing::TaskService tasks;
    auto                      gate  = std::make_shared<Gate>();
    auto                      first = tasks.submit("blocked", [gate](const eve::editing::TaskContext& context) {
        gate->block(context);
        return eve::editing::TaskOutcome{};
    });
    REQUIRE(first.ok());
    REQUIRE(gate->waitEntered());
    auto               capture = std::make_shared<int>(5);
    std::weak_ptr<int> weak    = capture;
    auto               second =
        tasks.submit("queued", [capture](const eve::editing::TaskContext&) { return eve::editing::TaskOutcome{}; });
    REQUIRE(second.ok());
    capture.reset();
    REQUIRE(tasks.discard(second.value()).ok());
    CHECK(weak.expired());
    CHECK(!tasks.snapshot(second.value()).ok());
    REQUIRE(tasks.discard(first.value()).ok());
    CHECK(!tasks.waitIdle(std::chrono::milliseconds(1)).ok());
    gate->release();
    REQUIRE(tasks.waitIdle(std::chrono::seconds(3)).ok());
    CHECK(gate->cancelled.load());
    CHECK(!tasks.snapshot(first.value()).ok());
}

namespace {
class InvalidAudio final : public eve::audio_editor::IAudioSourcePreviewProvider {
public:
    eve::audio_editing::EditorResult<eve::audio_editing::AudioWaveformResult> generate(
        const eve::audio_editing::AudioWaveformRequest& request, const eve::audio_editing::EditorAudioPcm& pcm,
        const eve::editing::TaskContext&) const override {
        auto result = eve::audio_editing::AudioWaveformService{}.generate(request, pcm);
        ++result.sourceRevision;
        return eve::editing::applied(std::move(result));
    }
};
class InvalidAnimation final : public eve::animation_editor::IAnimationClipPreviewProvider {
public:
    eve::animation_editing::EditorResult<eve::animation_editor::AnimationPreviewPublication> generate(
        const eve::animation_editing::AnimationClipDocumentTarget& document, double time,
        const std::vector<eve::animation_editor::AnimationPreviewBone>& skeleton, const std::string& selected,
        const eve::editing::TaskContext& context) const override {
        auto result = eve::animation_editor::makeAnimationClipPreviewProvider()->generate(document, time, skeleton,
                                                                                          selected, context);
        if (!result.ok()) return result;
        auto value = std::move(result).takeValue();
        ++value.overlay.targetRevision;
        return eve::editing::applied(std::move(value));
    }
};
template <class Editor, class Delayed>
void checkDestruction(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    auto editor = std::make_unique<Editor>("async.destroy");
    REQUIRE(preview_test::finish(*editor).ok());
    auto                   provider = std::make_shared<Delayed>();
    auto                   gate     = provider->gate;
    std::weak_ptr<Delayed> weak     = provider;
    REQUIRE(editor->setPreviewProvider(provider).ok());
    REQUIRE(editor->requestPreview().ok());
    REQUIRE(gate->waitEntered());
    provider.reset();
    CHECK(!weak.expired());
    // The owner is destroyed while the worker holds an immutable input snapshot.
    std::jthread release([gate] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        gate->release();
    });
    editor.reset();
    CHECK(gate->cancelled.load());
    CHECK(weak.expired());
}
}  // namespace
TEST_CASE("editor.presentation.async_audio_rejects_stale_provider_payload_atomically") {
    eve::audio_editor::AudioSourceEditor editor("async.audio.invalid");
    REQUIRE(preview_test::finish(editor).ok());
    const auto count = editor.bucketCount();
    const auto width = editor.layoutWidth();
    REQUIRE(editor.setPreviewProvider(std::make_shared<InvalidAudio>()).ok());
    REQUIRE(editor.setViewportWidth(123.0f).ok());
    CHECK(!preview_test::finish(editor).ok());
    CHECK(editor.previewState().phase == PreviewPhase::Failed);
    CHECK_EQ(editor.bucketCount(), count);
    CHECK_EQ(editor.layoutWidth(), width);
    REQUIRE(editor.setPreviewProvider(eve::audio_editor::makeAudioSourcePreviewProvider()).ok());
    REQUIRE(editor.requestPreview().ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK_EQ(editor.bucketCount(), 123);
}
TEST_CASE("editor.presentation.async_animation_rejects_partial_pose_overlay_publication") {
    eve::animation_editor::AnimationClipEditor editor("async.animation.invalid");
    REQUIRE(preview_test::finish(editor).ok());
    const auto time       = editor.preview().time;
    const auto primitives = editor.primitiveCount();
    REQUIRE(editor.setPreviewProvider(std::make_shared<InvalidAnimation>()).ok());
    REQUIRE(editor.seekSeconds(1.0).ok());
    CHECK(!preview_test::finish(editor).ok());
    CHECK(editor.previewState().phase == PreviewPhase::Failed);
    CHECK_EQ(editor.preview().time, time);
    CHECK_EQ(editor.primitiveCount(), primitives);
    REQUIRE(editor.setPreviewProvider(eve::animation_editor::makeAnimationClipPreviewProvider()).ok());
    REQUIRE(editor.requestPreview().ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK_EQ(editor.preview().time, 1.0);
    REQUIRE(editor.seekSeconds(editor.duration()).ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK_EQ(editor.preview().time, 0.0);  // A looping clip samples the endpoint at its start.
}
TEST_CASE("editor.presentation.async_audio_destruction_cancels_owning_worker_inputs") {
    checkDestruction<eve::audio_editor::AudioSourceEditor, DelayedAudio>(_ZEROERR_TEST_CONTEXT);
}
TEST_CASE("editor.presentation.async_animation_destruction_cancels_owning_worker_inputs") {
    checkDestruction<eve::animation_editor::AnimationClipEditor, DelayedAnimation>(_ZEROERR_TEST_CONTEXT);
}
