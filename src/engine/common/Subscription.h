#pragma once

#include "common/Export.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace eve {

/**
 * @brief Move-only RAII token that owns one observer registration.
 *
 * Destroying or explicitly disposing the token cancels the registration. The
 * cancellation operation is idempotent, and a moved-from token is inactive.
 * The cancellation callback must be non-throwing; `dispose()` is `noexcept`
 * and is therefore suitable for destructor use.
 *
 * A token belongs to the thread-affine observer that created it. It must be
 * disposed or destroyed on that observer's owner thread unless the observer
 * explicitly documents a stronger cross-thread guarantee.
 */
class EVENGINE_API Subscription {
public:
    using Cancel = std::function<void()>;

    /** @brief Creates an inactive token. */
    Subscription() noexcept = default;

    /**
     * @brief Creates a token from a cancellation callback.
     * @param cancel Non-throwing callback invoked at most once on disposal.
     */
    explicit Subscription(Cancel cancel);

    Subscription(const Subscription &)            = delete;
    Subscription &operator=(const Subscription &) = delete;
    Subscription(Subscription &&other) noexcept;
    Subscription &operator=(Subscription &&other) noexcept;
    ~Subscription();

    /** @brief Cancels this registration; safe to call repeatedly. */
    void dispose() noexcept;

    /** @brief Returns whether this token is inactive or has already been disposed. */
    bool disposed() const noexcept { return disposed_; }

    /** @brief Compatibility spelling for disposed(). */
    bool isDisposed() const noexcept { return disposed(); }

private:
    Cancel cancel_;
    bool   disposed_ = true;
};

/**
 * @brief Thread-affine callback registry with mutation-safe dispatch.
 *
 * `Observer` is intentionally a small lifecycle primitive, not an event bus
 * or a synchronization primitive. All methods, including destruction of the
 * returned Subscription, must be called from the owner thread. `notify()`
 * invokes callbacks synchronously on its caller and holds no lock while an
 * unknown callback runs.
 *
 * Dispatch is reentrant. A callback may subscribe or dispose any token,
 * including its own. Registrations added during a dispatch are considered on
 * the next dispatch; registrations disposed before their turn are skipped.
 * The callback currently running is allowed to finish. Recursive notify calls
 * are supported, but callers must bound recursion themselves.
 *
 * @tparam Args Callback argument types, passed as const references.
 */
template <typename... Args>
class Observer {
public:
    using Callback = std::function<void(const Args &...)>;

    Observer() : state_(std::make_shared<State>()) {}
    Observer(const Observer &)            = delete;
    Observer &operator=(const Observer &) = delete;
    Observer(Observer &&)                 = delete;
    Observer &operator=(Observer &&)      = delete;
    ~Observer()                           = default;

    /**
     * @brief Registers a callback owned by the returned Subscription.
     * @param callback Callback invoked by notify; an empty callback is ignored.
     * @return Move-only token that cancels this registration when disposed.
     * @warning Call this method and dispose the returned token on the owner thread.
     */
    [[nodiscard("retain Subscription or explicitly dispose it")]] Subscription subscribe(Callback callback) {
        compactInactive();
        if (!callback) return {};

        auto entry      = std::make_shared<Entry>();
        entry->id       = state_->nextId++;
        entry->callback = std::move(callback);
        state_->entries.push_back(entry);

        const std::weak_ptr<State> weakState = state_;
        return Subscription([weakState, entry]() noexcept {
            entry->active = false;
            if (const auto state = weakState.lock()) {
                std::erase_if(state->entries,
                              [&entry](const std::shared_ptr<Entry> &candidate) { return candidate == entry; });
            }
        });
    }

    /**
     * @brief Synchronously notifies a snapshot of current registrations.
     * @param args Values passed to each active callback as borrowed references.
     * @warning The arguments must remain valid for the duration of this call.
     */
    void notify(const Args &...args) {
        const auto snapshot = state_->entries;
        for (const auto &entry : snapshot) {
            if (!entry->active || !entry->callback) continue;
            // Copy outside any registry lock (the registry deliberately has no
            // lock) so callback destruction/re-entrancy cannot invalidate the
            // callable being invoked.
            Callback callback = entry->callback;
            if (entry->active && callback) callback(args...);
        }
    }

    /**
     * @brief Notifies all snapshotted callbacks and contains unknown exceptions.
     * @param onFailure Non-throwing handler called once per callback exception.
     * @param args Values passed to each active callback as borrowed references.
     * @return Number of callbacks that threw; committed caller state is not
     *         rolled back or reported as an operation failure.
     * @remarks Registrations added or disposed during dispatch follow the same
     *          next-dispatch rules as notify().
     */
    template <typename FailureHandler>
    [[nodiscard]] std::size_t notifyChecked(FailureHandler &&onFailure, const Args &...args) {
        const auto  snapshot = state_->entries;
        std::size_t failures = 0;
        for (const auto &entry : snapshot) {
            if (!entry->active || !entry->callback) continue;
            Callback callback = entry->callback;
            if (!entry->active || !callback) continue;
            try {
                callback(args...);
            } catch (...) {
                ++failures;
                std::forward<FailureHandler>(onFailure)();
            }
        }
        return failures;
    }

    /** @brief Returns the number of currently active registrations. */
    std::size_t size() const noexcept {
        std::size_t count = 0;
        for (const auto &entry : state_->entries)
            if (entry->active) ++count;
        return count;
    }

    /** @brief Removes inactive entries without invoking user callbacks. */
    void compactInactive() {
        std::erase_if(state_->entries, [](const std::shared_ptr<Entry> &entry) { return !entry->active; });
    }

private:
    struct Entry {
        std::uint64_t id = 0;
        Callback      callback;
        bool          active = true;
    };

    struct State {
        std::uint64_t                       nextId = 1;
        std::vector<std::shared_ptr<Entry>> entries;
    };

    std::shared_ptr<State> state_;
};

}  // namespace eve
