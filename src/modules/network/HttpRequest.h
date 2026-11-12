#pragma once
#include "common/Export.h"


#include "NetTypes.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace eve::data {
class ByteData;
}

namespace eve::network {

class Network;
class HttpRuntime;

/**
 * @brief Asynchronous HTTP request (Poco-based). Configure headers/body, call
 * submit(), then receive the response through Network::pump as a
 * NetEvType::HttpResp completion.
 * @ownership Owns its configuration and body; borrows its Network.
 * @lifetime Destruction cancels results not yet pumped. Keep the handle alive
 * until consuming events already queued by pump(); copies copy configuration only.
 * @thread Configure, submit and destroy on the Network owner thread.
 * @reentrancy No user callbacks run from the HTTP executor.
 */
class EVENGINE_API_PLATFORM HttpRequest {
public:
    /** @brief Creates a request; method is e.g. "GET"/"POST", url is absolute. */
    HttpRequest(Network* net, std::string method, std::string url);
    /** @brief Drops undelivered results; in-flight I/O finishes under its timeout. */
    ~HttpRequest();
    /** @brief Copies configuration and the borrowed module, without copying jobs. */
    HttpRequest(const HttpRequest& other);
    /** @brief Cancels this handle's jobs before copying the other configuration. */
    HttpRequest& operator=(const HttpRequest& other);

    /** @brief Sets one request header. */
    void setHeader(std::string k, std::string v);
    /** @brief Sets the request body bytes. */
    void setBody(eve::data::ByteData* data);
    /** @brief Sets the request body as a string. */
    void setBodyString(std::string s);
    /** @brief Overrides the module default timeout for this request (ms). */
    void setTimeout(int ms);
    /** @brief Overrides TLS verification for this request. */
    void setVerifySsl(bool verify);
    /** @brief Overrides the CA file or hashed CA directory with an owning UTF-8 path.
     * Empty selects system/default trust; an unset override inherits the module.
     * The custom trust source is exclusive. Applied to subsequent submissions.
     * @cost Copies a path per configuration change; certificate loading occurs on submit's job.
     */
    void setCaLocation(std::string location);
    /** @brief Queues an immutable request snapshot; true reports acceptance only.
     * @cost Starts a thread and performs network I/O per submission; final results arrive through pump().
     */
    bool submit();

    /** @brief Returns the live module, or null after module destruction.
     * @ownership Borrowed; the Network owner retains ownership.
     * @lifetime Owner thread only; valid until the module is destroyed.
     */
    Network* network() const;

private:
    Network* net_ = nullptr;
    std::weak_ptr<HttpRuntime>         runtime_;
    std::string method_;
    std::string url_;
    std::map<std::string, std::string> headers_;
    std::shared_ptr<std::vector<char>> body_;
    int timeoutMs_ = -1;
    int verifySsl_ = -1;  // -1 = use module default
    std::optional<std::string>         caLocation_;
};

}  // namespace eve::network
