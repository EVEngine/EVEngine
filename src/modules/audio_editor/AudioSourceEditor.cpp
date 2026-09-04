#include "audio_editor/AudioSourceEditor.h"

#include "audio/Audio.h"
#include "audio/Source.h"
#include "audio_editing/AudioTarget.h"
#include "common/Exception.h"
#include "common/Module.h"
#include "sound/SoundData.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace eve::audio_editor {
namespace {

template <class T = void>
audio_editing::EditorResult<T> editorError(audio_editing::EditorStatus status, std::string rule,
                                           std::string message) {
    return eve::editing::failed<T>(status, audio_editing::RuleId(std::move(rule)), std::move(message));
}

double readNumber(const audio_editing::AudioSourceTarget& target, const editor::SelectionSnapshot& selection,
                  const char* path, double fallback) {
    auto value = target.read(selection, audio_editing::PropertyPath(path));
    if (const auto* real = value.value.getIf<double>()) return *real;
    if (const auto* integer = value.value.getIf<std::int64_t>()) return static_cast<double>(*integer);
    return fallback;
}

bool readFlag(const audio_editing::AudioSourceTarget& target, const editor::SelectionSnapshot& selection,
              const char* path, bool fallback) {
    auto        value = target.read(selection, audio_editing::PropertyPath(path));
    const auto* flag  = value.value.getIf<bool>();
    return flag ? *flag : fallback;
}

}  // namespace

void AudioClockTransportBackend::play() { playing_ = true; }
void AudioClockTransportBackend::pause() { playing_ = false; }
void AudioClockTransportBackend::stop() {
    playing_  = false;
    position_ = 0.0;
}

audio_editing::EditorResult<void> AudioClockTransportBackend::seek(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0 || seconds > duration_)
        return editorError(audio_editing::EditorStatus::Rejected, "editor.audio.clock-seek",
                           "Clock seek is outside the clip");
    position_ = seconds;
    return eve::editing::applied<void>();
}

void AudioClockTransportBackend::setDuration(double seconds) {
    duration_ = seconds > 0.0 && std::isfinite(seconds) ? seconds : 1.0;
    position_ = std::clamp(position_, 0.0, duration_);
}

void AudioClockTransportBackend::advance(double deltaSeconds) {
    if (!playing_ || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0) return;
    position_ += deltaSeconds;
    if (position_ >= duration_) {
        position_ = duration_;
        playing_  = false;
    }
}

AudioSourceEditor::AudioSourceEditor(std::string targetId)
    : target_(std::move(targetId)), authority_(&target_), transactions_(&authority_) {
    seedPreviewDocument();
    generateTonePcm();
    clock_.setDuration(static_cast<double>(pcm_.samples.size()) / static_cast<double>(pcm_.sampleRate));
    auto rebuilt = rebuildWaveform();
    if (!rebuilt.ok()) rebuilt.ignore("preview waveform uses an empty envelope when generation is rejected");
    auto bound = bindClockAudition();
    if (!bound.ok()) bound.ignore("clock audition bind is retried after workspace setup");
    auto looped = syncLoop();
    if (!looped.ok()) looped.ignore("preview loop uses clip duration when the seeded range is rejected");
    auto snap = transport_.snapshot(auditionRevision());
    if (snap.ok()) lastTransport_ = snap.value();
    else snap.ignore("transport snapshot waits until the first successful bind");
}

AudioSourceEditor::~AudioSourceEditor() {
    transport_.unbind();
    liveBackend_.reset();
    if (liveSource_) {
        delete liveSource_;
        liveSource_ = nullptr;
    }
}

void AudioSourceEditor::seedPreviewDocument() {
    audio_editing::EditorValue::Object properties;
    properties["clip.asset"]     = std::string("asset://preview/tone.sine");
    properties["clip.mode"]      = std::string("static");
    properties["play.autoplay"]  = false;
    properties["play.loop"]      = true;
    properties["play.volume"]    = 0.8;
    properties["play.pitch"]     = 1.0;
    properties["play.loop-start"] = 0.5;
    properties["play.loop-end"]   = 1.5;
    properties["spatial.relative"] = true;
    properties["spatial.reference-distance"] = 1.0;
    properties["spatial.maximum-distance"]   = 10000.0;
    properties["spatial.position"]  = audio_editing::EditorValue::Array{0.0, 0.0, 0.0};
    properties["spatial.velocity"]  = audio_editing::EditorValue::Array{0.0, 0.0, 0.0};
    properties["spatial.direction"] = audio_editing::EditorValue::Array{0.0, 0.0, -1.0};
    properties["mixer.bus"]         = std::string("master");
    audio_editing::EditorValue::Object root;
    root["schemaVersion"] = std::int64_t{1};
    root["properties"]    = audio_editing::EditorValue(std::move(properties));
    auto loaded = target_.loadSnapshot(audio_editing::EditorValue(std::move(root)));
    if (!loaded.ok())
        loaded.ignore("audio source editor keeps defaults when the preview snapshot is rejected");
}

