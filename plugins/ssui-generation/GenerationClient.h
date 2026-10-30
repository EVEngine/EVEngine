#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ssui {
/** @brief Native asynchronous SSUI v1 client and single-writer material document. */
class GenerationClient {
public:
    /** @brief Stops and joins outstanding network work. */
    ~GenerationClient();
    /** @brief Configures an HTTP service and local project directory; restores material history. */
    void configure(const std::string& server, const std::string& project);
    /** @brief Starts a versioned recipe with JSON inputs/context. Returns false when busy. */
    bool start(const std::string& recipe, const std::string& inputs, const std::string& context);
    /** @brief Requests server cancellation and discards any late candidate. */
    void cancel();
    /** @brief Requests cancellation and waits for bounded network operations to finish. */
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
    /** @brief Commits candidate after revision validation; preserves undo history. */
    std::string apply();
    /** @brief Restores previous committed material and increments revision. */
    std::string undo();
private:
    void run(std::string recipe, std::string inputs, std::string context, int revision);
    std::string request(const std::string& method, const std::string& path, const std::string& body = "", size_t limit = 1024*1024);
    void save(const std::string& path, const std::vector<std::string>& history, int revision);
    void checkRevision() const;
    mutable std::mutex mutex_;
    std::thread worker_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> busy_{false};
    std::string server_, root_, clientId_, status_ = "Not configured", candidate_, applied_;
    int progress_ = 0, revision_ = 0, candidateRevision_ = 0;
    std::vector<std::string> history_;
};
}
