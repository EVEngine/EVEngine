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

class UdpSocket {
public:
    explicit UdpSocket(Network* net);
    ~UdpSocket();

    bool bind(uint16_t port);
    bool connect(std::string host, uint16_t port);
    bool sendTo(eve::data::ByteData* data, std::string host, uint16_t port);
    bool sendToString(std::string s, std::string host, uint16_t port);
    bool send(eve::data::ByteData* data);
    bool sendString(std::string s);
    void close();

    Poco::Net::DatagramSocket* datagram();
    bool isBound() const { return bound_; }
    Network* network() const { return net_; }

private:
    Network* net_ = nullptr;
    std::unique_ptr<Poco::Net::DatagramSocket> sock_;
    bool bound_ = false;
    bool connected_ = false;
};

}  // namespace eve::network
