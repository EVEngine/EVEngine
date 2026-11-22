#pragma once

#include <Poco/Crypto/OpenSSLInitializer.h>
#include <future>
#include <map>
#include <mutex>
#include <unordered_map>
#include "network/NetTypes.h"

namespace eve::network {

// Module-private HTTP executor. Jobs own request snapshots, never their handles
// or Network. Registry cookies distinguish successive lifetimes at one address.
struct HttpPolicy {
    int         timeoutMs = 10000;
    bool        verifySsl = true;
    std::string caLocation;
};

struct HttpOptions {
    std::string                        method;
    std::string                        url;
    std::map<std::string, std::string> headers;
    std::shared_ptr<std::vector<char>> body;
    HttpPolicy                         policy;
};

struct HttpOrigin;

struct HttpResult {
    NetCompletion completion;
    size_t        sentBytes = 0;

private:
    friend class HttpRuntime;
    std::shared_ptr<HttpOrigin> origin_;
};

class HttpRuntime {
public:
    enum class SubmitStatus { Accepted, Closed, Unavailable };
    enum class PollStatus { Pending, Ready, Closed };
    enum class DeliveryStatus { Ready, Retired };

    /** @brief Initializes the module-owned executor and SSL lifetime. */
    HttpRuntime() = default;
    /** @brief Joins remaining jobs after canceling undelivered results. */
    ~HttpRuntime();
    /** @brief Copies the authoritative policy for one submission. */
    HttpPolicy policy() const;
    /** @brief Changes the default I/O timeout. */
    void setTimeout(int ms);
    /** @brief Returns the default I/O timeout. */
    int timeout() const;
    /** @brief Changes default certificate verification for subsequent jobs. */
    void setVerifySsl(bool verify);
    /** @brief Returns default certificate verification. */
    bool getVerifySsl() const;
    /** @brief Owns a custom CA path for subsequent jobs. */
    void setCaLocation(std::string location);
    /** @brief Reports whether the owning module has shut down. */
    bool isClosed() const;
    /** @brief Starts an owning snapshot without retaining the handle.
     * @ownership Origin is borrowed identity, canceled by its owner's destructor.
     * @lifetime The runtime joins all jobs before releasing its SSL initializer.
     * @cost Creates a thread and performs network I/O per submission.
     */
    [[nodiscard]] SubmitStatus submit(void* origin, HttpOptions options);
    /** @brief Takes only this origin's next ready result.
     * @ownership Origin is a borrowed identity; result transfers its owned payload.
     */
    [[nodiscard]] PollStatus take(void* origin, HttpResult& result);
    /** @brief Transfers ready, still-live results without invoking callbacks.
     * @cost Scans pending requests once per pump; completed payloads are moved.
     */
    void drain(std::vector<HttpResult>& out);
    /** @brief Rechecks lifetime immediately before queuing an event, after any reentrant callbacks. */
    [[nodiscard]] DeliveryStatus prepareDelivery(HttpResult& result);
    /** @brief Retires all jobs for the borrowed origin without blocking its destructor.
     * @ownership Origin is identity only and is never dereferenced.
     */
    void cancel(void* origin);
    /** @brief Cancels all origins, then joins jobs without holding the registry lock.
     * @cost Waits for in-flight I/O during module shutdown; never called per frame.
     */
    void close();

private:
    struct Job {
        std::shared_ptr<HttpOrigin> lifetime;
        std::future<HttpResult>     future;
    };

    // Initialize on construction before starting concurrent Context users.
    // Unlike uninitializeSSL(), this does not shut down the process SSLManager.
    Poco::Crypto::OpenSSLInitializer                                ssl_;
    mutable std::mutex                                              mu_;
    HttpPolicy                                                      policy_;
    bool                                                            closed_ = false;
    std::unordered_map<std::uintptr_t, std::shared_ptr<HttpOrigin>> origins_;
    std::vector<Job>                                                jobs_;
};

}  // namespace eve::network
