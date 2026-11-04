#pragma once
#include "common/Export.h"


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

/** @brief UDP socket backed by Poco::Net; supports connect/bind and datagram send.
 *  @ownership The caller owns the socket; Network must outlive it.
 *  @thread Public operations run on the owner thread; I/O runs on NetWorker.
 *  @lifetime Closing waits for active sends/polling and cancels queued sends.
 */
class EVENGINE_API_PLATFORM UdpSocket {
public:
    /** @brief Creates an unbound socket owned by the given module. */
    explicit UdpSocket(Network* net);
    /** @brief Udp socket. */
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
    struct SendState;
    std::shared_ptr<SendState>                 sendState_;
    Network* net_ = nullptr;
    std::unique_ptr<Poco::Net::DatagramSocket> sock_;
    bool bound_ = false;
    bool connected_ = false;
};

}  // namespace eve::network
