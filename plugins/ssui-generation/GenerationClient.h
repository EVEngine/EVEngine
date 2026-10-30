#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include "common/Result.h"

namespace ssui {
/** @brief Native asynchronous SSUI v1 client and single-writer material document.
 * @ownership Owns its worker and all returned snapshots; inputs are copied.
 * @thread Commands and destruction on the main thread only; queries use synchronized copies.
 * @reentrancy No callbacks or scripts are invoked. Destruction cancels and joins.
 * @note Configure restores atomically. Generation prepares a candidate; only apply publishes it.
 */
class GenerationClient {
public:
    /** @brief Stops and joins outstanding network work. */
    ~GenerationClient();
    /** @brief Configures an HTTP service and local project directory; restores material history.
     * @cost Reads one bounded material document per connection. */
    [[nodiscard]] eve::Result<void> configure(std::string_view server, std::string_view project);
    /** @brief Starts a versioned recipe with JSON inputs/context. Returns an owning request ID, or Conflict when busy.
     * @cost Copies JSON and starts one worker per submission; HTTP work is asynchronous. */
    [[nodiscard]] eve::Result<std::string> start(std::string_view recipe, std::string_view inputs,
                                                 std::string_view context);
    /** @brief Fire-and-forget cancellation signal; immediately suppresses late candidates.
     * Remote cancellation is best-effort; completion() reports Cancelled locally. */
    void requestCancel();
    /** @brief Cancels and joins; idempotent cleanup, never commits a document.
     * @cost May wait for an in-flight socket operation (five-second timeout). */
    void shutdown();
    /** @brief True while background work is active. */
    bool busy() const;
    /** @brief Current human-readable state or error. */
    std::string status() const;
    /** @brief Progress from 0 to 100. */
    int progress() const;
    /** @brief Project-relative candidate PNG; empty if unavailable. */
    std::string candidate() const;
    /** @brief Project-relative committed PNG; empty when unset. */
    std::string applied() const;
    /** @brief Commits candidate after revision validation; preserves undo history.
     * @cost Writes the bounded history once per explicit commit. */
    [[nodiscard]] eve::Result<std::string> apply();
    /** @brief Restores previous committed material and increments revision.
     * @cost Writes the bounded history once per explicit undo. */
    [[nodiscard]] eve::Result<std::string> undo();
    /** @brief Returns the asynchronous completion as an owning candidate path.
     * @return Conflict while pending, Cancelled after cancellation, or the worker diagnostic.
     * @note Query on the main thread; does not consume the candidate or mutate the document. */
    [[nodiscard]] eve::Result<std::string> completion() const;

private:
    void        configureImpl(const std::string& server, const std::string& project);
    std::string startImpl(const std::string& recipe, const std::string& inputs, const std::string& context);
    std::string applyImpl();
    std::string undoImpl();
    eve::Status completionStatus_ = eve::Status::success();
    void        run(std::string recipe, std::string inputs, std::string context, int revision, std::string requestId);
    std::string request(const std::string& method, const std::string& path, const std::string& body = "",
                        size_t limit = 1024 * 1024);
    void        save(const std::string& path, const std::vector<std::string>& history, int revision);
    void        checkRevision() const;
    mutable std::mutex       mutex_;
    std::thread              worker_;
    std::atomic<bool>        stop_{false};
    std::atomic<bool>        busy_{false};
    std::string              server_, root_, clientId_, status_ = "Not configured", candidate_, applied_;
    int                      progress_ = 0, revision_ = 0, candidateRevision_ = 0;
    std::vector<std::string> history_;
};
}  // namespace ssui
