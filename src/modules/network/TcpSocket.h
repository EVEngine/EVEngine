#pragma once

#include "NetTypes.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace eve::data {
class ByteData;
}

namespace Poco::Net {
class StreamSocket;
class ServerSocket;
}

namespace eve::network {

class Network;

class TcpSocket {
public:
    explicit TcpSocket(Network* net);
    ~TcpSocket();

    bool connect(std::string host, uint16_t port);
    bool listen(uint16_t port);
    TcpSocket* accept();
    bool send(eve::data::ByteData* data);
    bool sendString(std::string s);
    void close();
    bool isConnected() const;
    std::string getPeer() const;
    uint16_t getLocalPort() const;

    // Internal: used by Network::pump / NetWorker
    Network* network() const { return net_; }
    void setConnectedSocket(std::unique_ptr<Poco::Net::StreamSocket> sock);
    Poco::Net::StreamSocket* stream();
    Poco::Net::ServerSocket* server();
    bool isListening() const { return listening_; }
    bool takeAccepted(std::unique_ptr<TcpSocket>& out);
    void pushAccepted(std::unique_ptr<TcpSocket> sock);
    size_t pendingSendBytes() const { return sendQueued_; }
    void addSendQueued(size_t n) { sendQueued_ += n; }
    void subSendQueued(size_t n) { sendQueued_ = sendQueued_ > n ? sendQueued_ - n : 0; }

private:
    Network* net_ = nullptr;
    std::unique_ptr<Poco::Net::StreamSocket> stream_;
    std::unique_ptr<Poco::Net::ServerSocket> server_;
    bool listening_ = false;
    bool connected_ = false;
    size_t sendQueued_ = 0;
    std::mutex acceptMu_;
    std::vector<std::unique_ptr<TcpSocket>> accepted_;
    std::string peer_;
};

}  // namespace eve::network
