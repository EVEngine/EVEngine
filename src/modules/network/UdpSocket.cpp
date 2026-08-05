#include "network/UdpSocket.h"
#include "network/Network.h"
#include "network/NetWorker.h"
#include "data/ByteData.h"

#include <Poco/Net/DatagramSocket.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Exception.h>

namespace eve::network {

UdpSocket::UdpSocket(Network* net) : net_(net) {}

UdpSocket::~UdpSocket() {
    close();
}

Poco::Net::DatagramSocket* UdpSocket::datagram() {
    return sock_.get();
}

bool UdpSocket::bind(uint16_t port) {
    try {
        sock_ = std::make_unique<Poco::Net::DatagramSocket>();
        sock_->bind(Poco::Net::SocketAddress(port), true);
        sock_->setBlocking(false);
        bound_ = true;
        if (net_) net_->watchUdp(this);
        return true;
    } catch (...) {
        sock_.reset();
        bound_ = false;
        if (net_) {
            NetCompletion c;
            c.type   = NetEvType::Err;
            c.kind   = NetKind::Udp;
            c.handle = this;
            c.reason = "refused";
            net_->post(std::move(c));
        }
        return false;
    }
}

bool UdpSocket::connect(std::string host, uint16_t port) {
    try {
        if (!sock_) sock_ = std::make_unique<Poco::Net::DatagramSocket>();
        sock_->connect(Poco::Net::SocketAddress(host, port));
        sock_->setBlocking(false);
        connected_ = true;
        if (net_) net_->watchUdp(this);
        return true;
    } catch (...) {
        if (net_) {
            NetCompletion c;
            c.type   = NetEvType::Err;
            c.kind   = NetKind::Udp;
            c.handle = this;
            c.reason = "refused";
            net_->post(std::move(c));
        }
        return false;
    }
}

bool UdpSocket::sendTo(eve::data::ByteData* data, std::string host, uint16_t port) {
    if (!data || !sock_ || !net_ || !net_->worker()) return false;
    size_t n = data->getSize();
    auto buf = std::make_shared<std::vector<char>>(
        static_cast<char*>(data->getData()),
        static_cast<char*>(data->getData()) + n);
    auto* self = this;
    net_->worker()->submit([self, buf, host, port]() {
        try {
            if (!self->sock_) return;
            self->sock_->sendTo(buf->data(), static_cast<int>(buf->size()),
                                Poco::Net::SocketAddress(host, port));
        } catch (...) {
            NetCompletion c;
            c.type   = NetEvType::Err;
            c.kind   = NetKind::Udp;
            c.handle = self;
            c.reason = "closed";
            self->net_->post(std::move(c));
        }
    });
    return true;
}

bool UdpSocket::sendToString(std::string s, std::string host, uint16_t port) {
    if (s.empty()) return false;
    eve::data::ByteData data(s.data(), s.size());
    return sendTo(&data, host, port);
}

bool UdpSocket::send(eve::data::ByteData* data) {
    if (!data || !sock_ || !connected_ || !net_ || !net_->worker()) return false;
    size_t n = data->getSize();
    auto buf = std::make_shared<std::vector<char>>(
        static_cast<char*>(data->getData()),
        static_cast<char*>(data->getData()) + n);
    auto* self = this;
    net_->worker()->submit([self, buf]() {
        try {
            if (!self->sock_) return;
            self->sock_->sendBytes(buf->data(), static_cast<int>(buf->size()));
        } catch (...) {
            NetCompletion c;
            c.type   = NetEvType::Err;
            c.kind   = NetKind::Udp;
            c.handle = self;
            c.reason = "closed";
            self->net_->post(std::move(c));
        }
    });
    return true;
}

bool UdpSocket::sendString(std::string s) {
    if (s.empty()) return false;
    eve::data::ByteData data(s.data(), s.size());
    return send(&data);
}

void UdpSocket::close() {
    if (net_) net_->unwatchUdp(this);
    bound_ = false;
    connected_ = false;
    try {
        if (sock_) sock_->close();
    } catch (...) {
    }
    sock_.reset();
}

}  // namespace eve::network
