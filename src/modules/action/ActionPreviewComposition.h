#pragma once
#include "common/Export.h"

/** @file ActionPreviewComposition.h
 *  @brief Host helper that composes IActionPreviewSinkProvider listeners.
 *
 *  Keeps editor/script hosts from embedding multi-sink prepare/present logic.
 *  ActionPreviewController still owns transport; this only builds the sink tree.
 */

#include "action/ActionPreview.h"

#include <memory>
#include <vector>

namespace eve::action {

/**
 * @brief Fan-out sink that prepares/presents every child, rolling back on prepare failure.
 */
class EVENGINE_API_PLATFORM ComposedActionPreviewSink final : public IActionPreviewSink {
public:
    ComposedActionPreviewSink() = default;
    ~ComposedActionPreviewSink() override = default;

    ComposedActionPreviewSink(const ComposedActionPreviewSink&)            = delete;
    ComposedActionPreviewSink& operator=(const ComposedActionPreviewSink&) = delete;
    ComposedActionPreviewSink(ComposedActionPreviewSink&&)                 = default;
    ComposedActionPreviewSink& operator=(ComposedActionPreviewSink&&)      = default;

    /** @brief Append one owning child sink. */
    void add(std::unique_ptr<IActionPreviewSink> sink);
    /** @brief True when no children were installed. */
    [[nodiscard]] bool empty() const noexcept;

    [[nodiscard]] Result<void> prepare(const ActionPreviewFrame& frame) override;
    void                       present(const ActionPreviewFrame& frame) noexcept override;
    void                       discardPrepared() noexcept override;

private:
    std::vector<std::unique_ptr<IActionPreviewSink>> sinks_;
};

/**
 * @brief Build a composed sink from every registered IActionPreviewSinkProvider.
 * @return Empty unique_ptr when no providers are present; otherwise an owning composed sink.
 * @remarks Provider creation failure returns a structured Result failure and publishes no sink.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<std::unique_ptr<IActionPreviewSink>> composeActionPreviewSinksFromProviders();

}  // namespace eve::action
