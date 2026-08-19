#pragma once

#include "NetTypes.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace eve::data {
class ByteData;
}

namespace eve::network {

class Network;

/**
 * @brief Asynchronous HTTP request (Poco-based). Configure headers/body, call
 * submit(), then receive the response through Network::pump as a
 * NetEvType::HttpResp completion.
 */
class HttpRequest {
public:
    /** @brief Creates a request; method is e.g. "GET"/"POST", url is absolute. */
    HttpRequest(Network* net, std::string method, std::string url);
    ~HttpRequest() = default;

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
    /** @brief Queues the request on the worker; true when accepted. */
    bool submit();

    /** @brief Owning module. */
    Network* network() const { return net_; }

private:
    Network* net_ = nullptr;
    std::string method_;
    std::string url_;
    std::map<std::string, std::string> headers_;
    std::shared_ptr<std::vector<char>> body_;
    int timeoutMs_ = -1;
    int verifySsl_ = -1;  // -1 = use module default
};

}  // namespace eve::network