void AudioSourceEditor::generateTonePcm() {
    constexpr int    kRate     = 22050;
    constexpr double kSeconds  = 2.0;
    const int        samples   = static_cast<int>(kRate * kSeconds);
    pcm_.sampleRate            = kRate;
    pcm_.channels              = 1;
    pcm_.samples.resize(static_cast<std::size_t>(samples));
    for (int i = 0; i < samples; ++i) {
        const double t      = static_cast<double>(i) / static_cast<double>(kRate);
        pcm_.samples[static_cast<std::size_t>(i)] =
            static_cast<float>(0.32 * std::sin(6.283185307179586 * 440.0 * t) +
                               0.12 * std::sin(6.283185307179586 * 660.0 * t));
    }
}

editor::SelectionSnapshot AudioSourceEditor::selection() const {
    editor::SelectionSnapshot snapshot;
    snapshot.channel = "audio";
    editor::SelectionItem item;
    item.domain = editor::SelectionDomain::Asset;
    item.target = editor::TargetId(target_.targetId());
    item.item   = editor::StableId(target_.targetId().value());
    item.type   = "audio.source";
    snapshot.items.push_back(item);
    snapshot.primary = item;
    return snapshot;
}

audio_editing::Revision AudioSourceEditor::auditionRevision() const {
    return audio_editing::Revision(pcmRevision_);
}

audio_editing::EditorResult<void> AudioSourceEditor::rebuildWaveform() {
    const int width = std::max(8, static_cast<int>(std::lround(viewportWidth_)));
    audio_editing::AudioWaveformRequest request;
    request.asset          = "asset://preview/tone.sine";
    request.sourceRevision = auditionRevision();
    request.pixelWidth     = width;
    request.loopStart      = readNumber(target_, selection(), "play.loop-start", 0.0);
    request.loopEnd        = readNumber(target_, selection(), "play.loop-end", duration());
    waveform_              = waveforms_.generate(request, pcm_);
    if (waveform_.status != audio_editing::EditorStatus::Applied)
        return editorError(waveform_.status, "editor.audio.waveform", "Could not rebuild the source waveform");
    return eve::editing::applied<void>();
}

audio_editing::EditorResult<void> AudioSourceEditor::bindClockAudition() {
    transport_.unbind();
    liveBound_ = false;
    clock_.setDuration(duration());
    return transport_.bind(audio_editing::StableId(target_.targetId().value()), auditionRevision(), &clock_);
}

audio_editing::EditorResult<void> AudioSourceEditor::syncLoop() {
    const bool   loop  = readFlag(target_, selection(), "play.loop", false);
    const double start = readNumber(target_, selection(), "play.loop-start", 0.0);
    double       end   = readNumber(target_, selection(), "play.loop-end", 0.0);
    if (end <= 0.0) end = duration();
    auto applied = transport_.setLoop(auditionRevision(), loop, start, end);
    if (!applied.ok()) return applied;
    return rebuildWaveform();
}

audio_editing::EditorResult<void> AudioSourceEditor::publishLive() {
    if (!liveSource_) return eve::editing::applied<void>();
    return audio_editing::AudioSourceRuntimeApplier().apply(target_, liveSource_);
}

audio_editing::EditorResult<void> AudioSourceEditor::configureWorkspace(editor::EditorWorkspace& workspace) const {
    editor::EditorWorkspace candidate = workspace;
    struct Panel {
        const char* id;
        const char* title;
        const char* region;
        const char* context;
        int         order;
    };
    constexpr Panel panels[] = {
        {"audio.sources", "Sources", "left", "list", 100},
        {"audio.waveform", "Waveform", "center", "preview", 100},
        {"audio.inspector", "Source Inspector", "right", "inspector", 100},
        {"audio.transport", "Transport", "bottom", "transport", 100},
    };
    for (const auto& panel : panels) {
        if (!candidate.registerPanel(panel.id, panel.title, panel.region, panel.order) ||
            !candidate.setPanelCapability(panel.id, "audio.source") ||
            !candidate.setPanelContext(panel.id, panel.context))
            return editorError(audio_editing::EditorStatus::Rejected, "editor.audio.workspace-conflict",
                               "Could not install the audio source workspace composition");
    }
    if (!candidate.activatePanel("audio.waveform"))
        return editorError(audio_editing::EditorStatus::Rejected, "editor.audio.workspace-activate",
                           "Could not activate the waveform panel");
    workspace = std::move(candidate);
    return eve::editing::applied<void>();
}

