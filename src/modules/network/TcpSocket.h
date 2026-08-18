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

/**
 * @brief TCP socket backed by Poco::Net; supports both client (connect) and
 * server (listen/accept) roles. Data arrives via Network::pump as NetCompletion.
 */
class TcpSocket {
public:
    /** @brief Creates an unconnected socket owned by the given module. */
    explicit TcpSocket(Network* net);
    ~TcpSocket();

    /** @brief Connects to host:port; true on success. */
    bool connect(std::string host, uint16_t port);
    /** @brief Starts listening on the given port; true on success. */
    bool listen(uint16_t port);
    /** @brief Accepts one pending client, or nullptr if none. */
    TcpSocket* accept();
    /** @brief Sends a framed byte payload; true when queued/accepted. */
    bool send(eve::data::ByteData* data);
    /** @brief Sends a string payload. */
    bool sendString(std::string s);
    /** @brief Closes the socket. */
    void close();
    /** @brief True while a stream is connected. */
    bool isConnected() const;
    /** @brief Remote peer address string, e.g. "1.2.3.4:5678". */
    std::string getPeer() const;
    /** @brief Local listening port, or 0. */
    uint16_t getLocalPort() const;

    /** @brief Internal: owning module (used by Network::pump / NetWorker). */
    Network* network() const { return net_; }
    void setConnectedSocket(std::unique_ptr<Poco::Net::StreamSocket> sock);
    Poco::Net::StreamSocket* stream();
    Poco::Net::ServerSocket* server();
    /** @brief True after listen(). */
    bool isListening() const { return listening_; }
    bool takeAccepted(std::unique_ptr<TcpSocket>& out);
    void pushAccepted(std::unique_ptr<TcpSocket> sock);
    /** @brief Internal: queued-outgoing byte counter for back-pressure. */
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
