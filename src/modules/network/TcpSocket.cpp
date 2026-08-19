#include "network/TcpSocket.h"
#include "network/Network.h"
#include "network/NetWorker.h"
#include "data/ByteData.h"

#include <Poco/Net/StreamSocket.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Net/NetException.h>
#include <Poco/Net/SocketDefs.h>
#include <Poco/Timespan.h>

#include <cstring>

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
            int timeoutMs = self->net_->getTimeout();
            sock->connect(Poco::Net::SocketAddress(host, port),
                          Poco::Timespan(timeoutMs / 1000, (timeoutMs % 1000) * 1000));
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
    if (!data || !net_) return false;
    size_t n = data->getSize();
    return queueSend(data->getData(), n);
}

bool TcpSocket::sendString(std::string s) {
    if (s.empty()) return false;
    eve::data::ByteData data(s.data(), s.size());
    return send(&data);
}

bool TcpSocket::queueSend(const void* d, size_t n) {
    if (!d || n == 0) return false;
    const char* p = static_cast<const char*>(d);
    bool overLimit = false;
    {
        std::lock_guard<std::mutex> lock(sendMu_);
        overLimit = pendingSend_.size() + n > kMaxSendBuffer;
        if (!overLimit) pendingSend_.insert(pendingSend_.end(), p, p + n);
    }
    if (overLimit) {
        NetCompletion c;
        c.type   = NetEvType::Err;
        c.kind   = NetKind::Tcp;
        c.handle = this;
        c.reason = "limit";
        if (net_) net_->post(std::move(c));
        return false;
    }
    return true;
}

size_t TcpSocket::pendingSendBytes() const {
    std::lock_guard<std::mutex> lock(sendMu_);
    return pendingSend_.size();
}

void TcpSocket::clearPendingSend() {
    std::lock_guard<std::mutex> lock(sendMu_);
    pendingSend_.clear();
}

void TcpSocket::flushSend() {
    if (!stream_ || !connected_) return;

    std::vector<char> local;
    {
        std::lock_guard<std::mutex> lock(sendMu_);
        if (pendingSend_.empty()) return;
        local.swap(pendingSend_);
    }

    size_t off = 0;
    bool failed = false;
    while (off < local.size()) {
        try {
            int n = stream_->sendBytes(local.data() + static_cast<long>(off),
                                       static_cast<int>(local.size() - off));
            if (n <= 0) break;  // non-blocking would-block (Poco returns -1) or zero progress
            off += static_cast<size_t>(n);
        } catch (const Poco::TimeoutException&) {
            break;  // would block; keep the remainder for the next poll
        } catch (const Poco::IOException& e) {
            // Poco maps EAGAIN/WSAEWOULDBLOCK to IOException("Operation would
            // block") on some platforms instead of returning -1. Non-fatal.
            if (e.code() == POCO_EWOULDBLOCK || e.code() == POCO_EAGAIN) break;
            failed = true;
            break;
        } catch (...) {
            failed = true;
            break;
        }
    }

    if (failed && net_) {
        NetCompletion c;
        c.type   = NetEvType::Err;
        c.kind   = NetKind::Tcp;
        c.handle = this;
        c.reason = "closed";
        net_->post(std::move(c));
        close();
    }

    if (!failed && off < local.size()) {
        std::lock_guard<std::mutex> lock(sendMu_);
        pendingSend_.insert(pendingSend_.begin(),
                            local.begin() + static_cast<long>(off),
                            local.end());
    }
}

void TcpSocket::close() {
    if (net_) net_->unwatchTcp(this);
    connected_ = false;
    listening_ = false;
    clearPendingSend();
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