audio_editing::EditorResult<void> AudioSourceEditor::setViewportWidth(float width) {
    if (!std::isfinite(width) || width < 8.0f)
        return editorError(audio_editing::EditorStatus::Rejected, "editor.audio.viewport",
                           "Waveform viewport width must be at least 8 pixels");
    viewportWidth_ = width;
    return rebuildWaveform();
}

audio_editing::EditorResult<void> AudioSourceEditor::seekX(float x) {
    if (!std::isfinite(x) || viewportWidth_ <= 0.0f)
        return editorError(audio_editing::EditorStatus::Rejected, "editor.audio.seek-x",
                           "Waveform seek requires a finite x and viewport");
    const double seconds = std::clamp(static_cast<double>(x / viewportWidth_), 0.0, 1.0) * duration();
    return seekSeconds(seconds);
}

audio_editing::EditorResult<void> AudioSourceEditor::seekSeconds(double seconds) {
    auto sought = transport_.seek(auditionRevision(), seconds);
    if (!sought.ok()) return sought;
    auto snap = transport_.snapshot(auditionRevision());
    if (snap.ok()) lastTransport_ = snap.value();
    return eve::editing::applied<void>();
}

audio_editing::EditorResult<void> AudioSourceEditor::setProperty(const std::string& path,
                                                                 const audio_editing::EditorValue& value) {
    auto operation = target_.makeSet(selection(), audio_editing::PropertyPath(path), value,
                                     audio_editing::PropertySetMode::Absolute);
    if (!operation.ok())
        return audio_editing::EditorResult<void>::failure(operation.status());
    editor::TransactionSpec spec;
    spec.id           = editor::TransactionId("audio.source.tx." + std::to_string(++txSequence_));
    spec.label        = "Set " + path;
    spec.target       = editor::TargetId(target_.targetId());
    spec.baseRevision = target_.revision();
    spec.mergeKey     = "audio.source:" + target_.targetId().value() + ":" + path;
    auto begun        = transactions_.begin(std::move(spec));
    if (!begun.ok())
        return editorError(begun.code(), "editor.audio.begin", "Could not begin the audio property transaction");
    auto appended = transactions_.append(std::move(operation).takeValue());
    if (!appended.ok()) {
        (void)transactions_.rollback();
        return audio_editing::EditorResult<void>::failure(appended.status());
    }
    auto committed = transactions_.commit();
    if (!committed.ok())
        return audio_editing::EditorResult<void>::failure(committed.status());
    auto looped = syncLoop();
    if (!looped.ok()) return looped;
    return publishLive();
}

audio_editing::EditorResult<editor::TransactionReceipt> AudioSourceEditor::undo() {
    auto result = transactions_.undo();
    if (!result.ok()) return result;
    auto looped = syncLoop();
    if (!looped.ok())
        return audio_editing::EditorResult<editor::TransactionReceipt>::failure(looped.status());
    auto published = publishLive();
    if (!published.ok())
        return audio_editing::EditorResult<editor::TransactionReceipt>::failure(published.status());
    return result;
}

audio_editing::EditorResult<editor::TransactionReceipt> AudioSourceEditor::redo() {
    auto result = transactions_.redo();
    if (!result.ok()) return result;
    auto looped = syncLoop();
    if (!looped.ok())
        return audio_editing::EditorResult<editor::TransactionReceipt>::failure(looped.status());
    auto published = publishLive();
    if (!published.ok())
        return audio_editing::EditorResult<editor::TransactionReceipt>::failure(published.status());
    return result;
}

audio_editing::EditorResult<void> AudioSourceEditor::play() {
    auto played = transport_.play(auditionRevision());
    if (!played.ok()) return played;
    auto snap = transport_.snapshot(auditionRevision());
    if (snap.ok()) lastTransport_ = snap.value();
    return eve::editing::applied<void>();
}

audio_editing::EditorResult<void> AudioSourceEditor::pause() {
    auto paused = transport_.pause(auditionRevision());
    if (!paused.ok()) return paused;
    auto snap = transport_.snapshot(auditionRevision());
    if (snap.ok()) lastTransport_ = snap.value();
    return eve::editing::applied<void>();
}

audio_editing::EditorResult<void> AudioSourceEditor::stop() {
    auto stopped = transport_.stop(auditionRevision());
    if (!stopped.ok()) return stopped;
    auto snap = transport_.snapshot(auditionRevision());
    if (snap.ok()) lastTransport_ = snap.value();
    return eve::editing::applied<void>();
}

audio_editing::EditorResult<audio_editing::AudioTransportSnapshot> AudioSourceEditor::update(double deltaSeconds) {
    if (!liveBound_) clock_.advance(deltaSeconds);
    auto observed = transport_.update(auditionRevision());
    if (observed.ok()) lastTransport_ = observed.value();
    return observed;
}

