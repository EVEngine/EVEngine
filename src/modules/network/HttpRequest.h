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

class HttpRequest {
public:
    HttpRequest(Network* net, std::string method, std::string url);
    ~HttpRequest() = default;

    void setHeader(std::string k, std::string v);
    void setBody(eve::data::ByteData* data);
    void setBodyString(std::string s);
    void setTimeout(int ms);
    void setVerifySsl(bool verify);
    bool submit();

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
