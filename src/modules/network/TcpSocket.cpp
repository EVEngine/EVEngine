#include "network/TcpSocket.h"
#include "network/Network.h"
#include "network/NetWorker.h"
#include "data/ByteData.h"

#include <Poco/Net/StreamSocket.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Net/NetException.h>
#include <Poco/Timespan.h>

namespace eve::network {

TcpSocket::TcpSocket(Network* net) : net_(net) {}

TcpSocket::~TcpSocket() {
    close();
}

void TcpSocket::setConnectedSocket(std::unique_ptr<Poco::Net::StreamSocket> sock) {
    stream_    = std::move(sock);
    connected_ = stream_ != nullptr;
    if (stream_) {
        try {
            peer_ = stream_->peerAddress().toString();
        } catch (...) {
            peer_.clear();
        }
    }
}

Poco::Net::StreamSocket* TcpSocket::stream() {
    return stream_.get();
}

Poco::Net::ServerSocket* TcpSocket::server() {
    return server_.get();
}

void TcpSocket::pushAccepted(std::unique_ptr<TcpSocket> sock) {
    std::lock_guard<std::mutex> lock(acceptMu_);
    accepted_.push_back(std::move(sock));
}

bool TcpSocket::takeAccepted(std::unique_ptr<TcpSocket>& out) {
    std::lock_guard<std::mutex> lock(acceptMu_);
    if (accepted_.empty()) return false;
    out = std::move(accepted_.front());
    accepted_.erase(accepted_.begin());
    return true;
}

bool TcpSocket::connect(std::string host, uint16_t port) {
    if (!net_ || !net_->worker()) return false;
    auto* self = this;
    net_->worker()->submit([self, host, port]() {
        NetCompletion c;
        c.kind   = NetKind::Tcp;
        c.handle = self;
        try {
            auto sock = std::make_unique<Poco::Net::StreamSocket>();
            sock->connect(Poco::Net::SocketAddress(host, port), Poco::Timespan(self->net_->getTimeout(), 0));
            sock->setBlocking(false);
            self->setConnectedSocket(std::move(sock));
            c.type   = NetEvType::Conn;
            c.reason = "ok";
            c.peer   = self->getPeer();
            self->net_->post(std::move(c));
            self->net_->watchTcp(self);
        } catch (const Poco::Net::ConnectionRefusedException&) {
            c.type   = NetEvType::Err;
            c.reason = "refused";
            self->net_->post(std::move(c));
            NetCompletion fail;
            fail.type   = NetEvType::Conn;
            fail.kind   = NetKind::Tcp;
            fail.handle = self;
            fail.reason = "fail";
            self->net_->post(std::move(fail));
        } catch (const Poco::Exception& ex) {
            c.type   = NetEvType::Err;
            std::string msg = ex.displayText();
            if (msg.find("timed") != std::string::npos || msg.find("Timeout") != std::string::npos)
                c.reason = "timeout";
            else if (msg.find("host") != std::string::npos || msg.find("DNS") != std::string::npos)
                c.reason = "dns";
            else
                c.reason = "refused";
            self->net_->post(std::move(c));
            NetCompletion fail;
            fail.type   = NetEvType::Conn;
            fail.kind   = NetKind::Tcp;
            fail.handle = self;
            fail.reason = "fail";
            self->net_->post(std::move(fail));
        }
    });
    return true;
}

bool TcpSocket::listen(uint16_t port) {
    try {
        server_ = std::make_unique<Poco::Net::ServerSocket>(port);
        server_->setBlocking(false);
        listening_ = true;
        if (net_) net_->watchTcp(this);
        return true;
    } catch (...) {
        listening_ = false;
        server_.reset();
        if (net_) {
            NetCompletion c;
            c.type   = NetEvType::Err;
            c.kind   = NetKind::Tcp;
            c.handle = this;
            c.reason = "refused";
            net_->post(std::move(c));
        }
        return false;
    }
}

TcpSocket* TcpSocket::accept() {
    std::unique_ptr<TcpSocket> sock;
    if (!takeAccepted(sock)) return nullptr;
    return sock.release();
}

bool TcpSocket::send(eve::data::ByteData* data) {
    if (!data || !stream_ || !connected_ || !net_ || !net_->worker()) return false;
    size_t n = data->getSize();
    if (sendQueued_ + n > kMaxSendBuffer) {
        NetCompletion c;
        c.type   = NetEvType::Err;
        c.kind   = NetKind::Tcp;
        c.handle = this;
        c.reason = "limit";
        net_->post(std::move(c));
        return false;
    }
    auto buf = std::make_shared<std::vector<char>>(
        static_cast<char*>(data->getData()),
        static_cast<char*>(data->getData()) + n);
    addSendQueued(n);
    auto* self = this;
    net_->worker()->submit([self, buf, n]() {
        try {
            if (!self->stream_) {
                self->subSendQueued(n);
                return;
            }
            int sent = self->stream_->sendBytes(buf->data(), static_cast<int>(buf->size()));
            (void)sent;
            self->subSendQueued(n);
        } catch (...) {
            self->subSendQueued(n);
            NetCompletion c;
            c.type   = NetEvType::Err;
            c.kind   = NetKind::Tcp;
            c.handle = self;
            c.reason = "closed";
            self->net_->post(std::move(c));
        }
    });
    return true;
}

bool TcpSocket::sendString(std::string s) {
    if (s.empty()) return false;
    eve::data::ByteData data(s.data(), s.size());
    return send(&data);
}

void TcpSocket::close() {
    if (net_) net_->unwatchTcp(this);
    connected_ = false;
    listening_ = false;
    try {
        if (stream_) stream_->close();
    } catch (...) {
    }
    stream_.reset();
    try {
        if (server_) server_->close();
    } catch (...) {
    }
    server_.reset();
}

bool TcpSocket::isConnected() const {
    return connected_ && stream_ != nullptr;
}

std::string TcpSocket::getPeer() const {
    return peer_;
}

uint16_t TcpSocket::getLocalPort() const {
    try {
        if (server_) return static_cast<uint16_t>(server_->address().port());
        if (stream_) return static_cast<uint16_t>(stream_->address().port());
    } catch (...) {
    }
    return 0;
}

}  // namespace eve::network
