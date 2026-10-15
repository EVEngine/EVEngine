#include "audio/editing/AudioEditingCommands.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include "audio/editing/AudioEditingProvider.h"
#include "audio/editing/AudioImportDiagnostics.h"
#include "audio/editing/AudioTarget.h"

// AudioEditingCommands.cpp
namespace eve::audio_editing {
namespace {

const editing::Value* field(const editing::Value& value, const char* key) {
    const auto* object = value.getIf<editing::Value::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

editing::SelectionSnapshot selectionFor(editing::IEditableTarget& target) {
    editing::SelectionSnapshot selection;
    selection.channel = "audio";
    editing::SelectionItem item;
    item.domain = editing::SelectionDomain::Asset;
    item.target = editing::TargetId(target.targetId());
    item.item   = editing::StableId(target.targetId().value());
    item.type   = "audio.source";
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

}  // namespace

editing::Result<void> registerEditingCommands(editing::IEditingCommandRegistry& registry) {
    editing::EditingCommandDescriptor descriptor;
    descriptor.id                = editing::CommandId("audio.source.property.set.v1");
    descriptor.ownerModule       = "audio_editing";
    descriptor.displayName       = "Set audio source property";
    descriptor.category          = "Audio";
    descriptor.automationAllowed = true;
    return registry.registerPlannedCommand(std::move(descriptor), [](editing::IEditableTarget&      target,
                                                                     const editing::CommandRequest& request) {
        auto* properties =
            static_cast<editing::IPropertyProvider*>(target.queryCapability(AudioSourceTarget::editingCapabilityId()));
        const editing::Value* pathValue = field(request.payload, "path");
        const auto*           path      = pathValue ? pathValue->getIf<std::string>() : nullptr;
        const editing::Value* value     = field(request.payload, "value");
        if (!properties || !path || !value)
            return eve::editing::failed<editing::CommandPlan>(
                editing::Status::Rejected, editing::RuleId("audio.editing.property-payload"),
                "Audio property requires a source target, path and value");
        auto operation = properties->makeSet(selectionFor(target), editing::PropertyPath(*path), *value,
                                             editing::PropertySetMode::Absolute);
        if (!operation.ok())
            return eve::editing::failed<editing::CommandPlan>(operation.code(),
                                                              editing::RuleId("audio.editing.property-operation"),
                                                              "Audio source rejected the property value");
        editing::CommandPlan plan;
        plan.operations.push_back(std::move(operation.value()));
        plan.summary = editing::Value::Object{{"path", *path}};
        return eve::editing::applied<editing::CommandPlan>(std::move(plan));
    });
}

}  // namespace eve::audio_editing

// AudioEditingProvider.cpp
namespace eve::audio_editing {
namespace {
class AudioEditingFactory final : public IAudioEditingFactory {
public:
    std::unique_ptr<AudioSourcePublishingTarget> createSource(std::string              id,
                                                              IAudioSourceRuntimeSink* sink) const override {
        return std::make_unique<AudioSourcePublishingTarget>(std::move(id), sink);
    }
};
}  // namespace

editing::Result<editing::ProviderHandle> registerEditingProvider(editing::ExtensionProviderRegistry& registry) {
    editing::ExtensionDescriptor descriptor;
    descriptor.id              = "audio.editing";
    descriptor.schemaVersion   = 1;
    descriptor.requiredModules = {"audio"};
    return editing::registerStaticProvider(registry, std::move(descriptor), IAudioEditingFactory::capabilityId(),
                                           std::make_shared<AudioEditingFactory>());
}

}  // namespace eve::audio_editing

// AudioImportDiagnostics.cpp
namespace eve::audio_editing {

AudioImportInspectionResult AudioImportDiagnosticsService::inspect(const AudioImportInspectionRequest& request,
                                                                   const EditorAudioPcm&               pcm) const {
    AudioImportInspectionResult result;
    result.asset          = request.asset;
    result.sourceRevision = request.sourceRevision;
    if (request.asset.empty() || request.sourceRevision == 0 || request.codec.empty() || pcm.sampleRate < 8000 ||
        pcm.sampleRate > 384000 || pcm.channels < 1 || pcm.channels > 8 || pcm.samples.empty() ||
        pcm.samples.size() % static_cast<std::size_t>(pcm.channels) != 0 ||
        pcm.samples.size() > request.maximumSamples || request.maximumSamples == 0 ||
        !std::isfinite(request.clippingThreshold) || request.clippingThreshold <= 0.0 ||
        request.clippingThreshold > 1.0 || !std::isfinite(request.silenceThreshold) || request.silenceThreshold < 0.0 ||
        request.silenceThreshold >= request.clippingThreshold) {
        result.status = EditorStatus::Rejected;
        result.diagnostics.push_back(eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::InvalidArgument, RuleId("editor.audio.invalid-import-inspection"),
            DiagnosticSeverity::Error, "Audio inspection requires valid identity, format, thresholds and bounded PCM"));
        return result;
    }
    const std::size_t frames = pcm.samples.size() / static_cast<std::size_t>(pcm.channels);
    result.duration          = static_cast<double>(frames) / static_cast<double>(pcm.sampleRate);
    if (pcm.samples.size() > std::numeric_limits<std::size_t>::max() / sizeof(float)) {
        result.status = EditorStatus::Rejected;
        result.diagnostics.push_back(eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::InvalidArgument, RuleId("editor.audio.decoded-size-overflow"),
            DiagnosticSeverity::Error, "Decoded audio size exceeds the host range"));
        return result;
    }
    result.decodedBytes       = pcm.samples.size() * sizeof(float);
    result.compressionRatio   = request.encodedBytes > 0
                                    ? static_cast<double>(result.decodedBytes) / static_cast<double>(request.encodedBytes)
                                    : 0.0;
    result.recommendStreaming = result.decodedBytes > request.staticMemoryBudget || result.duration > 10.0;
    result.channels.resize(static_cast<std::size_t>(pcm.channels));
    std::vector<long double> squares(result.channels.size(), 0.0L);
    std::vector<long double> sums(result.channels.size(), 0.0L);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (int channel = 0; channel < pcm.channels; ++channel) {
            const float sample =
                pcm.samples[frame * static_cast<std::size_t>(pcm.channels) + static_cast<std::size_t>(channel)];
            if (!std::isfinite(sample)) {
                result.status = EditorStatus::Rejected;
                result.diagnostics.push_back(eve::editing::ruleDiagnostic(
                    eve::DiagnosticCode::InvalidArgument, RuleId("editor.audio.nonfinite-sample"),
                    DiagnosticSeverity::Error, "Decoded PCM contains a non-finite sample"));
                return result;
            }
            auto&        statistics = result.channels[static_cast<std::size_t>(channel)];
            const double absolute   = std::abs(static_cast<double>(sample));
            statistics.peak         = std::max(statistics.peak, absolute);
            if (absolute >= request.clippingThreshold) ++statistics.clippedSamples;
            sums[static_cast<std::size_t>(channel)] += sample;
            squares[static_cast<std::size_t>(channel)] += static_cast<long double>(sample) * sample;
        }
    }
    bool silent = true;
    for (std::size_t channel = 0; channel < result.channels.size(); ++channel) {
        auto& statistics    = result.channels[channel];
        statistics.dcOffset = static_cast<double>(sums[channel] / frames);
        statistics.rms      = std::sqrt(static_cast<double>(squares[channel] / frames));
        silent              = silent && statistics.peak <= request.silenceThreshold;
        if (statistics.clippedSamples > 0)
            result.diagnostics.push_back(eve::editing::ruleDiagnostic(
                eve::DiagnosticCode::PreconditionViolation, RuleId("editor.audio.clipping"),
                DiagnosticSeverity::Warning, "Audio channel " + std::to_string(channel) + " contains clipped samples"));
        if (std::abs(statistics.dcOffset) > 0.01)
            result.diagnostics.push_back(eve::editing::ruleDiagnostic(
                eve::DiagnosticCode::PreconditionViolation, RuleId("editor.audio.dc-offset"),
                DiagnosticSeverity::Warning,
                "Audio channel " + std::to_string(channel) + " has significant DC offset"));
    }
    if (silent)
        result.diagnostics.push_back(
            eve::editing::ruleDiagnostic(eve::DiagnosticCode::PreconditionViolation, RuleId("editor.audio.silence"),
                                         DiagnosticSeverity::Warning, "Decoded audio is effectively silent"));
    if (result.recommendStreaming && !request.streaming)
        result.diagnostics.push_back(eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::PreconditionViolation, RuleId("editor.audio.streaming-recommended"),
            DiagnosticSeverity::Warning, "Long or memory-heavy audio should use streaming playback"));
    if (!result.recommendStreaming && request.streaming)
        result.diagnostics.push_back(
            eve::editing::ruleDiagnostic(eve::DiagnosticCode::None, RuleId("editor.audio.static-recommended"),
                                         DiagnosticSeverity::Info, "Short audio can avoid streaming overhead"));
    result.status = EditorStatus::Applied;
    return result;
}

}  // namespace eve::audio_editing

