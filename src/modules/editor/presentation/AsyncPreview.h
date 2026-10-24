#pragma once

#include "editing/EditingTaskService.h"
#include "editor/presentation/PreviewState.h"

#include <exception>
#include <limits>
#include <optional>

namespace eve::editor {

/**
 * @brief Typed latest-request preview publication using the existing editing task executor.
 * @ownership Worker owns immutable input captures and a private completion mailbox, never a domain pointer.
 * @thread All methods are owner-thread only. Work runs on TaskService's worker and must cooperate with cancellation.
 * @reentrancy No work or unknown callback executes under a publication lock.
 * @lifetime Close invalidates publication immediately; destruction cancels and joins bounded/cooperative work.
 */
template <class Payload>
class AsyncPreview {
public:
    using Work = std::function<editing::Result<Payload>(const editing::TaskContext&)>;
    /** @brief Start a private task executor; no domain state is captured. */
    AsyncPreview() = default;
    /** @brief Cancel outstanding work before the executor joins. */
    ~AsyncPreview() { close(); }
    AsyncPreview(const AsyncPreview&)            = delete;
    AsyncPreview& operator=(const AsyncPreview&) = delete;

    /**
     * @brief Queue owning work, cancelling the previous request. Returns Pending after acceptance.
     * @cost Queues one background computation; capture size is paid once per invalidation, not per poll.
     */
    [[nodiscard]] editing::Result<void> request(Work work) {
        if (state_.phase == PreviewPhase::Closed || state_.phase == PreviewPhase::Unavailable)
            return editing::failed<void>(editing::Status::Unsupported, editing::RuleId("editor.preview.unavailable"),
                                         "Preview session is closed or has no provider");
        if (!work || state_.request == std::numeric_limits<std::uint64_t>::max())
            return editing::rejected<void>(editing::RuleId("editor.preview.request"),
                                           "Invalid preview request or exhausted sequence");
        auto cancelled = cancel();
        if (!cancelled.ok()) return cancelled;
        ++state_.request;
        auto mailbox   = std::make_shared<Mailbox>();
        auto submitted = tasks_.submit("editor.preview", [mailbox,
                                                          work = std::move(work)](const editing::TaskContext& context) {
            auto invoke = [&]() -> editing::Result<Payload> {
                try {
                    return work(context);
                } catch (const std::exception& error) {
                    return editing::failed<Payload>(editing::Status::Failed,
                                                    editing::RuleId("editor.preview.exception"), error.what());
                } catch (...) {
                    return editing::failed<Payload>(editing::Status::Failed,
                                                    editing::RuleId("editor.preview.exception"),
                                                    "Unknown preview provider exception");
                }
            };
            auto                 result = invoke();
            editing::TaskOutcome outcome;
            outcome.status      = result.status().code();
            outcome.diagnostics = result.status().diagnostics();
            if (!context.isCancellationRequested()) {
                std::lock_guard lock(mailbox->mutex);
                mailbox->result.emplace(std::move(result));
            } else {
                result.ignore("Cancelled preview has no publication consumer; the previous domain preview is retained");
            }
            return outcome;
        });
        if (!submitted.ok()) {
            state_.phase = PreviewPhase::Failed;
            return editing::Result<void>::failure(submitted.status());
        }
        task_        = std::move(submitted).takeValue();
        mailbox_     = std::move(mailbox);
        state_.phase = PreviewPhase::Pending;
        return editing::pending();
    }

    /** @brief Consume only the current request's completion, once; failed providers preserve the domain payload. */
    [[nodiscard]] std::optional<editing::Result<Payload>> poll() {
        if (!mailbox_) return {};
        std::optional<editing::Result<Payload>> result;
        {
            std::lock_guard lock(mailbox_->mutex);
            if (mailbox_->result) result.emplace(std::move(*mailbox_->result));
        }
        if (!result) return {};
        auto discarded = tasks_.discard(task_);
        if (!discarded.ok()) return editing::Result<Payload>::failure(discarded.status());
        task_ = {};
        mailbox_.reset();
        state_.phase = result->ok() ? PreviewPhase::Ready : PreviewPhase::Failed;
        return result;
    }
    /** @brief Cancel queued/running work and discard any completed but unpublished result; keeps the last domain
     * preview. */
    [[nodiscard]] editing::Result<void> cancel() {
        if (!task_.empty()) {
            auto discarded = tasks_.discard(task_);
            if (!discarded.ok()) return discarded;
            task_ = {};
        }
        mailbox_.reset();
        if (state_.phase != PreviewPhase::Closed && state_.phase != PreviewPhase::Unavailable)
            state_.phase = PreviewPhase::Cancelled;
        return editing::applied<void>();
    }
    /** @brief Invalidate all old provider work, including replacement with the same provider instance or absence. */
    [[nodiscard]] editing::Result<void> replaceProvider(bool available) {
        if (state_.phase == PreviewPhase::Closed ||
            state_.providerGeneration == std::numeric_limits<std::uint64_t>::max())
            return editing::rejected<void>(editing::RuleId("editor.preview.provider"),
                                           "Closed session or exhausted provider generation");
        auto cancelled = cancel();
        if (!cancelled.ok()) return cancelled;
        ++state_.providerGeneration;
        state_.phase = available ? PreviewPhase::Idle : PreviewPhase::Unavailable;
        return editing::applied<void>();
    }
    /** @brief Permanently close this session; cancellation is observable immediately and closure is idempotent. */
    void close() {
        auto cancelled = cancel();
        if (!cancelled.ok()) EV_ASSERT(false, "Owned preview task must be discardable");
        state_.phase = PreviewPhase::Closed;
    }
    /** @brief Record a domain validation failure after consuming a provider result; the domain keeps its previous
     * payload. */
    void rejectPublication() noexcept { state_.phase = PreviewPhase::Failed; }
    /** @brief Read lifecycle state without copying the domain payload or querying any registry. */
    PreviewState state() const noexcept { return state_; }

private:
    struct Mailbox {
        ~Mailbox() {
            if (result) result->ignore("Superseded or closed preview completion is intentionally not published");
        }
        std::mutex                              mutex;
        std::optional<editing::Result<Payload>> result;
    };
    editing::TaskService     tasks_;
    editing::TaskId          task_;
    std::shared_ptr<Mailbox> mailbox_;
    PreviewState             state_;
};
}  // namespace eve::editor
