#include "action/ActionPreviewComposition.h"

#include "common/Capability.h"

namespace eve::action {

void ComposedActionPreviewSink::add(std::unique_ptr<IActionPreviewSink> sink) { sinks_.push_back(std::move(sink)); }

bool ComposedActionPreviewSink::empty() const noexcept { return sinks_.empty(); }

Result<void> ComposedActionPreviewSink::prepare(const ActionPreviewFrame& frame) {
    std::size_t prepared = 0;
    for (; prepared < sinks_.size(); ++prepared) {
        auto result = sinks_[prepared]->prepare(frame);
        if (result) continue;
        for (std::size_t index = 0; index <= prepared; ++index) sinks_[index]->discardPrepared();
        return Result<void>::failure(result.status());
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

void ComposedActionPreviewSink::present(const ActionPreviewFrame& frame) noexcept {
    for (auto& sink : sinks_) sink->present(frame);
}

void ComposedActionPreviewSink::discardPrepared() noexcept {
    for (auto& sink : sinks_) sink->discardPrepared();
}

Result<std::unique_ptr<IActionPreviewSink>> composeActionPreviewSinksFromProviders() {
    auto       composed      = std::make_unique<ComposedActionPreviewSink>();
    const auto providerCount = cap::listenerCount<IActionPreviewSinkProvider>();
    for (std::size_t index = 0; index < providerCount; ++index) {
        auto* provider = cap::listenerAt<IActionPreviewSinkProvider>(index);
        if (!provider) continue;
        auto sink = provider->createActionPreviewSink();
        if (!sink) return Result<std::unique_ptr<IActionPreviewSink>>::failure(sink.status());
        composed->add(std::move(sink).takeValue());
    }
    if (composed->empty()) return Result<std::unique_ptr<IActionPreviewSink>>::success(nullptr);
    return Result<std::unique_ptr<IActionPreviewSink>>::success(std::move(composed));
}

}  // namespace eve::action