// AudioPublishingTarget.cpp
namespace eve::audio_editing {
namespace {}  // namespace

AudioSourcePublishingTarget::AudioSourcePublishingTarget(std::string id, IAudioSourceRuntimeSink* sink)
    : document_(std::move(id)), sink_(sink) {}

TargetDescriptor AudioSourcePublishingTarget::describe() const {
    TargetDescriptor descriptor = document_.describe();
    descriptor.type             = "audio-source-runtime";
    return descriptor;
}

void* AudioSourcePublishingTarget::queryCapability(const CapabilityId& capability) {
    return document_.queryCapability(capability);
}

Result<void> AudioSourcePublishingTarget::applyDomainOperation(const DomainOperation& operation) {
    if (staging_) return document_.applyDomainOperation(operation);
    auto candidate = cloneDomainState();
    auto applied   = candidate->applyDomainOperation(operation);
    if (!applied.ok()) return applied;
    return commitDomainState(std::move(candidate));
}

std::unique_ptr<IDomainOperationTarget> AudioSourcePublishingTarget::cloneDomainState() const {
    auto candidate      = std::make_unique<AudioSourcePublishingTarget>(*this);
    candidate->staging_ = true;
    return candidate;
}

Result<void> AudioSourcePublishingTarget::commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* typed = dynamic_cast<AudioSourcePublishingTarget*>(candidate.get());
    if (!typed || typed->targetId() != targetId() || typed->sink_ != sink_ || !typed->staging_)
        return eve::editing::failed<void>(EditorStatus::Conflict, RuleId("editor.audio.publishing-candidate-mismatch"),
                                          "Audio publishing candidate belongs to another live target");
    if (!sink_)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.audio.publishing-sink-missing"),
                                          "Audio publishing target requires a live runtime sink");
    Result<void> published = sink_->publish(typed->document_);
    if (!published.ok()) return published;
    document_ = typed->document_;
    return eve::editing::applied<void>();
}

}  // namespace eve::audio_editing
