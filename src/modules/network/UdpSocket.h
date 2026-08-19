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
class DatagramSocket;
}

namespace eve::network {

class Network;

/** @brief UDP socket backed by Poco::Net; supports connect/bind and datagram send. */
class UdpSocket {
public:
    /** @brief Creates an unbound socket owned by the given module. */
    explicit UdpSocket(Network* net);
    ~UdpSocket();

    /** @brief Binds to a local port; true on success. */
    bool bind(uint16_t port);
    /** @brief Connects to a remote host:port (restricts send()). */
    bool connect(std::string host, uint16_t port);
    /** @brief Sends a datagram to an explicit host:port. */
    bool sendTo(eve::data::ByteData* data, std::string host, uint16_t port);
    /** @brief Sends a string datagram to an explicit host:port. */
    bool sendToString(std::string s, std::string host, uint16_t port);
    /** @brief Sends a datagram to the connected peer. */
    bool send(eve::data::ByteData* data);
    /** @brief Sends a string datagram to the connected peer. */
    bool sendString(std::string s);
    /** @brief Closes the socket. */
    void close();

    /** @brief Internal: underlying Poco datagram socket. */
    Poco::Net::DatagramSocket* datagram();
    /** @brief True after bind(). */
    bool isBound() const { return bound_; }
    /** @brief Owning module. */
    Network* network() const { return net_; }

private:
    Network* net_ = nullptr;
    std::unique_ptr<Poco::Net::DatagramSocket> sock_;
    bool bound_ = false;
    bool connected_ = false;
};

}  // namespace eve::network