audio_editing::EditorResult<void> AudioSourceEditor::attachLiveAudition() {
    auto* audioMod = eve::ModuleManager::getInstance<eve::audio::Audio>("Audio");
    if (!audioMod)
        return editorError(audio_editing::EditorStatus::Unsupported, "editor.audio.live-module",
                           "Audio module is not available for live audition");
    std::vector<std::uint8_t> bytes(pcm_.samples.size() * 2);
    for (std::size_t i = 0; i < pcm_.samples.size(); ++i) {
        const float   sample = std::clamp(pcm_.samples[i], -1.0f, 1.0f);
        const auto    quantized = static_cast<std::int16_t>(std::lround(sample * 32767.0f));
        bytes[i * 2]            = static_cast<std::uint8_t>(quantized & 0xff);
        bytes[i * 2 + 1]        = static_cast<std::uint8_t>((quantized >> 8) & 0xff);
    }
    const bool   wasPlaying = isPlaying();
    const double head       = playhead();
    try {
        soundData_ = std::make_unique<eve::sound::SoundData>(std::move(bytes), pcm_.sampleRate, 16, 1);
        transport_.unbind();
        liveSource_ = audioMod->newSource(soundData_.get());
    } catch (const eve::Exception& ex) {
        soundData_.reset();
        liveSource_ = nullptr;
        auto restored = bindClockAudition();
        if (!restored.ok()) restored.ignore("clock audition restored after live attach failure");
        return editorError(audio_editing::EditorStatus::Failed, "editor.audio.live-source",
                           std::string("Live audition source could not be created: ") + ex.what());
    }
    liveBackend_ = std::make_unique<audio_editing::AudioSourceTransportBackend>(liveSource_);
    auto bound   = transport_.bind(audio_editing::StableId(target_.targetId().value()), auditionRevision(),
                                   liveBackend_.get());
    if (!bound.ok()) {
        liveBackend_.reset();
        delete liveSource_;
        liveSource_ = nullptr;
        soundData_.reset();
        auto restored = bindClockAudition();
        if (!restored.ok()) restored.ignore("clock audition restored after live bind failure");
        auto looped = syncLoop();
        if (!looped.ok()) looped.ignore("loop restored after live bind failure");
        return bound;
    }
    liveBound_ = true;
    auto looped = syncLoop();
    if (!looped.ok()) return looped;
    auto published = publishLive();
    if (!published.ok()) return published;
    auto sought = transport_.seek(auditionRevision(), head);
    if (!sought.ok()) return sought;
    if (wasPlaying) return play();
    return eve::editing::applied<void>();
}

bool AudioSourceEditor::isPlaying() const noexcept {
    return lastTransport_.state == audio_editing::AudioTransportState::Playing;
}

double AudioSourceEditor::duration() const noexcept {
    if (pcm_.sampleRate <= 0 || pcm_.samples.empty() || pcm_.channels <= 0) return 0.0;
    return static_cast<double>(pcm_.samples.size() / static_cast<std::size_t>(pcm_.channels)) /
           static_cast<double>(pcm_.sampleRate);
}

double AudioSourceEditor::playhead() const noexcept { return lastTransport_.position; }

int AudioSourceEditor::bucketCount() const noexcept {
    if (waveform_.envelopes.empty()) return 0;
    return static_cast<int>(waveform_.envelopes.front().size());
}

float AudioSourceEditor::playheadX() const noexcept {
    if (duration() <= 0.0) return 0.0f;
    return static_cast<float>(playhead() / duration()) * viewportWidth_;
}

float AudioSourceEditor::loopStartX() const noexcept {
    if (duration() <= 0.0) return 0.0f;
    return static_cast<float>(lastTransport_.loopStart / duration()) * viewportWidth_;
}

float AudioSourceEditor::loopEndX() const noexcept {
    if (duration() <= 0.0) return viewportWidth_;
    const double end = lastTransport_.loopEnd > 0.0 ? lastTransport_.loopEnd : duration();
    return static_cast<float>(end / duration()) * viewportWidth_;
}

audio_editing::PropertyReadResult AudioSourceEditor::read(const std::string& path) const {
    return target_.read(selection(), audio_editing::PropertyPath(path));
}

const audio_editing::AudioWaveformBucket* AudioSourceEditor::bucket(int index) const {
    if (waveform_.envelopes.empty()) return nullptr;
    const auto& row = waveform_.envelopes.front();
    if (index < 0 || static_cast<std::size_t>(index) >= row.size()) return nullptr;
    return &row[static_cast<std::size_t>(index)];
}

}  // namespace eve::audio_editor
